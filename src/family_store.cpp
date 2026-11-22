#include "family_store.h"

#include <LittleFS.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/etharp.h>
#ifdef ESP_PLATFORM
#include <lwip/netif.h>
#include <lwip/priv/tcpip_priv.h>
#endif
#include <time.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <new>

using siblock::DeviceId;
using siblock::Family;

namespace {
constexpr const char* kPath = "/familia.txt";
constexpr const char* kNew = "/familia.new";
constexpr size_t kCacheSize = 16;
constexpr uint32_t kCacheMs = 5 * 60000UL;  // una IP reconocida se vuelve a preguntar a los 5 min
constexpr uint32_t kMissMs = 30000UL;       // una IP que la tabla ARP no tenía, a los 30 s
constexpr uint32_t kMaxPauseMin = 1440;

struct CacheEntry {
  uint32_t ip;
  DeviceId id;
  bool used, ok;
  uint32_t checkedMs;
};

SemaphoreHandle_t gMutex = nullptr;      // protege todo lo de abajo
SemaphoreHandle_t gSaveMutex = nullptr;  // un guardado a la vez
Family gFamily;
CacheEntry gCache[kCacheSize];
uint32_t gPauseUntil[siblock::kMaxDevices];
bool gPaused[siblock::kMaxDevices];
std::atomic<bool> gActive{false};  // hay horarios o pausas: si no, el DNS no pregunta nada

struct Lock {
  explicit Lock(TickType_t wait = portMAX_DELAY) : ok(xSemaphoreTake(gMutex, wait) == pdTRUE) {}
  ~Lock() {
    if (ok) xSemaphoreGive(gMutex);
  }
  bool ok;
};

void newSalt(Family& f) {
  for (size_t i = 0; i < sizeof f.salt; i += 4) {
    uint32_t r = esp_random();
    std::memcpy(f.salt + i, &r, 4);
  }
  f.hasSalt = true;
}

// Con el lock tomado.
void refreshActive() {
  bool any = gFamily.scheduleCount() > 0;
  for (size_t i = 0; !any && i < siblock::kMaxDevices; i++) any = gPaused[i];
  gActive = any;
}

// Con el lock tomado. Las huellas dependen de la sal: al cambiarla se olvida la caché.
void clearVolatile() {
  std::memset(gCache, 0, sizeof gCache);
  std::memset(gPauseUntil, 0, sizeof gPauseUntil);
  std::memset(gPaused, 0, sizeof gPaused);
}

// Con el lock tomado.
bool pauseActive(int slot, uint32_t now) {
  if (!gPaused[slot]) return false;
  if (int32_t(gPauseUntil[slot] - now) > 0) return true;
  gPaused[slot] = false;  // venció: se apaga la marca (si no, a los 24,8 días "volvería")
  refreshActive();
  return false;
}

// Escribe el texto en /familia.new y lo pone en lugar de /familia.txt. Con gSaveMutex tomado.
bool writeFile(const std::string& text) {
  fs::File f = LittleFS.open(kNew, FILE_WRITE);
  bool ok = f && f.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
  if (f) f.close();
  if (ok && !LittleFS.rename(kNew, kPath)) {
    LittleFS.remove(kPath);  // por si alguna versión no pisa el destino
    ok = LittleFS.rename(kNew, kPath);
  }
  if (!ok) {
    LittleFS.remove(kNew);
    Serial.println("[familia] no se pudo guardar /familia.txt");
  }
  return ok;
}

// Cambia una copia, la guarda y solo si se guardó la pone en uso: si la flash falla, el panel
// dice "no se pudo guardar" y de verdad nada cambió. edit(copia) devuelve falso si el cambio no
// vale; after() corre con el lock tomado justo después de ponerla en uso.
template <class Edit, class After>
bool commit(Edit edit, After after) {
  if (!gSaveMutex || xSemaphoreTake(gSaveMutex, portMAX_DELAY) != pdTRUE) return false;
  // Unos 1,5 KB: en el heap, no en la pila de la tarea web.
  std::unique_ptr<Family> next(new (std::nothrow) Family());
  bool ok = next != nullptr;
  if (ok) {
    {
      Lock l;
      *next = gFamily;
    }
    ok = edit(*next) && writeFile(siblock::writeFamily(*next));
  }
  if (ok) {
    Lock l;
    gFamily = *next;
    after();
    refreshActive();
  }
  xSemaphoreGive(gSaveMutex);
  return ok;
}

// ---------- Tabla ARP ----------

struct ArpCall {
  struct tcpip_api_call_data call;  // primero: lwIP recibe un puntero a esto
  ip4_addr_t ip;
  uint8_t mac[6];
  bool found;
};

err_t arpFind(struct tcpip_api_call_data* c) {
  ArpCall* a = reinterpret_cast<ArpCall*>(c);
  struct eth_addr* eth = nullptr;
  const ip4_addr_t* ipr = nullptr;
  a->found = false;
  if (netif_default && etharp_find_addr(netif_default, &a->ip, &eth, &ipr) >= 0 && eth) {
    std::memcpy(a->mac, eth->addr, 6);
    a->found = true;
  }
  return ERR_OK;
}

// La tabla ARP vive en el hilo de lwIP: se le pregunta desde ahí (tcpip_api_call).
bool arpLookup(uint32_t ip, uint8_t mac[6]) {
  ArpCall a;
  std::memset(&a, 0, sizeof a);
  a.ip.addr = ip;
  if (tcpip_api_call(arpFind, &a.call) != ERR_OK || !a.found) return false;
  std::memcpy(mac, a.mac, 6);
  return true;
}
}  // namespace

namespace family {

bool readFile(const char* path, siblock::BackupReader& r) {
  fs::File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  if (f.size() > siblock::kBackupMaxBytes) {
    f.close();
    r.failTooBig();
    return false;
  }
  char buf[siblock::kBackupMaxLine + 2];  // +1 para el '\r' y +1 para notar una línea larga
  size_t n = 0;
  bool ok = true;
  uint8_t chunk[128];
  while (ok) {
    size_t got = f.read(chunk, sizeof chunk);
    if (!got) break;
    for (size_t i = 0; i < got && ok; i++) {
      char c = char(chunk[i]);
      if (c == '\n') {
        ok = r.line(buf, n);
        n = 0;
      } else if (n < sizeof buf) {
        buf[n++] = c;
      }  // si no: la línea ya es demasiado larga y el lector la rechaza
    }
  }
  f.close();
  if (ok && n) ok = r.line(buf, n);
  return ok;
}

void begin() {
  gMutex = xSemaphoreCreateMutex();
  gSaveMutex = xSemaphoreCreateMutex();
  std::unique_ptr<siblock::BackupReader> r(new (std::nothrow) siblock::BackupReader(false));
  fs::File probe = LittleFS.open(kPath, FILE_READ);
  bool exists = bool(probe);
  if (probe) probe.close();
  bool ok = r && exists && readFile(kPath, *r) && r->finish();
  Lock l;
  clearVolatile();
  if (ok) {
    gFamily = r->result().family;
    Serial.printf("[familia] /familia.txt: %u aparatos, %u horarios\n", unsigned(__builtin_popcount(gFamily.usedMask())),
                  unsigned(gFamily.scheduleCount()));
  } else {
    gFamily.clear();
    newSalt(gFamily);
    if (exists)
      Serial.printf("[familia] /familia.txt dañado (%s): se empieza sin aparatos\n", r ? r->error() : "sin memoria");
  }
  refreshActive();
}

bool clockOk() { return time(nullptr) >= 1700000000; }

bool idForIp(uint32_t ip, DeviceId& out) {
  if (!gMutex || ip == 0) return false;
  uint32_t now = millis();
  uint8_t salt[16];
  {
    Lock l(pdMS_TO_TICKS(50));
    if (!l.ok) return false;
    for (const CacheEntry& e : gCache) {
      if (e.used && e.ip == ip && now - e.checkedMs < (e.ok ? kCacheMs : kMissMs)) {
        if (e.ok) out = e.id;
        return e.ok;
      }
    }
    std::memcpy(salt, gFamily.salt, sizeof salt);
  }
  uint8_t mac[6];
  bool found = arpLookup(ip, mac);
  DeviceId id{};
  if (found) id = siblock::deviceIdFor(salt, mac);
  std::memset(mac, 0, sizeof mac);
  Lock l(pdMS_TO_TICKS(50));
  if (!l.ok) return false;
  if (std::memcmp(salt, gFamily.salt, sizeof salt) != 0) return false;  // la sal cambió mientras tanto
  // Casilla: la misma IP, una libre o la más vieja.
  size_t at = 0;
  uint32_t oldest = 0;
  for (size_t i = 0; i < kCacheSize; i++) {
    const CacheEntry& e = gCache[i];
    if (e.used && e.ip == ip) {
      at = i;
      break;
    }
    uint32_t age = e.used ? now - e.checkedMs : UINT32_MAX;
    if (age >= oldest) {
      oldest = age;
      at = i;
    }
  }
  gCache[at] = {ip, id, true, found, now};
  if (found) out = id;
  return found;
}

bool seenRecently(const DeviceId& id, uint32_t* ip) {
  if (!gMutex) return false;
  uint32_t now = millis();
  Lock l;
  for (const CacheEntry& e : gCache) {
    if (e.used && e.ok && e.id == id && now - e.checkedMs < kCacheMs) {
      if (ip) *ip = e.ip;
      return true;
    }
  }
  return false;
}

Decision decide(uint32_t ip, const std::string& name) {
  if (!gActive.load()) return Decision::kNone;
  DeviceId id;
  if (!idForIp(ip, id)) return Decision::kNone;
  int32_t mow;
  Lock l(pdMS_TO_TICKS(50));
  if (!l.ok) return Decision::kNone;  // mejor dejar pasar que trabar el DNS
  int slot = gFamily.slotOf(id);
  if (slot < 0) return Decision::kNone;
  mow = siblock::localMinuteOfWeek(int64_t(time(nullptr)), gFamily.tz);
  if (gFamily.blockedBySchedule(slot, name, mow)) return Decision::kBlockSchedule;
  if (pauseActive(slot, millis())) return Decision::kPaused;
  return Decision::kNone;
}

void diagFor(uint32_t ip, bool& paused, uint8_t& scheduleGroups) {
  paused = false;
  scheduleGroups = 0;
  if (!gActive.load()) return;
  DeviceId id;
  if (!idForIp(ip, id)) return;
  Lock l(pdMS_TO_TICKS(50));
  if (!l.ok) return;
  int slot = gFamily.slotOf(id);
  if (slot < 0) return;
  if (clockOk()) scheduleGroups = gFamily.activeGroups(slot, siblock::localMinuteOfWeek(int64_t(time(nullptr)), gFamily.tz));
  paused = pauseActive(slot, millis());
}

int slotOf(const DeviceId& id) {
  Lock l;
  return gFamily.slotOf(id);
}

size_t namedCount() {
  Lock l;
  return size_t(__builtin_popcount(gFamily.usedMask()));
}

uint32_t usedMask() {
  Lock l;
  return gFamily.usedMask();
}

size_t scheduleCount() {
  Lock l;
  return gFamily.scheduleCount();
}

int16_t tz() {
  Lock l;
  return gFamily.tz;
}

bool setName(const DeviceId& id, const String& name) {
  std::string n(name.c_str(), name.length());
  return commit([&](Family& f) { return f.setName(id, n); }, [] {});
}

bool forget(const DeviceId& id) {
  int slot = -1;
  return commit(
      [&](Family& f) {
        slot = f.slotOf(id);
        return slot >= 0 && f.forget(id);
      },
      [&] { gPaused[slot] = false; });
}

bool pause(const DeviceId& id, uint32_t minutes) {
  if (minutes > kMaxPauseMin) return false;
  Lock l;
  int slot = gFamily.slotOf(id);
  if (slot < 0) return false;
  gPauseUntil[slot] = millis() + minutes * 60000UL;
  gPaused[slot] = minutes > 0;
  refreshActive();
  return true;
}

uint32_t pauseLeft(int slot) {
  if (slot < 0 || size_t(slot) >= siblock::kMaxDevices) return 0;
  uint32_t now = millis();
  Lock l;
  if (!pauseActive(slot, now)) return 0;
  return (gPauseUntil[slot] - now + 999) / 1000;
}

bool setSchedule(int index, const siblock::Schedule& s, int16_t tz) {
  if (!siblock::validTzMinutes(tz)) return false;
  return commit(
      [&](Family& f) {
        if (!f.setSchedule(index, s)) return false;
        f.tz = tz;
        return true;
      },
      [] {});
}

bool removeSchedule(int index) {
  return commit([&](Family& f) { return f.removeSchedule(index); }, [] {});
}

void snapshot(Family& out) {
  Lock l;
  out = gFamily;
}

bool replace(const Family& f) {
  if (!f.hasSalt) return false;
  return commit(
      [&](Family& next) {
        next = f;
        return true;
      },
      [] { clearVolatile(); });
}

void erase() {
  if (!gMutex) {
    LittleFS.remove(kPath);
    return;
  }
  // Un guardado a medias no debe volver a escribir lo que se acaba de borrar.
  bool saving = gSaveMutex && xSemaphoreTake(gSaveMutex, portMAX_DELAY) == pdTRUE;
  {
    Lock l;
    LittleFS.remove(kPath);
    LittleFS.remove(kNew);
    gFamily.clear();
    newSalt(gFamily);
    clearVolatile();
    refreshActive();
  }
  if (saving) xSemaphoreGive(gSaveMutex);
}

}  // namespace family

#include "blocklist.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "certs.h"
#include "community.h"
#include "config.h"
#include "lists.h"

using namespace adblock;

namespace {
constexpr const char* kListPath = "/list.bin";  // [claves ordenadas][índice]
constexpr const char* kOldIdxPath = "/list.idx";  // formato viejo (dos archivos): se borra
constexpr const char* kListNew = "/list.new";
constexpr const char* kRulesPath = "/custom.txt";
constexpr const char* kRulesNew = "/custom.new";
constexpr const char* kTmpDir = "/tmp";
constexpr int kBucketBits = 6;  // 64 archivos temporales: al ordenar, cada uno cabe en ~40 KB
constexpr int kBuckets = 1 << kBucketBits;
constexpr int kBucketBuf = 64;  // claves en RAM por archivo antes de escribir (20 KB en total)
constexpr uint32_t kIndexBytes = kIndexSize * 4;
constexpr size_t kChunk = 64;    // claves leídas por lectura al buscar

// Búferes de búsqueda: protegidos por el mutex de BlockList.
uint8_t gLookupRaw[kChunk * kKeyBytes];
uint64_t gLookupKeys[kChunk];
// Búfer para armar la lista (solo lo usa la tarea que actualiza).
uint8_t gBuildRaw[kChunk * kKeyBytes];

String bucketPath(int b) {
  char p[24];
  snprintf(p, sizeof p, "%s/b%02d.bin", kTmpDir, b);
  return String(p);
}

bool appendFile(const String& path, const void* data, size_t len) {
  fs::File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return false;
  size_t w = f.write(static_cast<const uint8_t*>(data), len);
  f.close();
  return w == len;
}

bool writeAll(fs::File& f, const void* data, size_t len) {
  return f.write(static_cast<const uint8_t*>(data), len) == len;
}

struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};

// Reemplaza dst por src. LittleFS (como POSIX) pisa el destino en un solo paso, así que un
// corte de luz deja la versión vieja o la nueva, nunca ninguna.
bool replaceFile(const char* src, const char* dst) {
  if (LittleFS.rename(src, dst)) return true;
  LittleFS.remove(dst);  // por si alguna versión no pisa el destino
  return LittleFS.rename(src, dst);
}
}  // namespace

bool BlockList::begin() {
  mutex_ = xSemaphoreCreateMutex();
  cleanupTemp();
  fs::File f = LittleFS.open(kRulesPath, FILE_READ);
  if (f) {
    std::string text;
    text.reserve(f.size());
    while (f.available()) text += char(f.read());
    f.close();
    Lock l(mutex_);
    rules_.parse(text);
  }
  return loadFromFs();
}

String BlockList::lastError() const {
  Lock l(mutex_);
  return lastError_;
}

// Abre un archivo de lista y comprueba que sea coherente: tamaño = claves + índice, índice que
// nunca baja y que termina en n. Deja el archivo abierto y el índice en RAM.
static bool openListFile(const char* path, fs::File& fl, std::unique_ptr<uint32_t[]>& idx,
                         uint32_t& n) {
  idx.reset(new (std::nothrow) uint32_t[kIndexSize]);
  if (!idx) return false;
  fl = LittleFS.open(path, FILE_READ);
  if (!fl) return false;
  size_t size = fl.size();
  bool ok = size >= kIndexBytes && (size - kIndexBytes) % kKeyBytes == 0 &&
            fl.seek(size - kIndexBytes) &&
            fl.read(reinterpret_cast<uint8_t*>(idx.get()), kIndexBytes) == kIndexBytes;
  n = ok ? idx[kIndexSize - 1] : 0;
  ok = ok && idx[0] == 0 && size_t(n) * kKeyBytes == size - kIndexBytes;
  for (size_t i = 1; ok && i < kIndexSize; i++)
    if (idx[i] < idx[i - 1]) ok = false;  // índice dañado
  if (!ok) fl.close();
  return ok;
}

bool BlockList::loadFromFs() {
  std::unique_ptr<uint32_t[]> idx;
  fs::File fl;
  uint32_t n = 0;
  if (!openListFile(kListPath, fl, idx, n)) return false;
  Lock l(mutex_);
  if (file_) file_.close();
  file_ = fl;
  index_ = std::move(idx);
  count_ = n;
  return true;
}

bool BlockList::lookupKey(uint64_t key) {
  // Si la lista está ocupada (cambio de lista) no se espera: mejor dejar pasar que trabar el DNS.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  bool found = false;
  if (index_ && file_) {
    uint32_t b = indexBucket(key);
    uint32_t pos = index_[b], end = index_[b + 1];
    while (pos < end) {
      size_t n = std::min<size_t>(kChunk, end - pos);
      if (!file_.seek(size_t(pos) * kKeyBytes) ||
          file_.read(gLookupRaw, n * kKeyBytes) != n * kKeyBytes)
        break;
      for (size_t i = 0; i < n; i++) gLookupKeys[i] = unpackKey(&gLookupRaw[i * kKeyBytes]);
      if (gLookupKeys[n - 1] >= key) {  // si está, está en este pedazo
        found = containsSorted(gLookupKeys, n, key);
        break;
      }
      pos += n;
    }
  }
  xSemaphoreGive(mutex_);
  return found;
}

Verdict BlockList::check(const std::string& name) {
  for (const char* a : ALLOWLIST) {
    size_t al = strlen(a);
    if (name.size() == al && name == a) return Verdict::kAllowedCustom;
    if (name.size() > al && name[name.size() - al - 1] == '.' &&
        name.compare(name.size() - al, al, a) == 0)
      return Verdict::kAllowedCustom;
  }
  // Si el mutex está ocupado (guardando reglas o cambiando de lista) no se espera más de 50 ms:
  // mejor dejar pasar una consulta que trabar el DNS de la casa.
  CustomRules::Result r = CustomRules::kNone;
  bool community = false;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
    r = rules_.match(name);
    community = siblock::communityAllows(community_, name);
    xSemaphoreGive(mutex_);
  }
  if (r == CustomRules::kAllow) return Verdict::kAllowedCustom;
  if (pausedSecondsLeft() > 0) return Verdict::kPaused;
  // Tus reglas "Bloquear" ganan siempre sobre los permitidos de la comunidad.
  if (r == CustomRules::kBlock) return Verdict::kBlockedCustom;
  bool hit = forEachSuffix(name, [this](const char* p, size_t n) {
    return lookupKey(keyOf(hashDomain(p, n)));
  });
  // Solo se dice "comunidad" cuando cambió algo: lo que la lista no bloquea pasa como siempre.
  if (hit && community) return Verdict::kAllowedCommunity;
  return hit ? Verdict::kBlockedList : Verdict::kAllowed;
}

void BlockList::setCommunity(std::vector<uint64_t> keys) {
  Lock l(mutex_);
  community_ = std::move(keys);
}

// ---------- Reglas propias y pausa ----------

bool BlockList::saveRules() {
  std::string text;
  {
    Lock l(mutex_);
    text = rules_.serialize();
  }
  fs::File f = LittleFS.open(kRulesNew, FILE_WRITE);
  if (!f) return false;
  bool ok = writeAll(f, text.data(), text.size());
  f.close();
  return ok && replaceFile(kRulesNew, kRulesPath);
}

bool BlockList::addRule(const std::string& domain, bool block) {
  bool ok;
  {
    Lock l(mutex_);
    ok = rules_.add(domain, block);
  }
  return ok && saveRules();
}

bool BlockList::removeRule(const std::string& domain) {
  bool ok;
  {
    Lock l(mutex_);
    ok = rules_.remove(domain);
  }
  return ok && saveRules();
}

bool BlockList::replaceRules(const std::vector<std::string>& block, const std::vector<std::string>& allow) {
  CustomRules nr;
  for (const auto& d : block)
    if (!nr.add(d, true)) return false;
  for (const auto& d : allow)
    if (!nr.add(d, false)) return false;
  std::string text = nr.serialize();
  fs::File f = LittleFS.open(kRulesNew, FILE_WRITE);
  if (!f) return false;
  bool ok = writeAll(f, text.data(), text.size());
  f.close();
  if (!ok || !replaceFile(kRulesNew, kRulesPath)) {
    LittleFS.remove(kRulesNew);
    return false;
  }
  Lock l(mutex_);
  rules_ = std::move(nr);
  return true;
}

void BlockList::rules(std::vector<std::string>& blocked, std::vector<std::string>& allowed) {
  Lock l(mutex_);
  blocked = rules_.blocked();
  allowed = rules_.allowed();
}

// La pausa la cambia la tarea web y la vence la tarea DNS: ambas pasan por esta sección crítica.
static portMUX_TYPE gPauseMux = portMUX_INITIALIZER_UNLOCKED;

void BlockList::pause(uint32_t seconds) {
  portENTER_CRITICAL(&gPauseMux);
  pauseUntilMs_ = millis() + seconds * 1000;
  paused_ = seconds > 0;
  portEXIT_CRITICAL(&gPauseMux);
}

uint32_t BlockList::pausedSecondsLeft() const {
  if (!paused_) return 0;
  // Resta sin signo: aguanta el desborde de millis(). Al vencer se apaga la marca; si no,
  // 24,8 días después la resta "volvería" a parecer futura.
  portENTER_CRITICAL(&gPauseMux);
  int32_t left = int32_t(pauseUntilMs_ - millis());
  if (left <= 0) paused_ = false;
  portEXIT_CRITICAL(&gPauseMux);
  return left > 0 ? uint32_t(left + 999) / 1000 : 0;
}

// ---------- Descarga y armado de la lista ----------

void BlockList::cleanupTemp() {
  for (int b = 0; b < kBuckets; b++) LittleFS.remove(bucketPath(b));
  LittleFS.remove(kListNew);
  LittleFS.remove(kRulesNew);
  LittleFS.remove(kOldIdxPath);
  LittleFS.remove("/idx.new");
  LittleFS.mkdir(kTmpDir);
}

bool BlockList::download(const char* url, uint32_t& added) {
  String err;
  WiFiClientSecure client;
  client.setCACert(ROOT_CAS);
  HTTPClient http;
  http.useHTTP10(true);  // sin "chunked": el cuerpo llega limpio
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    Lock l(mutex_);
    lastError_ = "URL inválida";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    err = "HTTP " + String(code) + " " + http.errorToString(code);
    http.end();
    Lock l(mutex_);
    lastError_ = err;
    return false;
  }

  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kBuckets * kBucketBuf * kKeyBytes]);
  uint16_t fill[kBuckets] = {0};
  if (!buf) {
    http.end();
    Lock l(mutex_);
    lastError_ = "sin memoria para el búfer";
    return false;
  }
  auto flush = [&](int b) {
    bool ok = appendFile(bucketPath(b), &buf[b * kBucketBuf * kKeyBytes], fill[b] * kKeyBytes);
    fill[b] = 0;
    return ok;
  };

  WiFiClient* s = http.getStreamPtr();
  int remaining = http.getSize();  // -1 si el servidor no dice el tamaño
  if (remaining < 0) sizeUnknown_ = true;
  static char line[512];
  size_t lineLen = 0;
  bool overflow = false, ok = true;
  std::string dom;
  uint8_t chunk[1024];
  uint32_t lastData = millis();

  auto handleLine = [&]() {
    if (!overflow && parseListLine(line, lineLen, dom)) {
      if (added >= MAX_DOMAINS) {  // se ignora lo que pase del tope
        skipping_++;
        return true;
      }
      uint64_t key = keyOf(hashDomain(dom));
      int b = int(key >> (kKeyBits - kBucketBits));
      packKey(key, &buf[(b * kBucketBuf + fill[b]) * kKeyBytes]);
      fill[b]++;
      added++;
      if (fill[b] == kBucketBuf && !flush(b)) return false;
    }
    return true;
  };

  while (ok && (remaining > 0 || remaining == -1)) {
    size_t avail = s->available();
    if (!avail) {
      if (!http.connected()) break;
      if (millis() - lastData > 20000) {
        err = "la descarga se quedó sin datos";
        ok = false;
        break;
      }
      delay(2);
      continue;
    }
    int n = s->readBytes(chunk, std::min(avail, sizeof chunk));
    if (n <= 0) continue;
    lastData = millis();
    if (remaining > 0) remaining -= n;
    for (int i = 0; i < n && ok; i++) {
      char c = char(chunk[i]);
      if (c == '\n') {
        ok = handleLine();
        lineLen = 0;
        overflow = false;
      } else if (lineLen < sizeof line) {
        line[lineLen++] = c;
      } else {
        overflow = true;  // línea absurda: se descarta entera
      }
    }
  }
  if (ok && lineLen) ok = handleLine();
  http.end();

  if (ok && remaining > 0) {
    err = "descarga incompleta";
    ok = false;
  }
  for (int b = 0; ok && b < kBuckets; b++)
    if (fill[b] && !flush(b)) ok = false;
  if (!ok && err.isEmpty()) err = "no se pudo escribir en la flash (¿llena?)";
  if (!ok) {
    Lock l(mutex_);
    lastError_ = err;
  }
  return ok;
}

bool BlockList::buildFromBuckets(uint32_t& total) {
  total = 0;
  std::unique_ptr<uint32_t[]> idx(new (std::nothrow) uint32_t[kIndexSize]);
  if (!idx) return false;
  fs::File out = LittleFS.open(kListNew, FILE_WRITE);
  if (!out) return false;
  uint32_t nextIdx = 0;
  bool ok = true;

  for (int b = 0; ok && b < kBuckets; b++) {
    String path = bucketPath(b);
    fs::File f = LittleFS.open(path, FILE_READ);
    size_t n = f ? f.size() / kKeyBytes : 0;
    if (n) {
      // Se lee de a pedazos directo a las claves: no hace falta un búfer crudo del tamaño entero.
      std::unique_ptr<uint64_t[]> keys(new (std::nothrow) uint64_t[n]);
      if (!keys) ok = false;
      for (size_t i = 0; ok && i < n; i += kChunk) {
        size_t m = std::min(kChunk, n - i);
        if (f.read(gBuildRaw, m * kKeyBytes) != m * kKeyBytes) ok = false;
        for (size_t k = 0; ok && k < m; k++) keys[i + k] = unpackKey(&gBuildRaw[k * kKeyBytes]);
      }
      if (ok) {
        std::sort(keys.get(), keys.get() + n);
        n = std::unique(keys.get(), keys.get() + n) - keys.get();
        size_t fillRaw = 0;
        for (size_t i = 0; ok && i < n; i++) {
          uint32_t ib = indexBucket(keys[i]);
          while (nextIdx <= ib) idx[nextIdx++] = total;
          total++;
          packKey(keys[i], &gBuildRaw[fillRaw * kKeyBytes]);
          if (++fillRaw == kChunk) {
            ok = writeAll(out, gBuildRaw, fillRaw * kKeyBytes);
            fillRaw = 0;
          }
        }
        if (ok && fillRaw) ok = writeAll(out, gBuildRaw, fillRaw * kKeyBytes);
      }
    }
    if (f) f.close();
    LittleFS.remove(path);  // libera espacio antes de seguir
    delay(1);
  }
  while (nextIdx < kIndexSize) idx[nextIdx++] = total;
  if (ok) ok = writeAll(out, idx.get(), kIndexBytes);  // el índice va al final del mismo archivo
  out.close();
  if (!ok) return false;

  // Una lista nueva mucho más chica que la actual suele ser una descarga rota o una lista
  // que cambió de formato: se queda la actual (regla en siblock::acceptListSize).
  if (!siblock::acceptListSize(total, count_, sizeUnknown_, rejectedTotal_, allowShrink_)) {
    rejectedTotal_ = total;
    Lock l(mutex_);
    lastError_ = "la lista nueva trae " + String(total) + " dominios (antes " + String(count_) +
                 "); se queda la anterior";
    return false;
  }

  rejectedTotal_ = 0;
  // Antes de pisar la vieja se relee la nueva desde la flash (un close() no avisa si la flash se
  // llenó al final): si no está entera, se queda la vieja.
  idx.reset();  // ya está escrito; libera 16 KB para la comprobación
  {
    std::unique_ptr<uint32_t[]> chk;
    fs::File f;
    uint32_t n = 0;
    bool good = openListFile(kListNew, f, chk, n) && n == total;
    if (f) f.close();
    if (!good) {
      Lock l(mutex_);
      lastError_ = "la lista nueva quedó incompleta en la flash; se queda la anterior";
      return false;
    }
  }
  // Cambio a la lista nueva: un solo rename que pisa la vieja.
  {
    Lock l(mutex_);
    if (file_) file_.close();
    index_.reset();
    count_ = 0;
    ok = replaceFile(kListNew, kListPath);
  }
  if (ok && loadFromFs()) return true;
  Lock l(mutex_);
  lastError_ = "no se pudo abrir la lista nueva";
  return false;
}

void BlockList::noteError(const String& msg) {
  Lock l(mutex_);
  lastError_ = msg;
}

bool BlockList::update(const char* const* urls, size_t nUrls, bool allowShrink) {
  updating_ = true;
  allowShrink_ = allowShrink;
  {
    Lock l(mutex_);
    lastError_ = "";
  }
  cleanupTemp();
  skipping_ = 0;
  sizeUnknown_ = false;
  uint32_t t0 = millis(), added = 0, total = 0;
  bool ok = true;
  for (size_t i = 0; ok && i < nUrls; i++) {
    uint32_t before = added, skippedBefore = skipping_;
    Serial.printf("[lista] %u/%u %s (RAM libre %u KB)\n", unsigned(i + 1), unsigned(nUrls),
                  urls[i], unsigned(ESP.getFreeHeap() / 1024));
    ok = download(urls[i], added);
    if (ok && added == before && skipping_ == skippedBefore) {  // con tope lleno no es error
      Lock l(mutex_);
      lastError_ = String("lista vacía: ") + urls[i];
      ok = false;
    }
  }
  if (ok) {
    ok = buildFromBuckets(total);
    if (!ok) {
      Lock l(mutex_);
      if (lastError_.isEmpty()) lastError_ = "no se pudo armar la lista (¿flash llena?)";
    }
  }
  cleanupTemp();
  if (ok) {
    lastUpdateMs_ = millis();
    everUpdated_ = true;
    skipped_ = skipping_;
    if (skipped_)
      Serial.printf("[lista] AVISO: se llegó al tope MAX_DOMAINS; %u dominios ignorados\n",
                    unsigned(skipped_));
    Serial.printf("[lista] lista nueva: %u dominios en %u s\n", unsigned(total),
                  unsigned((millis() - t0) / 1000));
  } else {
    Serial.printf("[lista] ERROR: %s (se queda la lista anterior: %u dominios)\n",
                  lastError().c_str(), unsigned(count_));
    if (!count_) loadFromFs();
  }
  updating_ = false;
  return ok;
}

#include "settings.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstring>
#include <string>

#include "cloud.h"
#include "config.h"
#include "lists.h"
#include "notify.h"
#include "siblock_crypto.h"
#include "siblock_text.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {
Preferences gPrefs;
bool gOpen = false;
constexpr size_t kSaltLen = 16;
constexpr uint32_t kMaxIterations = 1000000;  // un valor raro en NVS no deja la placa pensando horas

std::string str(const String& s) { return std::string(s.c_str(), s.length()); }

// Leer una clave que no existe hace que la placa imprima un error: se pregunta antes.
String getStr(const char* key) { return gOpen && gPrefs.isKey(key) ? gPrefs.getString(key) : String(); }

bool getBlob(const char* key, uint8_t* out, size_t len) {
  return gOpen && gPrefs.isKey(key) && gPrefs.getBytesLength(key) == len && gPrefs.getBytes(key, out, len) == len;
}

// putString devuelve 0 tanto si falla como si el texto es vacío (red abierta).
bool putStr(const char* key, const String& v) {
  size_t n = gPrefs.putString(key, v);
  return v.length() ? n == v.length() : gPrefs.isKey(key);
}

void removeKey(const char* key) {
  if (gOpen && gPrefs.isKey(key)) gPrefs.remove(key);
}

uint32_t randomWord() { return esp_random(); }

// La contraseña del panel son tres claves (sal, iteraciones, hash). Un cambio se escribe primero
// en las claves "nuevas" (el hash al final); recién completas se copian a las de siempre y se
// borran. Si se corta la luz o falla una escritura a mitad, la que vale es la que esté completa:
// primero la nueva, si no la de siempre. Así cambiar la contraseña nunca deja el panel sin ella.
struct Cred {
  uint8_t salt[kSaltLen];
  uint8_t hash[32];
  uint32_t iter = 0;
};
constexpr const char* kNewSalt = "panel_nsalt";
constexpr const char* kNewIter = "panel_niter";
constexpr const char* kNewHash = "panel_nhash";

bool loadSet(const char* saltKey, const char* iterKey, const char* hashKey, Cred& c) {
  if (!getBlob(saltKey, c.salt, kSaltLen) || !getBlob(hashKey, c.hash, 32) || !gPrefs.isKey(iterKey)) return false;
  c.iter = gPrefs.getUInt(iterKey, 0);
  return c.iter > 0;
}

bool loadCred(Cred& c) {
  return loadSet(kNewSalt, kNewIter, kNewHash, c) || loadSet(NVS_PANEL_SALT, NVS_PANEL_ITER, NVS_PANEL_HASH, c);
}

bool writeSet(const char* saltKey, const char* iterKey, const char* hashKey, const Cred& c) {
  removeKey(hashKey);  // sin hash el juego no vale: el hash va al final
  return gPrefs.putBytes(saltKey, c.salt, kSaltLen) == kSaltLen && gPrefs.putUInt(iterKey, c.iter) > 0 &&
         gPrefs.putBytes(hashKey, c.hash, 32) == 32;
}

void dropNewSet() {
  removeKey(kNewHash);  // primero el hash: un juego a medias no cuenta
  removeKey(kNewSalt);
  removeKey(kNewIter);
}

// Pasa el juego nuevo (completo) a las claves de siempre. Si algo falla, el nuevo se queda y
// sigue valiendo; se reintenta en el próximo arranque.
bool commitNewSet() {
  Cred c;
  if (!loadSet(kNewSalt, kNewIter, kNewHash, c)) {
    dropNewSet();  // a medias: no se llegó a guardar, vale la de siempre
    return true;
  }
  bool ok = writeSet(NVS_PANEL_SALT, NVS_PANEL_ITER, NVS_PANEL_HASH, c);
  memset(c.hash, 0, sizeof c.hash);
  if (ok) dropNewSet();
  return ok;
}
}  // namespace

namespace settings {

void begin() {
  if (!gOpen) gOpen = gPrefs.begin(NVS_NAMESPACE, false);
  if (!gOpen) Serial.println("[config] no se pudo abrir la memoria de configuración");
  else if (!commitNewSet()) Serial.println("[config] no se pudo terminar de guardar la contraseña del panel");
}

bool hasWifi() { return !wifiSsid().isEmpty(); }
String wifiSsid() { return getStr(NVS_WIFI_SSID); }
String wifiPass() { return getStr(NVS_WIFI_PASS); }

bool setWifi(const String& ssid, const String& pass) {
  if (!gOpen || !siblock::validWifi(str(ssid), str(pass))) return false;
  // Primero la contraseña: si se corta la luz entre las dos, queda el WiFi viejo con otra
  // contraseña (no conecta y abre el modo de configuración), nunca una red a medias sin aviso.
  return putStr(NVS_WIFI_PASS, pass) && putStr(NVS_WIFI_SSID, ssid);
}

bool hasPanelPassword() {
  Cred c;
  bool ok = gOpen && loadCred(c);
  memset(c.hash, 0, sizeof c.hash);
  return ok;
}

bool setPanelPassword(const String& pw) {
  if (!gOpen || !siblock::validPanelPassword(str(pw))) return false;
  // Un cambio anterior a medio copiar se termina primero: las claves de siempre quedan completas
  // mientras se escribe el juego nuevo.
  if (!commitNewSet()) return false;
  Cred c;
  for (size_t i = 0; i < kSaltLen; i += 4) {
    uint32_t r = esp_random();
    memcpy(c.salt + i, &r, 4);
  }
  c.iter = kPanelIterations;
  siblock::pbkdf2Sha256(reinterpret_cast<const uint8_t*>(pw.c_str()), pw.length(), c.salt, kSaltLen, c.iter, c.hash);
  bool ok = writeSet(kNewSalt, kNewIter, kNewHash, c);
  memset(c.hash, 0, sizeof c.hash);
  if (!ok) {
    dropNewSet();  // la de antes sigue igual
    return false;
  }
  commitNewSet();  // si falla, la nueva igual vale (está completa en las claves nuevas)
  return true;
}

bool checkPanelPassword(const String& pw) {
  Cred c;
  uint8_t got[32];
  if (!gOpen || !loadCred(c)) return false;
  if (c.iter > kMaxIterations) return false;
  if (pw.length() == 0 || pw.length() > 64) return false;  // no gastar tiempo en algo imposible
  siblock::pbkdf2Sha256(reinterpret_cast<const uint8_t*>(pw.c_str()), pw.length(), c.salt, kSaltLen, c.iter, got);
  bool ok = siblock::equal32(got, c.hash);
  memset(got, 0, sizeof got);
  memset(c.hash, 0, sizeof c.hash);
  return ok;
}

String apPassword() {
  String p = getStr(NVS_AP_PASS);
  if (p.length() == 10) return p;
  // esp_random() solo es de verdad al azar con la radio encendida: main.cpp llama a esto
  // después de WiFi.mode().
  // Si no se pudo guardar, se usa la misma durante toda esta sesión: así la que sale por el
  // cable, la del punto de respaldo y la que muestra la página son siempre la misma.
  static String sCached;
  if (sCached.length() == 10) return sCached;
  std::string fresh = siblock::makeApPassword(randomWord);
  sCached = String(fresh.c_str());
  if (!gOpen || gPrefs.putString(NVS_AP_PASS, sCached) != 10)
    Serial.println("[ajustes] no se pudo guardar la contraseña de respaldo; vale hasta que se apague");
  return sCached;
}

String apName() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  return String(siblock::apSsid(mac).c_str());
}

void seedFromSecrets() {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  if (!hasWifi() && siblock::validWifi(WIFI_SSID, WIFI_PASSWORD)) {
    if (setWifi(WIFI_SSID, WIFI_PASSWORD)) Serial.println("[config] WiFi tomado de secrets.h");
  }
#endif
#if defined(WEB_PASSWORD)
  if (!hasPanelPassword() && siblock::validPanelPassword(WEB_PASSWORD)) {
    if (setPanelPassword(WEB_PASSWORD)) Serial.println("[config] contraseña del panel tomada de secrets.h");
  }
#endif
}

namespace {
String gInstallIdCache;  // si la NVS no guarda, el mismo número durante esta sesión
bool flagOn(const char* key) { return gOpen && gPrefs.isKey(key) && gPrefs.getUInt(key, 0) == 1; }
bool setFlag(const char* key, bool on) { return gOpen && gPrefs.putUInt(key, on ? 1 : 0) > 0; }
}  // namespace

String installId() {
  String s = getStr(NVS_INSTALL_ID);
  if (siblock::validInstallId(str(s))) return s;
  if (siblock::validInstallId(str(gInstallIdCache))) return gInstallIdCache;
  uint8_t rnd[16];
  for (size_t i = 0; i < sizeof rnd; i += 4) {
    uint32_t r = esp_random();
    memcpy(rnd + i, &r, 4);
  }
  gInstallIdCache = String(siblock::makeInstallId(rnd).c_str());
  if (!gOpen || gPrefs.putString(NVS_INSTALL_ID, gInstallIdCache) != 32)
    Serial.println("[ajustes] no se pudo guardar el número de instalación; vale hasta que se apague");
  return gInstallIdCache;
}

bool cloudShare() { return flagOn(NVS_CLOUD_SHARE); }
bool setCloudShare(bool on) { return setFlag(NVS_CLOUD_SHARE, on); }
bool versionCheck() { return flagOn(NVS_VER_CHECK); }
bool setVersionCheck(bool on) { return setFlag(NVS_VER_CHECK, on); }
bool community() { return flagOn(NVS_COMMUNITY); }
bool setCommunity(bool on) { return setFlag(NVS_COMMUNITY, on); }

bool relayBlock() { return gOpen && gPrefs.isKey(NVS_RELAY_BLOCK) && gPrefs.getUInt(NVS_RELAY_BLOCK, 0) == 1; }

bool setRelayBlock(bool on) { return gOpen && gPrefs.putUInt(NVS_RELAY_BLOCK, on ? 1 : 0) > 0; }

bool hasTelegramToken() { return gOpen && gPrefs.isKey(NVS_TG_TOKEN); }
String telegramToken() { return getStr(NVS_TG_TOKEN); }
bool setTelegramToken(const String& token) {
  return gOpen && siblock::validTelegramToken(str(token)) && putStr(NVS_TG_TOKEN, token);
}
void clearTelegramToken() { removeKey(NVS_TG_TOKEN); }
String telegramChat() { return getStr(NVS_TG_CHAT); }
bool setTelegramChat(const String& chat) {
  return gOpen && siblock::validTelegramChatId(str(chat)) && putStr(NVS_TG_CHAT, chat);
}
bool telegramOn() { return flagOn(NVS_TG_ON); }
bool setTelegramOn(bool on) { return setFlag(NVS_TG_ON, on); }
uint8_t telegramEvents() {
  if (!gOpen || !gPrefs.isKey(NVS_TG_EVENTS)) return kTgAll;
  return uint8_t(gPrefs.getUInt(NVS_TG_EVENTS, kTgAll) & kTgAll);
}
bool setTelegramEvents(uint8_t mask) { return gOpen && mask <= kTgAll && gPrefs.putUInt(NVS_TG_EVENTS, mask) > 0; }
uint32_t telegramWeek() { return gOpen && gPrefs.isKey(NVS_TG_WEEK) ? gPrefs.getUInt(NVS_TG_WEEK, 0) : 0; }
bool setTelegramWeek(uint32_t day) { return gOpen && gPrefs.putUInt(NVS_TG_WEEK, day) > 0; }
String telegramFirmware() { return getStr(NVS_TG_FW); }
bool setTelegramFirmware(const String& v) { return gOpen && putStr(NVS_TG_FW, v); }

static uint8_t readMask(const char* key) {
  if (!gOpen || !gPrefs.isKey(key)) return siblock::kListsDefault;
  uint32_t v = gPrefs.getUInt(key, siblock::kListsDefault);
  return siblock::validListMask(v) ? uint8_t(v) : siblock::kListsDefault;
}

static bool writeMask(const char* key, uint8_t mask) {
  return gOpen && siblock::validListMask(mask) && gPrefs.putUInt(key, mask) > 0;
}

uint8_t lists() { return readMask(NVS_LISTS); }
bool setLists(uint8_t mask) { return writeMask(NVS_LISTS, mask); }
uint8_t listsBuilt() { return readMask(NVS_LISTS_BUILT); }
bool setListsBuilt(uint8_t mask) { return writeMask(NVS_LISTS_BUILT, mask); }

void factoryReset() {
  gInstallIdCache = String();
  if (gOpen) gPrefs.clear();
}
}  // namespace settings

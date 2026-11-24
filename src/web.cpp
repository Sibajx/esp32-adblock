#include "fs_util.h"
#include "web.h"

#include <WebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include <time.h>

#include "cloud.h"
#include "community_store.h"
#include "config.h"
#include "diagnose.h"
#include "dns_names.h"
#include "family_store.h"
#include "lists.h"
#include "oui.h"
#include "portal.h"
#include "settings.h"
#include "siblock_crypto.h"
#include "siblock_text.h"
#include "telegram.h"
#include "usage.h"

namespace {
WebServer server(80);
BlockList* gList = nullptr;
DnsServer* gDns = nullptr;
volatile bool* gUpdateRequested = nullptr;
siblock::SetupWindow* gWindow = nullptr;

constexpr const char* kUser = "admin";
constexpr const char* kCsrfHeader = "X-Requested-With";
constexpr const char* kCsrfValue = "adblock";
constexpr const char* kTokenHeader = "X-Token";

// ---------- Sesiones ----------
// Al entrar con la contraseña se entrega un token al azar de 128 bits. El panel guarda solo el
// token (no la contraseña) y vence a las 12 h. Caben 4 sesiones; la más vieja se pisa.
constexpr int kSessions = 4;
constexpr uint32_t kSessionMs = 12UL * 3600 * 1000;
struct Session {
  bool used;
  char token[33];
  uint32_t issuedMs;
};
Session gSessions[kSessions];

// ---------- Bloqueo por intentos fallidos (por IP) ----------
// 5 contraseñas malas seguidas desde una IP la bloquean 30 s; cada bloqueo nuevo dobla la
// espera (hasta 16 min). Una IP no puede bloquear a las demás. Si las 16 casillas están
// ocupadas por IPs bloqueadas, las demás IPs comparten un turno: un intento cada 5 s entre
// todas (así cambiar de IP no sirve para probar contraseñas en masa y el dueño solo espera).
constexpr int kLockSlots = 16;
constexpr uint32_t kOverflowGapMs = 5000;
constexpr uint8_t kFailsBeforeLock = 5;
constexpr uint32_t kBaseLockMs = 30000;
constexpr uint8_t kMaxLevel = 5;                 // 30 s << 5 = 16 min
constexpr uint32_t kForgetMs = 60UL * 60 * 1000;  // sin fallos en 1 h se olvida la IP
struct LockSlot {
  uint32_t ip;
  uint8_t fails;
  uint8_t level;
  bool locked;
  uint32_t sinceMs;  // inicio del bloqueo o último fallo
};
LockSlot gLocks[kLockSlots];
// IPs que entraron con la contraseña buena hace poco: no esperan el turno compartido, así un
// atacante con muchas IPs no puede dejar al dueño sin poder entrar. Se olvidan a la hora, y
// una contraseña mala desde esa IP le quita el pase (si otro tomó esa IP, no prueba gratis).
constexpr uint32_t kTrustMs = 60UL * 60 * 1000;
uint32_t gTrusted[4] = {};
uint32_t gTrustedMs[4] = {};
uint8_t gTrustedNext = 0;
bool gOverflowBusy = false;  // hubo un intento fallido sin casilla hace menos de 5 s
uint32_t gOverflowMs = 0;

// El cuerpo de los POST se recibe en pedazos (sin pedir memoria del tamaño entero) y se
// guardan como mucho kMaxBody bytes. Si llega más, se contesta 413.
constexpr size_t kMaxBody = 512;
char gBody[kMaxBody + 1];
size_t gBodyLen = 0;
bool gBodyTooBig = false;

bool passwordConfigured() { return settings::hasPanelPassword(); }

bool setupOpen() { return !passwordConfigured() && gWindow && gWindow->isOpen(millis()); }

constexpr const char* kSetupClosedMsg = "Pulsa el botón BOOT de tu SiBlock una vez y vuelve a intentar";
constexpr const char* kBadLengthMsg = "la contraseña debe tener entre 12 y 64 caracteres";

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const char* msg) {
  sendJson(code, String("{\"ok\":false,\"error\":\"") + msg + "\"}");
}

// Anti "DNS rebinding": solo se atiende si el navegador pidió el panel por su IP o su nombre.
// Una página de internet que reapunta su dominio a esta IP manda su propio Host y se rechaza.
bool hostOk() {
  String h = server.hostHeader();
  int c = h.lastIndexOf(':');
  if (c >= 0 && h.indexOf(']') < 0) h = h.substring(0, c);
  h.toLowerCase();
  if (portal::active() && (h == portal::apIpText() || h == WiFi.softAPIP().toString())) return true;
  return h == WiFi.localIP().toString() || h == DEVICE_HOSTNAME || h == DEVICE_HOSTNAME ".local" ||
         h == LEGACY_HOSTNAME || h == LEGACY_HOSTNAME ".local" || (PANEL_EXTRA_HOST[0] && h == PANEL_EXTRA_HOST);
}

bool sameToken(const char* a, const char* b) {  // tiempo constante
  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) diff |= uint8_t(a[i] ^ b[i]);
  return diff == 0 && a[32] == 0 && b[32] == 0;
}

bool validToken() {
  String t = server.header(kTokenHeader);
  if (t.length() != 32) return false;
  for (const Session& s : gSessions)
    if (s.used && sameToken(s.token, t.c_str())) return true;
  return false;
}

const char* newToken() {
  int slot = 0;
  uint32_t now = millis();
  for (int i = 0; i < kSessions; i++) {
    if (!gSessions[i].used) {
      slot = i;
      break;
    }
    if (now - gSessions[i].issuedMs > now - gSessions[slot].issuedMs) slot = i;
  }
  Session& s = gSessions[slot];
  for (int i = 0; i < 4; i++) snprintf(s.token + i * 8, 9, "%08x", unsigned(esp_random()));
  s.issuedMs = now;
  s.used = true;
  return s.token;
}

LockSlot* lockSlotFor(uint32_t ip, bool create) {
  LockSlot* free = nullptr;
  LockSlot* oldest = nullptr;
  uint32_t now = millis();
  for (LockSlot& l : gLocks) {
    if (l.ip == ip) return &l;
    if (!l.ip && !free) free = &l;
    if (!l.locked && (!oldest || now - l.sinceMs > now - oldest->sinceMs)) oldest = &l;
  }
  if (!create) return nullptr;
  // Tabla llena: se pisa la IP sin bloqueo más vieja. Nunca se suelta a una bloqueada.
  LockSlot* s = free ? free : oldest;
  if (!s) return nullptr;
  *s = {ip, 0, 0, false, now};
  return s;
}

uint32_t lockMs(const LockSlot& l) { return kBaseLockMs << (l.level ? l.level - 1 : 0); }

// Vence sesiones y bloqueos. Corre en cada vuelta: así nada depende de comparar millis()
// después de que dé la vuelta (49 días).
void expire() {
  uint32_t now = millis();
  for (Session& s : gSessions)
    if (s.used && now - s.issuedMs >= kSessionMs) s.used = false;
  if (gOverflowBusy && now - gOverflowMs >= kOverflowGapMs) gOverflowBusy = false;
  for (int i = 0; i < 4; i++)
    if (gTrusted[i] && now - gTrustedMs[i] >= kTrustMs) gTrusted[i] = 0;
  for (LockSlot& l : gLocks) {
    if (!l.ip) continue;
    if (l.locked && now - l.sinceMs >= lockMs(l)) {
      l.locked = false;
      l.fails = 0;
      l.sinceMs = now;
    } else if (!l.locked && now - l.sinceMs >= kForgetMs) {
      l = {};
    }
  }
}

// Todo lo privado pasa por aquí. Pide:
//  - el header X-Requested-With: un <form> o <img> de otra página no puede ponerlo (anti-CSRF);
//  - un token de sesión (X-Token) o usuario y contraseña (Basic). No se pide con la ventana
//    del navegador para que no la guarde y la mande sola en peticiones de otras páginas.
bool trusted(uint32_t ip) {
  if (!ip) return false;
  for (uint32_t t : gTrusted)
    if (t == ip) return true;
  return false;
}

void untrust(uint32_t ip) {
  for (uint32_t& t : gTrusted)
    if (t == ip) t = 0;
}

bool allLocked() {
  for (const LockSlot& l : gLocks)
    if (!l.locked) return false;
  return true;
}

// Login Basic: "Basic base64(usuario:contraseña)". Se parte en el PRIMER ':' (el usuario no
// lleva ':'; la contraseña sí puede). El largo se cuenta en bytes.
bool basicAuthOk() {
  String h = server.header("Authorization");
  if (h.length() < 7 || h.length() > 200 || strncmp(h.c_str(), "Basic ", 6) != 0) return false;
  std::string dec;
  if (!siblock::base64Decode(h.c_str() + 6, h.length() - 6, dec)) return false;
  size_t colon = dec.find(':');
  if (colon == std::string::npos || dec.compare(0, colon, kUser) != 0 || colon != strlen(kUser)) return false;
  std::string pw = dec.substr(colon + 1);
  bool ok = settings::checkPanelPassword(String(pw.c_str()));
  std::fill(dec.begin(), dec.end(), '\0');
  std::fill(pw.begin(), pw.end(), '\0');
  return ok;
}

// Bloqueo por IP: ¿puede esta IP probar una contraseña ahora? Si no, ya contestó 429.
bool mayTryPassword(uint32_t ip) {
  LockSlot* l = lockSlotFor(ip, false);
  if (l && l->locked) {
    sendError(429, "demasiados intentos; espera unos minutos");
    return false;
  }
  if (!l && gOverflowBusy && !trusted(ip) && allLocked()) {
    sendError(429, "demasiados intentos en la red; espera unos segundos");
    return false;
  }
  return true;
}

void passwordOk(uint32_t ip) {
  LockSlot* l = lockSlotFor(ip, false);
  if (l) *l = {};
  for (int i = 0; i < 4; i++)
    if (gTrusted[i] == ip) {
      gTrustedMs[i] = millis();  // renueva la hora
      return;
    }
  gTrusted[gTrustedNext] = ip;
  gTrustedMs[gTrustedNext] = millis();
  gTrustedNext = (gTrustedNext + 1) % 4;
}

void passwordBad(uint32_t ip) {
  untrust(ip);
  LockSlot* l = lockSlotFor(ip, true);
  if (!l) {  // todas las casillas bloqueadas: cuenta en el turno compartido
    gOverflowBusy = true;
    gOverflowMs = millis();
    return;
  }
  l->sinceMs = millis();
  if (++l->fails >= kFailsBeforeLock) {
    l->locked = true;
    if (l->level < kMaxLevel + 1) l->level++;
    Serial.printf("[web] %s bloqueada %u s por contraseñas malas\n",
                  IPAddress(ip).toString().c_str(), unsigned(lockMs(*l) / 1000));
  }
}

bool authorize() {
  if (server.header(kCsrfHeader) != kCsrfValue) {
    sendError(403, "petición no permitida");
    return false;
  }
  if (!passwordConfigured()) {
    sendError(403, "primero crea tu contraseña");
    return false;
  }
  if (validToken()) return true;
  if (server.header("Authorization").isEmpty()) {
    sendError(401, "inicia sesión");
    return false;
  }
  uint32_t ip = uint32_t(server.client().remoteIP());
  if (!mayTryPassword(ip)) return false;
  if (basicAuthOk()) {
    passwordOk(ip);
    return true;
  }
  passwordBad(ip);
  sendError(401, "contraseña incorrecta");
  return false;
}

// ---------- Cuerpo de los POST ----------

void onBodyChunk() {
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START || raw.status == RAW_ABORTED) {
    // ABORTED: el cliente se cortó y el handler no va a correr; no dejar estado colgado.
    gBodyLen = 0;
    gBodyTooBig = false;
  } else if (raw.status == RAW_WRITE) {
    size_t n = raw.currentSize;
    if (gBodyLen + n > kMaxBody) {
      gBodyTooBig = true;  // se sigue recibiendo (y tirando) para cerrar bien la conexión
      return;
    }
    memcpy(gBody + gBodyLen, raw.buf, n);
    gBodyLen += n;
  }
  gBody[gBodyLen] = 0;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Lee un campo de un cuerpo x-www-form-urlencoded.
String bodyArg(const char* name) {
  size_t nl = strlen(name);
  const char* p = gBody;
  const char* end = gBody + gBodyLen;
  while (p < end) {
    const char* amp = static_cast<const char*>(memchr(p, '&', end - p));
    const char* stop = amp ? amp : end;
    const char* eq = static_cast<const char*>(memchr(p, '=', stop - p));
    if (eq && size_t(eq - p) == nl && memcmp(p, name, nl) == 0) {
      String v;
      for (const char* q = eq + 1; q < stop; q++) {
        if (*q == '+') {
          v += ' ';
        } else if (*q == '%' && q + 2 < stop && hexVal(q[1]) >= 0 && hexVal(q[2]) >= 0) {
          v += char(hexVal(q[1]) * 16 + hexVal(q[2]));
          q += 2;
        } else {
          v += *q;
        }
      }
      return v;
    }
    p = stop + 1;
  }
  return String();
}

// ¿Viene el campo en el cuerpo (aunque esté vacío)?
bool bodyHas(const char* name) {
  size_t nl = strlen(name);
  const char* p = gBody;
  const char* end = gBody + gBodyLen;
  while (p < end) {
    const char* amp = static_cast<const char*>(memchr(p, '&', end - p));
    const char* stop = amp ? amp : end;
    const char* eq = static_cast<const char*>(memchr(p, '=', stop - p));
    if (eq && size_t(eq - p) == nl && memcmp(p, name, nl) == 0) return true;
    p = stop + 1;
  }
  return false;
}

// Un carácter nulo (crudo o como %00) cortaría el valor al guardarlo (NVS y PBKDF2 usan c_str()):
// se guardaría algo distinto de lo que se revisó. Ningún campo lo necesita, así que se rechaza.
bool bodyHasNul() {
  if (memchr(gBody, 0, gBodyLen)) return true;
  for (size_t i = 0; i + 2 < gBodyLen; i++)
    if (gBody[i] == '%' && gBody[i + 1] == '0' && gBody[i + 2] == '0') return true;
  return false;
}

// Envuelve cada ruta: Host, tope del cuerpo y, si hace falta, sesión.
void redirectToPortal() {
  server.sendHeader("Location", "http://" + portal::apIpText() + "/");
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "");
}

bool fromApClient() { return portal::isApClient(server.client().remoteIP(), server.client().localIP()); }

template <bool kPrivate, void (*kFn)()>
void guarded() {
  // Portal cautivo: el teléfono prueba "¿hay internet?" con otro nombre; se le manda al portal.
  if (!hostOk() && fromApClient()) redirectToPortal();
  else if (!hostOk()) sendError(403, "entra por la IP de SiBlock o por http://" DEVICE_HOSTNAME ".local");
  else if (gBodyTooBig) sendError(413, "petición demasiado grande");
  else if (bodyHasNul()) sendError(400, "la petición trae un carácter no permitido");
  else if (!kPrivate || authorize()) kFn();
  // El cuerpo es de esta petición y de ninguna otra (un multipart no pasa por onBodyChunk).
  gBodyLen = 0;
  gBody[0] = 0;
  gBodyTooBig = false;
}

// Los nombres vienen de la red: se escapan para que no rompan el JSON.
void appendJsonString(String& out, const char* s) {
  out += '"';
  out += adblock::jsonEscape(s).c_str();
  out += '"';
}

const char* verdictName(Verdict v) {
  switch (v) {
    case Verdict::kBlockedList: return "lista";
    case Verdict::kBlockedCustom: return "tuyo";
    case Verdict::kAllowedCustom: return "permitido";
    case Verdict::kPaused: return "pausa";
    case Verdict::kBlockedSchedule: return "horario";
    case Verdict::kPausedDevice: return "pausa-aparato";
    case Verdict::kAllowedCommunity: return "comunidad";
    default: return "ok";
  }
}

#include "portal_page.h"
#include "web_page.h"

void handlePage() {
  server.sendHeader("Content-Security-Policy",
                    "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                    "connect-src 'self'; img-src *.prueba.siblock.internal:*; frame-ancestors 'none'");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("Cache-Control", "no-store");
  // Con el portal abierto, quien llega por la red de SiBlock ve la página para elegir el WiFi.
  server.send_P(200, "text/html; charset=utf-8", fromApClient() ? kPortalPage : kPage);
}

// ---------- Modo de configuración por WiFi ----------
// Solo mientras el portal está abierto, solo desde la red de SiBlock (WPA2 con la contraseña de
// respaldo) y con el header anti-CSRF. Así nadie en la red de la casa puede reapuntar la placa.
bool portalRequestOk() {
  if (!portal::active()) {
    server.send(404, "text/plain", "no existe");
    return false;
  }
  if (server.header(kCsrfHeader) != kCsrfValue) {
    sendError(403, "petición no permitida");
    return false;
  }
  if (!fromApClient()) {
    sendError(403, "esto solo se puede hacer conectado a la red de SiBlock");
    return false;
  }
  return true;
}

// El escaneo deja al teléfono del AP sin servicio unos segundos y frena el loop: como mucho uno
// cada 10 s; entre medias se devuelve el último resultado.
constexpr uint32_t kScanEveryMs = 10000;
String gScanJson;
uint32_t gScanMs = 0;

void handleWifiScan() {
  if (!portalRequestOk()) return;
  if (gScanJson.length() && millis() - gScanMs < kScanEveryMs) return sendJson(200, gScanJson);
  int n = WiFi.scanNetworks();
  String j = "{\"networks\":[";
  int shown = 0;
  for (int i = 0; i < n && shown < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;  // redes ocultas
    if (shown++) j += ',';
    j += "{\"ssid\":";
    appendJsonString(j, ssid.c_str());
    j += ",\"rssi\":" + String(int(WiFi.RSSI(i))) + ",\"open\":" +
         (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  j += "]}";
  gScanJson = j;
  gScanMs = millis();
  sendJson(200, j);
}

void handleWifiSave() {
  if (!portalRequestOk()) return;
  String ssid = bodyArg("ssid"), pass = bodyArg("pass");
  if (!siblock::validWifi(std::string(ssid.c_str(), ssid.length()), std::string(pass.c_str(), pass.length())))
    return sendError(400, "revisa el nombre del WiFi y su contraseña (de 8 a 63 caracteres, o vacía si es abierta)");
  if (!settings::setWifi(ssid, pass)) return sendError(500, "no se pudo guardar; prueba otra vez");
  Serial.printf("[portal] WiFi guardado: \"%s\"\n", ssid.c_str());
  portal::noteWifiSaved(millis());
  sendJson(200, "{\"ok\":true}");
}

// Público: solo contadores. Lo que dice qué visitó cada aparato va en /api/detail.
void handleStats() {
  DnsStats s = gDns->stats();
  String j;
  j.reserve(512);
  j += "{\"version\":\"" SIBLOCK_VERSION "\",\"queries\":" + String(s.queries) + ",\"blocked\":" + String(s.blocked) +
       ",\"forwarded\":" + String(s.forwarded) + ",\"answered\":" + String(s.answered) +
       ",\"dropped\":" + String(s.dropped) + ",\"timeouts\":" + String(s.timeouts) +
       ",\"cached\":" + String(s.cached) + ",\"tcp\":" + String(s.tcp) +
       ",\"rebind\":" + String(s.rebind) + ",\"canary\":" + String(s.canary) +
       ",\"relay\":" + String(s.relay) + ",\"schedule\":" + String(s.schedule) +
       ",\"cacheEntries\":" + String(unsigned(gDns->cacheEntries())) +
       ",\"domains\":" + String(gList->count()) + ",\"uptime\":" + String(millis() / 1000) +
       ",\"heap\":" + String(ESP.getFreeHeap()) +
       ",\"updating\":" + ((gList->updating() || *gUpdateRequested) ? "true" : "false") +
       ",\"paused\":" + String(gList->pausedSecondsLeft()) +
       ",\"skipped\":" + String(gList->lastSkipped()) +
       ",\"passwordSet\":" + (passwordConfigured() ? "true" : "false") +
       ",\"setupOpen\":" + (setupOpen() ? "true" : "false") +
       ",\"portal\":" + (portal::active() ? "true" : "false") +
       ",\"upstream\":\"" + gDns->currentUpstream().toString() + "\",\"error\":";
  appendJsonString(j, gList->lastError().c_str());
  j += "}";
  sendJson(200, j);
}

void handleDetail() {
  String j;
  j.reserve(2560);
  j += "{\"recent\":[";
  RecentQuery rq[20];
  size_t n = gDns->recent(rq, 20);
  for (size_t i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"d\":";
    appendJsonString(j, rq[i].name);
    j += ",\"v\":\"";
    j += verdictName(rq[i].verdict);
    j += "\",\"c\":\"" + IPAddress(rq[i].client).toString() + "\"}";
  }
  j += "],\"clients\":[";
  ClientStat cs[10];
  n = gDns->topClients(cs, 10);
  for (size_t i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"ip\":\"" + IPAddress(cs[i].ip).toString() + "\",\"q\":" + String(cs[i].queries) +
         ",\"b\":" + String(cs[i].blocked) + ",\"ago\":" + String((millis() - cs[i].lastMs) / 1000) +
         "}";
  }
  // Aparatos que se saltan SiBlock (DNS cifrado o Private Relay), solo en RAM.
  j += "],\"bypass\":[";
  siblock::BypassTable::Entry by[siblock::BypassTable::kSlots];
  n = gDns->bypassList(by, siblock::BypassTable::kSlots);
  uint32_t now = millis();
  for (size_t i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"ip\":\"" + IPAddress(by[i].ip).toString() + "\",\"k\":[";
    bool first = true;
    if (by[i].flags & siblock::kBypassDoh) {
      j += "\"doh\"";
      first = false;
    }
    if (by[i].flags & siblock::kBypassRelay) j += first ? "\"relay\"" : ",\"relay\"";
    j += "],\"ago\":" + String((now - by[i].lastMs) / 1000) + "}";
  }
  j += "]}";
  sendJson(200, j);
}

// ---------- "¿Este aparato usa SiBlock?" y "Algo no funciona" ----------

// GIF transparente de 1x1 (43 B). El panel lo carga desde <n>.prueba.siblock.internal: si el
// aparato le pregunta a SiBlock, ese nombre apunta a la placa y la imagen carga.
const uint8_t kProbeGif[43] = {'G', 'I', 'F', '8', '9', 'a', 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x00,
                               0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};

// Única excepción al chequeo de Host (anti-rebinding): esta ruta, con un Host
// "<etiquetas>.prueba.siblock.internal[:puerto]", devuelve siempre el mismo GIF y nada más.
// No lee cookies ni sesión, no cambia nada y no trae datos: una página ajena que la cargue solo
// sabe que hay una SiBlock en el camino (riesgo bajo aceptado, ver docs/seguridad.md).
void handleProbeGif() {
  String host = server.hostHeader();
  if (!siblock::isProbeHost(std::string(host.c_str(), host.length())) && !hostOk()) {
    sendError(403, "no permitido");
    return;
  }
  String body;
  body.reserve(sizeof kProbeGif);
  for (uint8_t b : kProbeGif) body += char(b);  // String::concat copia con largo: el 0x00 pasa
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.send(200, "image/gif", body);
}

// Público (con anti-CSRF): solo habla del aparato que pregunta y de lo que ya dice /api/stats.
// Devuelve códigos fijos; nunca dominios ni IPs.
void handleDiagnose() {
  if (server.header(kCsrfHeader) != kCsrfValue) return sendError(403, "petición no permitida");
  uint32_t ip = uint32_t(server.client().remoteIP());
  String probe = server.arg("probe");
  uint32_t gw = uint32_t(WiFi.gatewayIP());
  siblock::DiagInput in{};
  in.probe = siblock::parseProbe(std::string(probe.c_str(), probe.length()));
  in.clientSeen = gDns->clientSeenWithin(ip, 10UL * 60 * 1000);
  in.bypassFlags = gDns->bypassFlagsFor(ip);
  in.relayBlockOn = gDns->relayBlock();
  in.paused = gList->pausedSecondsLeft() > 0;
  in.listLoaded = gList->count() > 0;
  in.upstreamDown = gDns->upstreamDown();
  in.viaRouter = siblock::routerLooksLikeDns(gw, gw ? gDns->clientQueries(gw) : 0, gDns->totalClientQueries());
  family::diagFor(ip, in.devicePaused, in.scheduleGroups);
  // El User-Agent solo elige los pasos por sistema de esta respuesta: no se guarda ni se devuelve.
  String ua = server.header("User-Agent");
  in.os = siblock::osFromUserAgent(std::string(ua.c_str(), ua.length()));
  String j = "{\"findings\":[";
  bool first = true;
  for (const siblock::DiagFinding& f : siblock::diagnose(in)) {
    if (!first) j += ',';
    first = false;
    j += "{\"code\":\"";
    j += f.code;
    j += "\",\"level\":\"";
    j += siblock::levelName(f.level);
    j += "\"}";
  }
  j += "]}";
  sendJson(200, j);
}

// Con sesión: las horas dicen cuándo hay gente en casa y los dominios, qué se visita.
void handleHistory() {
  uint32_t hour = siblock::epochHour(int64_t(time(nullptr)));
  static siblock::UsageHistory::Slot slots[siblock::UsageHistory::kHours];  // 1,3 KB fuera de la pila
  if (hour) gDns->historySnapshot(hour, slots);
  else memset(slots, 0, sizeof slots);
  siblock::TopBlocked::Entry top[10];
  size_t n = gDns->topBlocked(top, 10);
  // Peor caso de una vez (sin realocar en un heap sin PSRAM): 168 pares de hasta 10 dígitos
  // ("[x,y],", 24 B) + encabezado, y cada dominio del top (cada byte escapado ocupa hasta 6,
  // "\u00XX") con sus comillas y su cuenta.
  size_t need = siblock::UsageHistory::kHours * 24 + 64;
  for (size_t i = 0; i < n; i++) need += strlen(top[i].name) * 6 + 32;
  String j;
  j.reserve(need);
  j += "{\"clock\":";
  j += hour ? "true" : "false";
  // "Cuánto te ahorra": bloqueadas de 7 días × 30 KB (estimado, falta verificar con fuente), en MB.
  j += ",\"ahorroMb\":" + String(unsigned(siblock::savedBytes(slots, siblock::UsageHistory::kHours) / 1048576ULL));
  j += ",\"hour\":" + String(unsigned(hour)) + ",\"h\":[";
  for (size_t i = 0; i < siblock::UsageHistory::kHours; i++) {
    if (i) j += ',';
    j += '[';
    j += String(unsigned(slots[i].queries));
    j += ',';
    j += String(unsigned(slots[i].blocked));
    j += ']';
  }
  j += "],\"top\":[";
  for (size_t i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"d\":";
    appendJsonString(j, top[i].name);
    j += ",\"n\":" + String(unsigned(top[i].count)) + "}";
  }
  j += "]}";
  sendJson(200, j);
}

void handleRulesGet() {
  std::vector<std::string> blocked, allowed;
  gList->rules(blocked, allowed);
  String j = "{\"blocked\":[";
  for (size_t i = 0; i < blocked.size(); i++) {
    if (i) j += ',';
    appendJsonString(j, blocked[i].c_str());
  }
  j += "],\"allowed\":[";
  for (size_t i = 0; i < allowed.size(); i++) {
    if (i) j += ',';
    appendJsonString(j, allowed[i].c_str());
  }
  j += "]}";
  sendJson(200, j);
}

void handleRulesPost() {
  String action = bodyArg("action");
  std::string domain = bodyArg("domain").c_str();
  if (domain.empty() || domain.size() > 253) return sendError(400, "dominio inválido");
  bool ok;
  if (action == "block") ok = gList->addRule(domain, true);
  else if (action == "allow") ok = gList->addRule(domain, false);
  else if (action == "remove") ok = gList->removeRule(domain);
  else return sendError(400, "acción inválida");
  if (!ok) return sendError(400, "dominio inválido, no estaba o ya hay 200 reglas");
  // Una respuesta guardada mientras el dominio estaba permitido (p. ej. con IP de la casa)
  // no debe sobrevivir al cambio de regla.
  gDns->clearCache();
  sendJson(200, "{\"ok\":true}");
}

void handlePause() {
  String m = bodyArg("minutes");
  long minutes = m.toInt();
  if (m.isEmpty() || minutes < 0 || minutes > 1440) return sendError(400, "minutos entre 0 y 1440");
  gList->pause(uint32_t(minutes) * 60);
  sendJson(200, "{\"ok\":true}");
}

// ---------- Aparatos de la familia y horarios ----------

// Entero de un campo del cuerpo: solo dígitos (y un '-' al inicio), entre min y max.
bool argInt(const char* name, long min, long max, long& out) {
  String s = bodyArg(name);
  const char* v = s.c_str();
  size_t len = s.length();
  if (len == 0 || len > 6) return false;
  bool neg = v[0] == '-';
  size_t i = neg ? 1 : 0;
  if (i >= len) return false;
  long x = 0;
  for (; i < len; i++) {
    char c = v[i];
    if (c < '0' || c > '9') return false;
    x = x * 10 + (c - '0');
  }
  if (neg) x = -x;
  if (x < min || x > max) return false;
  out = x;
  return true;
}

bool argDevice(siblock::DeviceId& id) {
  String h = bodyArg("h");
  return siblock::parseDeviceId(h.c_str(), h.length(), id);
}

void appendDeviceRow(String& j, bool& first, int slot, const siblock::DeviceId* id, const char* name, uint32_t ip,
                     const ClientStat* cs, uint32_t me, uint32_t now, uint8_t hint = siblock::kOuiNone) {
  if (!first) j += ',';
  first = false;
  j += "{\"s\":" + String(slot) + ",\"h\":\"";
  if (id) j += siblock::deviceIdHex(*id).c_str();
  j += "\",\"n\":";
  appendJsonString(j, name ? name : "");
  j += ",\"ip\":\"";
  if (ip) j += IPAddress(ip).toString();
  j += "\",\"q\":" + String(cs ? cs->queries : 0) + ",\"b\":" + String(cs ? cs->blocked : 0) +
       ",\"ago\":" + String(cs ? long((now - cs->lastMs) / 1000) : -1L) +
       ",\"pausa\":" + String(slot >= 0 ? family::pauseLeft(slot) : 0);
  if (ip && ip == me) j += ",\"yo\":true";
  // Solo para aparatos sin nombre: la marca por el prefijo de la MAC (una etiqueta, nunca la MAC).
  if (slot < 0 && id) {
    if (hint == siblock::kOuiPrivate) {
      j += ",\"privada\":true";
    } else if (const char* sug = siblock::ouiLabel(hint)) {
      j += ",\"sugerencia\":";
      appendJsonString(j, sug);
    }
  }
  j += '}';
}

// Los aparatos con nombre (aunque no se vean ahora) y los que más consultan (hasta 10), con su
// huella si la tabla ARP la da. Un aparato con nombre y una IP de ahora van en una sola fila. Los
// que no tienen nombre traen la marca sugerida o "privada"; la MAC nunca sale de la placa.
void handleDevicesGet() {
  std::unique_ptr<siblock::Family> fam(new (std::nothrow) siblock::Family());
  if (!fam) return sendError(503, "Intenta otra vez en un momento");
  family::snapshot(*fam);
  ClientStat cs[10];
  size_t n = gDns->topClients(cs, 10);
  siblock::DeviceId ids[10];
  uint8_t hints[10] = {};
  bool has[10] = {}, joined[10] = {};
  for (size_t i = 0; i < n; i++) has[i] = family::idForIp(cs[i].ip, ids[i], &hints[i]);
  uint32_t me = uint32_t(server.client().remoteIP()), now = millis();
  String j;
  j.reserve(2048);
  j += "{\"devices\":[";
  bool first = true;
  for (int slot = 0; slot < int(siblock::kMaxDevices); slot++) {
    if (!fam->used(slot)) continue;
    siblock::DeviceId id = fam->idAt(slot);
    const ClientStat* c = nullptr;
    uint32_t ip = 0;
    for (size_t i = 0; i < n; i++) {
      if (has[i] && !joined[i] && ids[i] == id) {
        joined[i] = true;
        c = &cs[i];
        ip = cs[i].ip;
        break;
      }
    }
    if (!ip) family::seenRecently(id, &ip);
    appendDeviceRow(j, first, slot, &id, fam->nameAt(slot), ip, c, me, now);
  }
  for (size_t i = 0; i < n; i++)
    if (!joined[i]) appendDeviceRow(j, first, -1, has[i] ? &ids[i] : nullptr, nullptr, cs[i].ip, &cs[i], me, now, hints[i]);
  j += "],\"tz\":" + String(int(fam->tz)) + ",\"hora\":" + (family::clockOk() ? "true" : "false") + "}";
  sendJson(200, j);
}

void handleDevicesPost() {
  String action = bodyArg("action");
  if (action != "name" && action != "forget" && action != "pause") return sendError(400, "acción inválida");
  siblock::DeviceId id;
  if (!argDevice(id)) return sendError(400, "no encuentro ese aparato");
  int slot = family::slotOf(id);
  if (action == "name") {
    String name = bodyArg("name");
    if (!siblock::validDeviceName(std::string(name.c_str(), name.length())))
      return sendError(400, "el nombre es muy largo o empieza o termina con espacio: usa uno más corto (los acentos y los emoji ocupan más)");
    // Solo aparatos que existen: con nombre o vistos en los últimos 5 min (no se inventan).
    if (slot < 0 && !family::seenRecently(id))
      return sendError(400, "no encuentro ese aparato; que use internet un momento y vuelve a intentar");
    if (slot < 0 && family::namedCount() >= siblock::kMaxDevices)
      return sendError(400, "ya hay 32 aparatos con nombre; olvida uno primero");
    if (!family::setName(id, name)) return sendError(500, "no se pudo guardar; prueba otra vez");
  } else if (action == "forget") {
    if (slot < 0) return sendError(400, "ese aparato no tiene nombre");
    if (!family::forget(id)) return sendError(500, "no se pudo guardar; prueba otra vez");
  } else {
    long minutes;
    if (!argInt("minutes", 0, 1440, minutes)) return sendError(400, "minutos entre 0 y 1440");
    if (slot < 0) return sendError(400, "primero ponle nombre al aparato");
    if (!family::pause(id, uint32_t(minutes))) return sendError(400, "no se pudo pausar");
  }
  sendJson(200, "{\"ok\":true}");
}

void handleSchedulesGet() {
  std::unique_ptr<siblock::Family> fam(new (std::nothrow) siblock::Family());
  if (!fam) return sendError(503, "Intenta otra vez en un momento");
  family::snapshot(*fam);
  String j;
  j.reserve(96 + fam->scheduleCount() * 64);
  j += "{\"items\":[";
  for (size_t i = 0; i < fam->scheduleCount(); i++) {
    const siblock::Schedule& s = fam->scheduleAt(i);
    char dev[9];
    snprintf(dev, sizeof dev, "%08x", unsigned(s.devices));
    if (i) j += ',';
    j += "{\"i\":" + String(unsigned(i)) + ",\"dev\":\"" + dev + "\",\"g\":" + String(unsigned(s.groups)) +
         ",\"d\":" + String(unsigned(s.days)) + ",\"ini\":" + String(unsigned(s.start)) +
         ",\"fin\":" + String(unsigned(s.end)) + "}";
  }
  j += "],\"tz\":" + String(int(fam->tz)) + "}";
  sendJson(200, j);
}

void handleSchedulesPost() {
  String action = bodyArg("action");
  long i;
  if (action == "delete") {
    if (!argInt("i", 0, long(siblock::kMaxSchedules) - 1, i) || size_t(i) >= family::scheduleCount())
      return sendError(400, "ese horario no existe");
    if (!family::removeSchedule(int(i))) return sendError(500, "no se pudo guardar; prueba otra vez");
    return sendJson(200, "{\"ok\":true}");
  }
  if (action != "save") return sendError(400, "acción inválida");
  long g, d, ini, fin, tz;
  String dev = bodyArg("dev");
  uint8_t raw[4];
  if (!argInt("i", -1, long(siblock::kMaxSchedules) - 1, i) ||
      (i >= 0 && size_t(i) >= family::scheduleCount()))
    return sendError(400, "ese horario no existe");
  if (i < 0 && family::scheduleCount() >= siblock::kMaxSchedules)
    return sendError(400, "ya hay 8 horarios; borra uno primero");
  if (dev.length() != 8 || !siblock::fromHex(std::string(dev.c_str(), dev.length()), raw, 4))
    return sendError(400, "elige al menos un aparato");
  if (!argInt("tz", -720, 840, tz)) return sendError(400, "la hora de tu aparato no es válida");
  if (!argInt("g", 1, 7, g)) return sendError(400, "elige qué bloquear");
  if (!argInt("d", 1, 127, d)) return sendError(400, "elige al menos un día");
  if (!argInt("ini", 0, 1439, ini) || !argInt("fin", 0, 1439, fin) || ini == fin)
    return sendError(400, "la hora de inicio y la de fin deben ser distintas");
  uint32_t devices = uint32_t(raw[0]) << 24 | uint32_t(raw[1]) << 16 | uint32_t(raw[2]) << 8 | raw[3];
  if (!devices || (devices & ~family::usedMask())) return sendError(400, "elige aparatos que tengan nombre");
  siblock::Schedule s{devices, uint8_t(g), uint8_t(d), uint16_t(ini), uint16_t(fin)};
  if (!family::setSchedule(int(i), s, int16_t(tz))) return sendError(500, "no se pudo guardar; prueba otra vez");
  sendJson(200, "{\"ok\":true}");
}

// ---------- Respaldo y restauración ----------
// El respaldo es un archivo de texto (lib/siblock_core/backup.h) con reglas, listas, Private Relay,
// aparatos y horarios. No lleva WiFi, contraseñas, número de instalación ni IPs.
// Se sube en pedazos de hasta 3 KB (el servidor no acepta cuerpos grandes) a /restore.tmp, se
// revisa entero y se aplica todo junto. El número de pedazo va en el header X-Parte (en modo raw
// el servidor no lee los argumentos de la URL).

constexpr const char* kRestoreTmp = "/restore.tmp";
constexpr const char* kPartHeader = "X-Parte";
constexpr size_t kPartMax = 3072;
uint32_t gRestoreBytes = 0;  // bytes ya guardados en /restore.tmp
int gRestoreNext = -1;       // pedazo que sigue (-1 = no hay archivo a medias)
struct PartState {
  bool authorized, writing, tooBig, failed, badSeq;
  int n;
  size_t got;
};
PartState gPart = {};
fs::File gPartFile;

void dropRestore() {
  if (gPartFile) gPartFile.close();
  fsu::remove(kRestoreTmp);
  gRestoreBytes = 0;
  gRestoreNext = -1;
}

void handleBackupGet() {
  std::unique_ptr<siblock::Family> fam(new (std::nothrow) siblock::Family());
  if (!fam) return sendError(503, "Intenta otra vez en un momento");
  family::snapshot(*fam);
  std::vector<std::string> block, allow;
  gList->rules(block, allow);
  std::string text = siblock::writeBackup(settings::lists(), gDns->relayBlock(), block, allow, *fam);
  block.clear();
  block.shrink_to_fit();
  allow.clear();
  allow.shrink_to_fit();
  // El lector no acepta más de kBackupMaxBytes: mejor avisar que entregar un archivo que después
  // no se puede restaurar (pasa con muchas reglas de nombres muy largos).
  if (text.size() > siblock::kBackupMaxBytes)
    return sendError(500, "el respaldo quedó demasiado grande; borra algunas reglas propias y prueba otra vez");
  // Fecha de la placa en la hora de la casa; sin hora de internet, sin fecha.
  char name[48] = "siblock-respaldo.txt";
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    time_t local = now + time_t(fam->tz) * 60;
    struct tm t;
    gmtime_r(&local, &t);
    snprintf(name, sizeof name, "siblock-respaldo-%04d-%02d-%02d.txt", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  }
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  // send_P manda el texto tal cual, sin copiarlo a un String (sin PSRAM, 24 KB pesan).
  server.send_P(200, "text/plain; charset=utf-8", text.c_str());
}

// Cuerpo de /api/restore/part. Se revisa todo en RAW_START: sin sesión, sin el header anti-CSRF o
// con el número de pedazo que no toca, no se escribe nada en la flash.
void onRestoreChunk() {
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START) {
    if (gPartFile) gPartFile.close();
    gPart = {};
    gPart.authorized = hostOk() && server.header(kCsrfHeader) == kCsrfValue && passwordConfigured() && validToken();
    if (!gPart.authorized) return;
    String v = server.header(kPartHeader);
    const char* c = v.c_str();
    bool digits = v.length() >= 1 && v.length() <= 2;
    for (size_t i = 0; digits && i < v.length(); i++) digits = c[i] >= '0' && c[i] <= '9';
    int n = digits ? atoi(c) : -1;
    if (n < 0 || (n != 0 && n != gRestoreNext)) {
      gPart.badSeq = true;
      return;
    }
    if (n == 0) dropRestore();
    gPart.n = n;
    gPartFile = LittleFS.open(kRestoreTmp, n == 0 ? FILE_WRITE : FILE_APPEND);
    gPart.writing = bool(gPartFile);
    gPart.failed = !gPart.writing;
  } else if (raw.status == RAW_WRITE) {
    if (!gPart.writing) return;
    size_t n = raw.currentSize;
    if (gPart.got + n > kPartMax || gRestoreBytes + gPart.got + n > siblock::kBackupMaxBytes) {
      gPart.tooBig = true;
      gPart.writing = false;
      dropRestore();
      return;
    }
    if (gPartFile.write(raw.buf, n) != n) {
      gPart.failed = true;
      gPart.writing = false;
      dropRestore();
      return;
    }
    gPart.got += n;
  } else {
    if (gPartFile) gPartFile.close();
    if (raw.status == RAW_ABORTED && gPart.authorized) {
      gPart.writing = false;
      dropRestore();
    }
  }
}

void handleRestorePart() {
  if (gPart.tooBig) return sendError(413, "el respaldo es demasiado grande");
  if (!gPart.authorized) return sendError(401, "inicia sesión otra vez");
  if (gPart.badSeq) return sendError(400, "falta un pedazo; sube el archivo otra vez");
  if (gPart.failed || !gPart.writing) {
    dropRestore();
    return sendError(500, "no se pudo guardar; prueba otra vez");
  }
  gRestoreBytes += gPart.got;
  gRestoreNext = gPart.n + 1;
  gPart = {};
  sendJson(200, "{\"ok\":true}");
}

// Lee /restore.tmp entero con el lector del respaldo. Si falla, ya contestó.
std::unique_ptr<siblock::BackupReader> readRestore() {
  if (gRestoreNext <= 0) {
    sendError(400, "primero sube el archivo");
    return nullptr;
  }
  std::unique_ptr<siblock::BackupReader> r(new (std::nothrow) siblock::BackupReader(true));
  if (!r) {
    sendError(503, "Intenta otra vez en un momento");
    return nullptr;
  }
  bool ok = family::readFile(kRestoreTmp, *r) && r->finish();
  if (!ok) {
    String j = "{\"ok\":false,\"error\":";
    appendJsonString(j, *r->error() ? r->error() : "no se pudo leer el archivo");
    j += ",\"linea\":" + String(r->errorLine()) + "}";
    sendJson(400, j);
    return nullptr;
  }
  return r;
}

void handleRestoreCheck() {
  auto r = readRestore();
  if (!r) return;
  const siblock::Backup& b = r->result();
  sendJson(200, "{\"ok\":true,\"reglas\":" + String(unsigned(b.block.size() + b.allow.size())) +
                    ",\"aparatos\":" + String(unsigned(__builtin_popcount(b.family.usedMask()))) +
                    ",\"horarios\":" + String(unsigned(b.family.scheduleCount())) +
                    ",\"listas\":" + String(unsigned(b.lists)) + "}");
}

void handleRestoreApply() {
  if (bodyArg("confirm") != "1") return sendError(400, "falta confirmar");
  auto r = readRestore();
  if (!r) return;
  if (ESP.getMaxAllocHeap() < 40 * 1024) return sendError(503, "Intenta otra vez en un momento");
  const siblock::Backup& b = r->result();
  // Ya se revisó entero: lo que falle ahora es la flash. Primero las reglas (todo o nada).
  if (!gList->replaceRules(b.block, b.allow)) return sendError(500, "no se pudo guardar; prueba otra vez");
  if (!family::replace(b.family)) return sendError(500, "se guardaron las reglas, pero no los aparatos; prueba otra vez");
  bool listsChanged = b.lists != settings::lists();
  bool ok = (!listsChanged || settings::setLists(b.lists)) && settings::setRelayBlock(b.relay);
  gDns->setRelayBlock(settings::relayBlock());
  dropRestore();
  gDns->clearCache();
  if (listsChanged) *gUpdateRequested = true;
  Serial.println("[web] respaldo restaurado");
  if (!ok) return sendError(500, "se guardó casi todo, pero no las opciones; revísalas en el panel");
  sendJson(200, "{\"ok\":true}");
}

// Opciones del panel: "Apagar iCloud Private Relay en esta casa" y las listas elegidas.
void handleOptionsGet() {
  sendJson(200, String("{\"relayBlock\":") + (gDns->relayBlock() ? "true" : "false") +
                    ",\"lists\":" + String(unsigned(settings::lists())) +
                    ",\"listsOffer\":" + String(unsigned(siblock::listsOffered())) +
                    ",\"skipped\":" + String(gList->lastSkipped()) + "}");
}

// Cada campo que venga debe ser válido; si alguno no lo es, no se cambia nada.
void handleOptionsPost() {
  bool hasRelay = bodyHas("relayBlock"), hasLists = bodyHas("lists");
  String v = bodyArg("relayBlock");
  long lists = 0;
  if (!hasRelay && !hasLists) return sendError(400, "valor inválido");
  if (hasRelay && v != "0" && v != "1") return sendError(400, "valor inválido");
  if (hasLists && (!argInt("lists", 1, 7, lists) || !siblock::validListMask(uint32_t(lists)) ||
                   (uint32_t(lists) & ~uint32_t(siblock::listsOffered()))))
    return sendError(400, "esa combinación de listas no existe");
  if (hasLists && uint8_t(lists) != settings::lists()) {
    if (!settings::setLists(uint8_t(lists))) return sendError(500, "no se pudo guardar; prueba otra vez");
    *gUpdateRequested = true;  // se baja otra vez con las listas nuevas
  }
  if (hasRelay) {
    bool on = v == "1";
    if (!settings::setRelayBlock(on)) return sendError(500, "no se pudo guardar; prueba otra vez");
    gDns->setRelayBlock(on);
  }
  // Misma respuesta que antes de la fase 5 (docs/api.md): las listas se leen con GET.
  sendJson(200, String("{\"ok\":true,\"relayBlock\":") + (gDns->relayBlock() ? "true" : "false") + "}");
}

// ---------- Nube: "Compartir con SiBlock" (todo apagado de fábrica) ----------

void sendCloudStatus() {
  const cloud::Status& st = cloud::status();
  String id = settings::installId();
  String j;
  j.reserve(200);
  j += "{\"share\":";
  j += st.share ? "true" : "false";
  j += ",\"versionCheck\":";
  j += st.versionCheck ? "true" : "false";
  // installId y latest solo tienen [0-9a-z.]: ya se validaron al crearlos o leerlos.
  j += ",\"installId\":\"" + id + "\",\"current\":\"" SIBLOCK_VERSION "\",\"latest\":\"";
  j += st.latest.c_str();
  j += "\",\"update\":";
  j += siblock::isNewer(st.latest, SIBLOCK_VERSION) ? "true" : "false";
  j += ",\"lastPing\":\"";
  j += cloud::pingName(st.lastPing);
  community::Info ci = community::info();
  j += "\",\"community\":";
  j += st.community ? "true" : "false";
  j += ",\"communityCount\":" + String(unsigned(ci.count));
  j += ",\"communityUpdated\":" + String(unsigned(ci.updated));
  j += ",\"filtro\":";
  j += st.filtered ? "true" : "false";
  j += "}";
  sendJson(200, j);
}

void handleCloudGet() { sendCloudStatus(); }

// Cada campo que venga debe ser 0 o 1; si alguno no lo es, no se cambia nada.
void handleCloudPost() {
  bool hasShare = bodyHas("share"), hasVer = bodyHas("versionCheck"), hasCom = bodyHas("community");
  String share = bodyArg("share"), ver = bodyArg("versionCheck"), com = bodyArg("community");
  auto isBit = [](const String& v) { return v == "0" || v == "1"; };
  if ((!hasShare && !hasVer && !hasCom) || (hasShare && !isBit(share)) || (hasVer && !isBit(ver)) ||
      (hasCom && !isBit(com)))
    return sendError(400, "datos");
  cloud::Status& st = cloud::status();
  const bool oldVer = st.versionCheck, oldCom = st.community;
  // El 500 dice "no se cambió nada": si una falla, se deshacen las que ya se habían cambiado.
  auto undo = [&](bool ver) {
    if (ver && hasVer && st.versionCheck != oldVer && settings::setVersionCheck(oldVer)) st.versionCheck = oldVer;
    if (hasCom && st.community != oldCom && settings::setCommunity(oldCom)) st.community = oldCom;
  };
  if (hasCom) {
    bool on = com == "1";
    if (!settings::setCommunity(on)) return sendError(500, "guardar");
    st.community = on;
  }
  // "Compartir" va al final: si algo falla antes, no cambia sin que la persona lo sepa.
  if (hasVer) {
    bool on = ver == "1";
    if (!settings::setVersionCheck(on)) {
      undo(false);
      return sendError(500, "guardar");
    }
    st.versionCheck = on;
  }
  if (hasShare) {
    bool on = share == "1";
    if (!settings::setCloudShare(on)) {
      undo(true);
      return sendError(500, "guardar");
    }
    st.share = on;
  }
  if (!st.versionCheck) st.latest.clear();  // apagado no se avisa de nada
  // Apagada, los permitidos de la comunidad se borran de la RAM y de la flash.
  if (hasCom && !st.community) community::clear();
  sendCloudStatus();
}

// ---------- Avisos por Telegram (apagados de fábrica) ----------
// El token solo se escribe: ninguna respuesta lo trae (solo "tokenSaved"), no se imprime y no va
// en el respaldo. El envío no se hace aquí: el panel lo pide y loop() lo manda (watchTelegram).

siblock::Cooldown gTestGate;  // un mensaje de prueba cada 30 s

void sendTelegramStatus() {
  const telegram::Status& st = telegram::status();
  uint8_t ev = settings::telegramEvents();
  String j;
  j.reserve(220);
  j += "{\"on\":";
  j += settings::telegramOn() ? "true" : "false";
  j += ",\"tokenSaved\":";
  j += settings::hasTelegramToken() ? "true" : "false";
  j += ",\"chat\":";
  appendJsonString(j, settings::telegramChat().c_str());  // solo [-0-9]: se validó al guardarlo
  j += ",\"reboot\":";
  j += (ev & settings::kTgReboot) ? "true" : "false";
  j += ",\"listFail\":";
  j += (ev & settings::kTgListFail) ? "true" : "false";
  j += ",\"weekly\":";
  j += (ev & settings::kTgWeekly) ? "true" : "false";
  j += ",\"last\":\"";
  j += siblock::telegramResultName(st.last);
  j += "\",\"lastCode\":" + String(st.lastCode);
  j += ",\"lastAt\":" + String(unsigned(st.lastAt));
  j += ",\"pending\":";
  j += (st.pending & telegram::kTest) ? "true" : "false";
  j += "}";
  sendJson(200, j);
}

void handleTelegramGet() { sendTelegramStatus(); }

// Campos (todos opcionales, al menos uno): token, chat, on, reboot, listFail, weekly (0/1) y
// clearToken=1. Primero se revisa todo; si algo no vale, no se cambia nada.
// Borra el texto al salir (por cualquier return): así el token no queda en la RAM.
struct WipeOnExit {
  String& s;
  ~WipeOnExit() {
    if (s.length()) memset(const_cast<char*>(s.c_str()), 0, s.length());
  }
};

void handleTelegramPost() {
  // Campos de 0/1: -1 = no vino, 0, 1; 2 = vino con otro valor.
  static const char* const kBits[] = {"on", "reboot", "listFail", "weekly", "clearToken"};
  int8_t bits[5];
  bool any = false, bad = false;
  for (int i = 0; i < 5; i++) {
    bits[i] = -1;
    if (!bodyHas(kBits[i])) continue;
    any = true;
    String v = bodyArg(kBits[i]);
    bits[i] = v == "1" ? 1 : v == "0" ? 0 : 2;
    bad = bad || bits[i] == 2;
  }
  bool hasToken = bodyHas("token"), hasChat = bodyHas("chat");
  String token = hasToken ? bodyArg("token") : String();
  WipeOnExit wipeToken{token};
  token.trim();  // en su lugar, sin otra copia
  String chat = hasChat ? bodyArg("chat") : String();
  chat.trim();
  // El cuerpo traía el token: se borra de la RAM del panel ya mismo.
  memset(gBody, 0, sizeof gBody);
  gBodyLen = 0;
  if (bad || (!any && !hasToken && !hasChat)) return sendError(400, "datos");
  bool clear = bits[4] == 1;
  if (hasToken && (clear || !siblock::validTelegramToken(std::string(token.c_str(), token.length()))))
    return sendError(400, "token");
  if (hasChat && !siblock::validTelegramChatId(std::string(chat.c_str(), chat.length()))) return sendError(400, "chat");
  // Encender pide token y chat (los que vienen o los guardados).
  bool wantOn = bits[0] >= 0 ? bits[0] == 1 : settings::telegramOn();
  if (clear) wantOn = false;
  if (wantOn && !hasToken && !settings::hasTelegramToken()) return sendError(400, "falta_token");
  if (wantOn && !hasChat && settings::telegramChat().isEmpty()) return sendError(400, "falta_chat");
  uint8_t ev = settings::telegramEvents();
  const uint8_t kEv[3] = {settings::kTgReboot, settings::kTgListFail, settings::kTgWeekly};
  for (int i = 0; i < 3; i++)
    if (bits[i + 1] >= 0) ev = bits[i + 1] ? uint8_t(ev | kEv[i]) : uint8_t(ev & ~kEv[i]);
  // Apagar va primero y encender al final: si algo falla a medias, no queda encendido sin querer.
  if (!wantOn && settings::telegramOn() && !settings::setTelegramOn(false)) return sendError(500, "guardar");
  if (clear) settings::clearTelegramToken();
  if (hasToken && !settings::setTelegramToken(token)) return sendError(500, "guardar");
  if (hasChat && !settings::setTelegramChat(chat)) return sendError(500, "guardar");
  if (ev != settings::telegramEvents() && !settings::setTelegramEvents(ev)) return sendError(500, "guardar");
  if (wantOn && !settings::telegramOn() && !settings::setTelegramOn(true)) return sendError(500, "guardar");
  // Apagados, los avisos en espera no salen (watchTelegram los suelta). Sin token, ni la prueba.
  if (clear) {
    telegram::status().pending = 0;
    Serial.println("[telegram] token borrado");
  }
  sendTelegramStatus();
}

// Pide un mensaje de prueba. 202: loop() lo manda en un momento; el resultado sale en GET.
void handleTelegramTest() {
  if (!settings::hasTelegramToken() || settings::telegramChat().isEmpty()) return sendError(409, "falta");
  if (!gTestGate.take(millis(), 30000)) return sendError(429, "espera");
  telegram::status().pending |= telegram::kTest;
  sendJson(202, "{\"ok\":true}");
}

// ---------- Reportes desde el panel (solo con "compartir" encendido) ----------
// Solo opciones fijas: lo que se manda es siempre un cuerpo armado aquí con valores de las listas
// del contrato (o un dominio con [a-z0-9.-]); nunca texto que escribió la persona tal cual.

bool gReportBusy = false;  // un reporte a la vez

std::string trimLower(const String& v) {
  std::string s(v.c_str(), v.length());
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

void sendReport(const char* path, const std::string& body) {
  if (body.empty()) return sendError(400, "datos");
  if (gReportBusy) return sendError(429, "ocupado");
  gReportBusy = true;
  cloud::Result r = cloud::postJson(path, body);
  gReportBusy = false;
  switch (r) {
    case cloud::Result::kOk: return sendJson(200, "{\"ok\":true}");
    case cloud::Result::kLimit: return sendError(502, "limite");
    case cloud::Result::kRejected: return sendError(502, "rechazado");
    default: return sendError(502, "sin_internet");
  }
}

void handleReportModem() {
  if (!cloud::status().share) return sendError(409, "apagado");
  String fab = bodyArg("fabricante"), dnsOk = bodyArg("dnsOk"), menu = bodyArg("menu");
  if (dnsOk != "0" && dnsOk != "1") return sendError(400, "datos");
  sendReport("/api/v1/reporte/modem",
             siblock::modemReportJson(settings::installId().c_str(), std::string(fab.c_str(), fab.length()),
                                      dnsOk == "1", std::string(menu.c_str(), menu.length())));
}

void handleReportBlocked() {
  if (!cloud::status().share) return sendError(409, "apagado");
  String motivo = bodyArg("motivo");
  sendReport("/api/v1/reporte/bloqueo",
             siblock::blockedReportJson(settings::installId().c_str(), trimLower(bodyArg("domain")),
                                        std::string(motivo.c_str(), motivo.length())));
}

void handleUpdate() {
  *gUpdateRequested = true;
  sendJson(202, "{\"ok\":true}");
}


void clearSessions() {
  for (Session& s : gSessions) s = {};
}

void sendToken() {
  String j = "{\"ok\":true,\"token\":\"";
  j += newToken();
  j += "\"}";
  sendJson(200, j);
}

// Primer uso: crear la contraseña. Público (no hay contraseña todavía), pero solo dentro de la
// ventana de 30 min tras el arranque o tras pulsar BOOT: así nadie más en la red se adelanta.
void handleSetup() {
  if (server.header(kCsrfHeader) != kCsrfValue) return sendError(403, "petición no permitida");
  if (passwordConfigured()) return sendError(409, "ya hay contraseña");
  if (!setupOpen()) return sendError(403, kSetupClosedMsg);
  String pw = bodyArg("password");
  if (!siblock::validPanelPassword(std::string(pw.c_str(), pw.length()))) return sendError(400, kBadLengthMsg);
  if (!settings::setPanelPassword(pw)) return sendError(500, "no se pudo guardar; prueba otra vez");
  Serial.println("[web] contraseña del panel creada");
  clearSessions();
  sendToken();
}

// Cambiar la contraseña: pide la actual (cuenta para el bloqueo por IP) y cierra todas las sesiones.
void handlePassword() {
  uint32_t ip = uint32_t(server.client().remoteIP());
  if (!mayTryPassword(ip)) return;
  if (!settings::checkPanelPassword(bodyArg("current"))) {
    passwordBad(ip);
    return sendError(401, "la contraseña actual no es correcta");
  }
  passwordOk(ip);
  String pw = bodyArg("new");
  if (!siblock::validPanelPassword(std::string(pw.c_str(), pw.length()))) return sendError(400, kBadLengthMsg);
  if (!settings::setPanelPassword(pw)) return sendError(500, "no se pudo guardar; prueba otra vez");
  Serial.println("[web] contraseña del panel cambiada; sesiones cerradas");
  clearSessions();
  sendToken();
}

void handleLogin() { sendToken(); }  // la contraseña (Basic) se cambia por un token de sesión

void handleBackupAp() {
  String j = "{\"ssid\":";
  appendJsonString(j, settings::apName().c_str());
  j += ",\"pass\":";
  appendJsonString(j, settings::apPassword().c_str());
  j += "}";
  sendJson(200, j);
}

void handleLogout() {
  String t = server.header(kTokenHeader);
  for (Session& s : gSessions)
    if (s.used && t.length() == 32 && sameToken(s.token, t.c_str())) s.used = false;
  sendJson(200, "{\"ok\":true}");
}
}  // namespace

namespace web {
void begin(BlockList* list, DnsServer* dns, volatile bool* updateRequested, siblock::SetupWindow* window) {
  gWindow = window;
  gList = list;
  gDns = dns;
  gUpdateRequested = updateRequested;
  static const char* headers[] = {kCsrfHeader, "Authorization", kTokenHeader, kPartHeader, "User-Agent"};
  server.collectHeaders(headers, 5);
  fsu::remove(kRestoreTmp);  // un respaldo a medias de antes de reiniciar no vale
  server.on("/", HTTP_GET, guarded<false, handlePage>);
  server.on("/api/stats", HTTP_GET, guarded<false, handleStats>);
  server.on("/api/detail", HTTP_GET, guarded<true, handleDetail>);
  server.on("/api/rules", HTTP_GET, guarded<true, handleRulesGet>);
  // Los POST reciben el cuerpo por onBodyChunk (con tope) en vez de dejar que WebServer lo
  // copie entero a la RAM.
  server.on("/api/rules", HTTP_POST, guarded<true, handleRulesPost>, onBodyChunk);
  server.on("/api/pause", HTTP_POST, guarded<true, handlePause>, onBodyChunk);
  server.on("/api/update", HTTP_POST, guarded<true, handleUpdate>, onBodyChunk);
  server.on("/api/login", HTTP_POST, guarded<true, handleLogin>, onBodyChunk);
  server.on("/api/logout", HTTP_POST, guarded<true, handleLogout>, onBodyChunk);
  server.on("/api/setup", HTTP_POST, guarded<false, handleSetup>, onBodyChunk);
  server.on("/api/password", HTTP_POST, guarded<true, handlePassword>, onBodyChunk);
  server.on("/api/backup-ap", HTTP_GET, guarded<true, handleBackupAp>);
  server.on("/api/options", HTTP_GET, guarded<true, handleOptionsGet>);
  server.on("/api/diagnose", HTTP_GET, guarded<false, handleDiagnose>);
  server.on("/api/history", HTTP_GET, guarded<true, handleHistory>);
  server.on("/api/devices", HTTP_GET, guarded<true, handleDevicesGet>);
  server.on("/api/devices", HTTP_POST, guarded<true, handleDevicesPost>, onBodyChunk);
  server.on("/api/schedules", HTTP_GET, guarded<true, handleSchedulesGet>);
  server.on("/api/schedules", HTTP_POST, guarded<true, handleSchedulesPost>, onBodyChunk);
  server.on("/api/backup", HTTP_GET, guarded<true, handleBackupGet>);
  server.on("/api/restore/part", HTTP_POST, guarded<true, handleRestorePart>, onRestoreChunk);
  server.on("/api/restore/check", HTTP_POST, guarded<true, handleRestoreCheck>, onBodyChunk);
  server.on("/api/restore/apply", HTTP_POST, guarded<true, handleRestoreApply>, onBodyChunk);
  // Sin guarded: tiene su propio chequeo de Host (ver handleProbeGif).
  server.on("/siblock-prueba.gif", HTTP_GET, handleProbeGif);
  server.on("/api/options", HTTP_POST, guarded<true, handleOptionsPost>, onBodyChunk);
  server.on("/api/cloud", HTTP_GET, guarded<true, handleCloudGet>);
  server.on("/api/cloud", HTTP_POST, guarded<true, handleCloudPost>, onBodyChunk);
  server.on("/api/telegram", HTTP_GET, guarded<true, handleTelegramGet>);
  server.on("/api/telegram", HTTP_POST, guarded<true, handleTelegramPost>, onBodyChunk);
  server.on("/api/telegram/test", HTTP_POST, guarded<true, handleTelegramTest>, onBodyChunk);
  server.on("/api/report/modem", HTTP_POST, guarded<true, handleReportModem>, onBodyChunk);
  server.on("/api/report/blocked", HTTP_POST, guarded<true, handleReportBlocked>, onBodyChunk);
  server.on("/api/wifi/scan", HTTP_GET, guarded<false, handleWifiScan>);
  server.on("/api/wifi", HTTP_POST, guarded<false, handleWifiSave>, onBodyChunk);
  server.onNotFound([] {
    // Portal cautivo: las pruebas de "¿hay internet?" del teléfono (/generate_204,
    // /hotspot-detect.html...) llevan al portal para que se abra solo.
    if (fromApClient()) return redirectToPortal();
    server.send(404, "text/plain", "no existe");
  });
  server.begin();
  if (!passwordConfigured())
    Serial.println("[web] sin contraseña del panel: créala desde el panel en los primeros 30 min");
}

void loop() {
  expire();
  server.handleClient();
  if (gScanJson.length() && !portal::active()) gScanJson = String();  // portal cerrado: RAM libre
}
}  // namespace web

#include "cloud.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "certs.h"
#include "config.h"

namespace {
constexpr uint16_t kTimeoutMs = 8000;
constexpr size_t kMaxVersionBody = 1024;
// TLS pide ~40 KB entre los dos búferes y el apretón de manos: sin un bloque seguido de este
// tamaño ni se intenta (la descarga de la lista tiene el mismo tope).
constexpr uint32_t kMinFreeBlock = 32 * 1024;

bool canConnect() { return WiFi.status() == WL_CONNECTED && ESP.getMaxAllocHeap() >= kMinFreeBlock; }

void setup(HTTPClient& http) {
  http.useHTTP10(true);  // sin "chunked": el cuerpo llega limpio
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  http.setReuse(false);
  // Un 3xx no se sigue: la placa solo habla con siblock.s-dev.win.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setUserAgent("SiBlock/" SIBLOCK_VERSION);
}
}  // namespace

namespace cloud {

Status& status() {
  static Status s;
  return s;
}

const char* pingName(Ping p) {
  switch (p) {
    case Ping::kOk: return "ok";
    case Ping::kFail: return "fail";
    default: return "never";
  }
}

Result postJson(const char* path, const std::string& body) {
  if (!canConnect() || body.empty()) return Result::kNoNet;
  WiFiClientSecure client;
  client.setCACert(ROOT_CAS);
  HTTPClient http;
  setup(http);
  if (!http.begin(client, String(SIBLOCK_CLOUD_BASE) + path)) return Result::kNoNet;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(String(body.c_str()));
  http.end();
  if (code <= 0) return Result::kNoNet;
  if (code >= 200 && code < 300) return Result::kOk;
  if (code == 429) return Result::kLimit;
  return Result::kRejected;
}

bool fetchLatestVersion(std::string& out) {
  if (!canConnect()) return false;
  WiFiClientSecure client;
  client.setCACert(ROOT_CAS);
  HTTPClient http;
  setup(http);
  if (!http.begin(client, SIBLOCK_CLOUD_BASE "/version.json")) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  int size = http.getSize();  // -1 si no dice el tamaño
  if (size > int(kMaxVersionBody)) {
    http.end();
    return false;
  }
  char buf[kMaxVersionBody + 1];  // uno más para saber si se pasó del tope
  size_t n = 0;
  WiFiClient* s = http.getStreamPtr();
  uint32_t start = millis();
  while (s && n <= kMaxVersionBody && millis() - start < kTimeoutMs) {
    if (size >= 0 && n >= size_t(size)) break;
    int avail = s->available();
    if (avail > 0) {
      // Solo lo que ya llegó: readBytes espera (1 s por byte) si se le pide de más.
      size_t want = sizeof buf - n;
      if (size_t(avail) < want) want = size_t(avail);
      int r = int(s->readBytes(reinterpret_cast<uint8_t*>(buf + n), want));
      if (r > 0) n += size_t(r);
      else break;
    } else if (!http.connected()) {
      break;
    } else {
      delay(5);
    }
  }
  http.end();
  if (n > kMaxVersionBody) return false;
  return siblock::parseVersionJson(buf, n, out);
}

}  // namespace cloud

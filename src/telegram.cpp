#include "telegram.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstring>

#include "certs.h"
#include "config.h"
#include "settings.h"

namespace {
constexpr uint16_t kTimeoutMs = 8000;
constexpr uint32_t kMinFreeBlock = 32 * 1024;  // como en cloud.cpp: TLS pide ~40 KB

// Borra un texto con el token antes de soltar su memoria.
void wipe(String& s) {
  if (s.length()) memset(const_cast<char*>(s.c_str()), 0, s.length());
  s = String();
}
}  // namespace

namespace telegram {

Status& status() {
  static Status s;
  return s;
}

int send(const std::string& text) {
  if (WiFi.status() != WL_CONNECTED || ESP.getMaxAllocHeap() < kMinFreeBlock) return -1;
  String token = settings::telegramToken(), chat = settings::telegramChat();
  std::string body = siblock::telegramForm(std::string(chat.c_str(), chat.length()), text);
  if (body.empty() || !siblock::validTelegramToken(std::string(token.c_str(), token.length()))) {
    wipe(token);
    return 0;
  }
  String url;
  url.reserve(sizeof(SIBLOCK_TELEGRAM_BASE) + token.length() + 20);
  url = SIBLOCK_TELEGRAM_BASE "/bot";
  url += token;
  url += "/sendMessage";
  wipe(token);
  int code = -1;
  {
    WiFiClientSecure client;
    client.setCACert(ROOT_CAS);
    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(kTimeoutMs);
    http.setConnectTimeout(kTimeoutMs);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent("SiBlock/" SIBLOCK_VERSION);
    if (http.begin(client, url)) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      code = http.POST(String(body.c_str()));  // la respuesta (lo que dice Telegram) no se lee
      http.end();
    }
  }
  wipe(url);
  return code;
}

siblock::BootCause bootCause(bool updated) {
  if (updated) return siblock::BootCause::kUpdate;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return siblock::BootCause::kPower;
    case ESP_RST_BROWNOUT: return siblock::BootCause::kBrownout;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return siblock::BootCause::kCrash;
    default: return siblock::BootCause::kNormal;  // ESP.restart(), botón EN y lo demás
  }
}

}  // namespace telegram

#include "notify.h"

#include "family.h"

namespace siblock {

namespace {
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isTokenChar(char c) {
  return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

// "152 340 sitios" / "1 sitio".
std::string sites(uint32_t n) { return groupThousands(n) + (n == 1 ? " sitio" : " sitios"); }

std::string listPart(uint32_t listCount) {
  return listCount ? "Lista: " + sites(listCount) + "." : "Todavía no tiene su lista.";
}
}  // namespace

bool validTelegramToken(const std::string& s) {
  size_t colon = s.find(':');
  if (colon == std::string::npos || colon < 5 || colon > 12) return false;
  for (size_t i = 0; i < colon; i++)
    if (!isDigit(s[i])) return false;
  size_t rest = s.size() - colon - 1;
  if (rest < 30 || rest > 64) return false;
  for (size_t i = colon + 1; i < s.size(); i++)
    if (!isTokenChar(s[i])) return false;
  return true;
}

bool validTelegramChatId(const std::string& s) {
  size_t start = !s.empty() && s[0] == '-' ? 1 : 0;
  size_t n = s.size() - start;
  if (n < 1 || n > 20) return false;
  for (size_t i = start; i < s.size(); i++)
    if (!isDigit(s[i])) return false;
  return true;
}

std::string groupThousands(uint32_t n) {
  std::string digits = std::to_string(n), out;
  for (size_t i = 0; i < digits.size(); i++) {
    if (i && (digits.size() - i) % 3 == 0) out += "\xC2\xA0";
    out += digits[i];
  }
  return out;
}

std::string bootMessage(BootCause c, uint32_t listCount) {
  const char* why = "reinicio normal";
  switch (c) {
    case BootCause::kPower: why = "corte de luz o la desconectaron"; break;
    case BootCause::kCrash: why = "tuvo una falla y se recuperó sola"; break;
    case BootCause::kBrownout: why = "bajó la corriente"; break;
    case BootCause::kUpdate: why = "actualización"; break;
    default: break;
  }
  return std::string("SiBlock se reinició (") + why + "). " + listPart(listCount);
}

std::string listFailMessage(uint32_t hoursFailing, uint32_t listCount) {
  std::string s = "SiBlock no pudo actualizar la lista";
  uint32_t days = hoursFailing / 24;
  if (days) s += " desde hace " + std::to_string(days) + (days == 1 ? " día" : " días");
  if (listCount) return s + ". Sigue usando la que tiene (" + sites(listCount) + ").";
  return s + " y todavía no tiene ninguna: no bloquea nada.";
}

std::string listOkMessage(uint32_t listCount) {
  return "SiBlock ya pudo actualizar la lista: " + sites(listCount) + ".";
}

std::string weeklyMessage(uint32_t blocked) {
  if (!blocked) return "Resumen de la semana: no hubo anuncios ni rastreadores que bloquear.";
  return "Resumen de la semana: " + groupThousands(blocked) +
         (blocked == 1 ? " anuncio o rastreador bloqueado." : " anuncios y rastreadores bloqueados.");
}

std::string testMessage() { return "SiBlock: mensaje de prueba. Los avisos van a llegar a este chat."; }

std::string formEncode(const std::string& s) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                 c == '_' || c == '.' || c == '~';
    if (plain) {
      out += char(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  return out;
}

std::string telegramForm(const std::string& chatId, const std::string& text) {
  if (!validTelegramChatId(chatId) || text.empty()) return "";
  return "chat_id=" + chatId + "&text=" + formEncode(text);
}

int32_t weeklySummaryDay(int64_t epochSeconds, int16_t tzMinutes) {
  int32_t mow = localMinuteOfWeek(epochSeconds, tzMinutes);
  if (mow < 6 * 1440 + 20 * 60) return -1;  // domingo 20:00 (lunes 00:00 = 0); -1 = sin hora
  return int32_t((epochSeconds + int64_t(tzMinutes) * 60) / 86400);
}

bool WeeklyOnce::take(int32_t day, uint32_t stored) {
  if (day <= 0 || uint32_t(day) == stored || uint32_t(day) == last_) return false;
  last_ = uint32_t(day);
  return true;
}

TelegramResult telegramResultFor(int httpCode) {
  if (httpCode == kTgLowMem) return TelegramResult::kLowMem;
  if (httpCode == kTgNoConfig) return TelegramResult::kNoConfig;
  if (httpCode <= 0) return TelegramResult::kNoNet;
  if (httpCode >= 200 && httpCode < 300) return TelegramResult::kOk;
  if (httpCode == 401 || httpCode == 404) return TelegramResult::kToken;
  if (httpCode == 400) return TelegramResult::kChat;
  if (httpCode == 403) return TelegramResult::kBlocked;
  if (httpCode == 429) return TelegramResult::kLimit;
  return TelegramResult::kRejected;
}

const char* telegramResultName(TelegramResult r) {
  switch (r) {
    case TelegramResult::kOk: return "ok";
    case TelegramResult::kToken: return "token";
    case TelegramResult::kChat: return "chat";
    case TelegramResult::kBlocked: return "bloqueado";
    case TelegramResult::kLimit: return "limite";
    case TelegramResult::kNoNet: return "sin_internet";
    case TelegramResult::kRejected: return "rechazado";
    case TelegramResult::kLowMem: return "poca_ram";
    case TelegramResult::kNoConfig: return "sin_datos";
    default: return "never";
  }
}

bool telegramTransient(TelegramResult r) { return r == TelegramResult::kNoNet || r == TelegramResult::kLowMem; }

bool TelegramRetry::waiting(uint32_t nowMs) const { return wait_ && nowMs - at_ < kGapMs; }

bool TelegramRetry::failed(uint8_t bit, uint32_t nowMs) {
  if (bit != bit_) {  // otro aviso: su propia cuenta
    bit_ = bit;
    tries_ = 0;
  }
  wait_ = true;
  at_ = nowMs;
  if (tries_ >= kMax) {
    tries_ = 0;
    bit_ = 0;
    return false;
  }
  tries_++;
  return true;
}

void TelegramRetry::done() {
  bit_ = 0;
  tries_ = 0;
  wait_ = false;
}

bool SendLimiter::allow(uint32_t nowMs) {
  // Lleno: la casilla que toca pisar es la más vieja; sirve si ya salió de la ventana.
  if (used_ == kMax && nowMs - at_[next_] < kWindowMs) return false;
  at_[next_] = nowMs;
  next_ = uint8_t((next_ + 1) % kMax);
  if (used_ < kMax) used_++;
  return true;
}

bool Cooldown::take(uint32_t nowMs, uint32_t gapMs) {
  if (used_ && nowMs - last_ < gapMs) return false;
  used_ = true;
  last_ = nowMs;
  return true;
}

ListFailWatch::Action ListFailWatch::onResult(bool ok, uint32_t nowMs, bool enabled, bool scheduled) {
  if (!ok && !scheduled) return kNone;  // una falla a mano no cuenta
  if (ok) {
    Action a = noticed_ && enabled ? kRecovered : kNone;
    fails_ = 0;
    noticed_ = false;
    return a;
  }
  if (!fails_) since_ = nowMs;
  if (fails_ < 255) fails_++;
  if (fails_ < kMinFails || !enabled) return kNone;
  if (noticed_ && nowMs - lastNotice_ < kRepeatMs) return kNone;
  noticed_ = true;
  lastNotice_ = nowMs;
  return kFail;
}

uint32_t ListFailWatch::failingHours(uint32_t nowMs) const { return fails_ ? (nowMs - since_) / 3600000 : 0; }

}  // namespace siblock

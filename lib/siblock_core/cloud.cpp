#include "cloud.h"

#include <cstring>

namespace siblock {

namespace {
bool isLowerHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool validVersionOrDev(const std::string& v) {
  uint32_t p[3];
  return v == "dev" || parseSemver(v, p);
}
}  // namespace

const char* const kFabricantes[] = {"tp-link", "huawei",  "zte",      "technicolor", "arris",
                                    "netgear", "asus",    "mikrotik", "proveedor",   "no-se"};
const size_t kFabricantesCount = sizeof kFabricantes / sizeof kFabricantes[0];
const char* const kMenus[] = {"internet_wan", "red_local", "avanzado", "otro"};
const size_t kMenusCount = sizeof kMenus / sizeof kMenus[0];
const char* const kMotivos[] = {"pagina_no_abre", "app_no_funciona", "juego_no_funciona", "otro"};
const size_t kMotivosCount = sizeof kMotivos / sizeof kMotivos[0];

bool validInstallId(const std::string& s) {
  if (s.size() != 32) return false;
  for (char c : s)
    if (!isLowerHex(c)) return false;
  return true;
}

std::string makeInstallId(const uint8_t rnd[16]) {
  static const char kHex[] = "0123456789abcdef";
  std::string out(32, '0');
  for (int i = 0; i < 16; i++) {
    out[2 * i] = kHex[rnd[i] >> 4];
    out[2 * i + 1] = kHex[rnd[i] & 0xF];
  }
  return out;
}

bool parseSemver(const std::string& s, uint32_t out[3]) {
  if (s.size() < 6 || s[0] != 'v') return false;
  size_t i = 1;
  for (int part = 0; part < 3; part++) {
    size_t start = i;
    uint32_t v = 0;
    while (i < s.size() && isDigit(s[i]) && i - start < 5) v = v * 10 + uint32_t(s[i++] - '0');
    size_t n = i - start;
    if (n == 0 || n > 4 || (n > 1 && s[start] == '0')) return false;
    out[part] = v;
    if (part < 2) {
      if (i >= s.size() || s[i] != '.') return false;
      i++;
    }
  }
  return i == s.size();
}

bool isNewer(const std::string& latest, const std::string& current) {
  uint32_t a[3], b[3];
  if (!parseSemver(latest, a) || !parseSemver(current, b)) return false;
  for (int i = 0; i < 3; i++)
    if (a[i] != b[i]) return a[i] > b[i];
  return false;
}

bool parseVersionJson(const char* body, size_t len, std::string& version) {
  if (!body || len > 1024) return false;
  static const char kKey[] = "\"version\"";
  const size_t kl = sizeof kKey - 1;
  for (size_t i = 0; i + kl <= len; i++) {
    if (memcmp(body + i, kKey, kl) != 0) continue;
    size_t j = i + kl;
    while (j < len && isSpace(body[j])) j++;
    if (j >= len || body[j] != ':') continue;  // era un valor ("url":"version"), no la clave
    j++;
    while (j < len && isSpace(body[j])) j++;
    if (j >= len || body[j] != '"') return false;
    size_t start = ++j;
    while (j < len && body[j] != '"' && j - start <= 16) j++;
    if (j >= len || body[j] != '"') return false;
    std::string v(body + start, j - start);
    uint32_t p[3];
    if (!parseSemver(v, p)) return false;
    version = v;
    return true;
  }
  return false;
}

bool validReportDomain(const std::string& d) {
  if (d.size() < 3 || d.size() > 253) return false;
  size_t labels = 0, labelLen = 0;
  char prev = '.';
  for (char c : d) {
    if (c == '.') {
      if (labelLen == 0 || prev == '-') return false;
      labels++;
      labelLen = 0;
    } else {
      bool ok = (c >= 'a' && c <= 'z') || isDigit(c) || c == '-';
      if (!ok || (c == '-' && labelLen == 0) || ++labelLen > 63) return false;
    }
    prev = c;
  }
  if (labelLen == 0 || prev == '-') return false;
  return labels + 1 >= 2;
}

bool inList(const char* const* list, size_t n, const std::string& v) {
  for (size_t i = 0; i < n; i++)
    if (v == list[i]) return true;
  return false;
}

std::string pingJson(const std::string& id, const std::string& version, uint32_t blocked) {
  if (!validInstallId(id) || !validVersionOrDev(version) || blocked > kMaxBlockedPerDay) return "";
  return "{\"i\":\"" + id + "\",\"v\":\"" + version + "\",\"b\":" + std::to_string(blocked) + "}";
}

std::string modemReportJson(const std::string& id, const std::string& fabricante, bool dnsOk,
                            const std::string& menu) {
  if (!validInstallId(id) || !inList(kFabricantes, kFabricantesCount, fabricante) ||
      !inList(kMenus, kMenusCount, menu))
    return "";
  return "{\"i\":\"" + id + "\",\"origen\":\"panel\",\"fabricante\":\"" + fabricante +
         "\",\"dns_ok\":" + (dnsOk ? "true" : "false") + ",\"menu\":\"" + menu + "\"}";
}

std::string blockedReportJson(const std::string& id, const std::string& domain, const std::string& motivo) {
  if (!validInstallId(id) || !validReportDomain(domain) || !inList(kMotivos, kMotivosCount, motivo)) return "";
  return "{\"i\":\"" + id + "\",\"origen\":\"panel\",\"dominio\":\"" + domain + "\",\"motivo\":\"" + motivo + "\"}";
}

uint32_t sumBlocked(const UsageHistory::Slot slots[UsageHistory::kHours], size_t hours) {
  if (hours > UsageHistory::kHours) hours = UsageHistory::kHours;
  uint64_t sum = 0;
  for (size_t i = UsageHistory::kHours - hours; i < UsageHistory::kHours; i++) sum += slots[i].blocked;
  return sum > UINT32_MAX ? UINT32_MAX : uint32_t(sum);
}

uint32_t blockedLastHours(const UsageHistory& h, uint32_t nowHour, size_t hours) {
  if (!nowHour) return 0;
  static UsageHistory::Slot slots[UsageHistory::kHours];  // 1,3 KB fuera de la pila
  h.snapshot(nowHour, slots);
  return sumBlocked(slots, hours);
}

void CloudSchedule::start(uint32_t nowMs) {
  base_ = nowMs;
  next_ = kFirstMs;
  fails_ = 0;
}

bool CloudSchedule::due(uint32_t nowMs) const { return next_ != 0 && nowMs - base_ >= next_; }

void CloudSchedule::done(bool ok, uint32_t nowMs, uint32_t jitterMs) {
  base_ = nowMs;
  if (ok) {
    fails_ = 0;
    next_ = kEveryMs + jitterMs % kRetryMinMs;
    return;
  }
  uint32_t wait = fails_ >= 5 ? kEveryMs : kRetryMinMs << fails_;  // 1, 2, 4, 8, 16 h; luego 24 h
  next_ = wait > kEveryMs ? kEveryMs : wait;
  if (fails_ < 255) fails_++;
}

}  // namespace siblock

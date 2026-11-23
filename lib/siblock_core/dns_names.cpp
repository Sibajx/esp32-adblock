#include "dns_names.h"

#include <cstring>

namespace siblock {

const char* const kDohNames[] = {
    "dns.google",           "cloudflare-dns.com",       "one.one.one.one",
    "dns.quad9.net",        "dns9.quad9.net",           "dns10.quad9.net",
    "dns11.quad9.net",      "doh.opendns.com",          "doh.familyshield.opendns.com",
    "dns.nextdns.io",       "dns.adguard-dns.com",      "dns.adguard.com",
    "doh.cleanbrowsing.org", "dns.mullvad.net",         "doh.mullvad.net",
};
const size_t kDohNamesCount = sizeof(kDohNames) / sizeof(kDohNames[0]);

// true si name == base o name termina en "." + base.
static bool isOrUnder(const std::string& name, const char* base) {
  size_t bl = strlen(base);
  if (name.size() == bl) return name == base;
  if (name.size() < bl + 1) return false;
  size_t off = name.size() - bl;
  return name[off - 1] == '.' && name.compare(off, bl, base) == 0;
}

SpecialName classifySpecial(const std::string& name) {
  if (isOrUnder(name, kProbeSuffix)) return SpecialName::kProbe;
  if (name == "use-application-dns.net") return SpecialName::kCanary;
  if (name == "mask.icloud.com" || name == "mask-h2.icloud.com") return SpecialName::kRelay;
  // DDR (RFC 9462 §4): SiBlock no tiene resolvedores cifrados que anunciar. Nunca se reenvía: el
  // DNS de arriba (1.1.1.1, 8.8.8.8) anunciaría los suyos y el aparato se saltaría SiBlock.
  if (isOrUnder(name, "resolver.arpa")) return SpecialName::kDdr;
  return SpecialName::kNone;
}

uint8_t bypassFlagsFor(const std::string& name) {
  if (name.empty()) return 0;
  uint8_t f = 0;
  for (size_t i = 0; i < kDohNamesCount; i++) {
    if (isOrUnder(name, kDohNames[i])) {
      f |= kBypassDoh;
      break;
    }
  }
  if (classifySpecial(name) == SpecialName::kRelay) f |= kBypassRelay;
  return f;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

bool isProbeHost(const std::string& host) {
  // Separa el puerto (si hay).
  size_t end = host.size();
  size_t colon = host.find(':');
  if (colon != std::string::npos) {
    size_t digits = host.size() - colon - 1;
    if (digits < 1 || digits > 5) return false;
    uint32_t port = 0;
    for (size_t i = colon + 1; i < host.size(); i++) {
      char c = host[i];
      if (c < '0' || c > '9') return false;
      port = port * 10 + uint32_t(c - '0');
    }
    if (port > 65535) return false;
    end = colon;
  }
  // Nombre en minúsculas, debe terminar en "." + kProbeSuffix con al menos una etiqueta antes.
  std::string name;
  name.reserve(end);
  for (size_t i = 0; i < end; i++) name.push_back(lower(host[i]));
  const size_t sl = sizeof(kProbeSuffix) - 1;
  if (name.size() < sl + 2) return false;
  size_t off = name.size() - sl;
  if (name[off - 1] != '.' || name.compare(off, sl, kProbeSuffix) != 0) return false;
  // Etiquetas antes del sufijo: 1..63 caracteres de [a-z0-9-].
  size_t labelLen = 0;
  for (size_t i = 0; i < off; i++) {
    char c = name[i];
    if (c == '.') {
      if (labelLen == 0) return false;
      labelLen = 0;
      continue;
    }
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok || ++labelLen > 63) return false;
  }
  return true;  // el último carácter antes del sufijo es '.', y la etiqueta previa ya se validó
}

}  // namespace siblock

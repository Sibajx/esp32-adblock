#include "siblock_text.h"

#include <cstdio>
#include <cstring>

namespace siblock {

bool base64Decode(const char* in, size_t len, std::string& out) {
  out.clear();
  if (len % 4) return false;
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  out.reserve(len / 4 * 3);
  for (size_t i = 0; i < len; i += 4) {
    bool last = i + 4 == len;
    int v[4];
    int pad = 0;
    for (int j = 0; j < 4; j++) {
      char c = in[i + j];
      if (c == '=') {
        // El relleno solo va al final, en las dos últimas posiciones y sin nada después.
        if (!last || j < 2) return false;
        pad++;
        v[j] = 0;
      } else {
        if (pad) return false;
        v[j] = val(c);
        if (v[j] < 0) return false;
      }
    }
    uint32_t n = uint32_t(v[0]) << 18 | uint32_t(v[1]) << 12 | uint32_t(v[2]) << 6 | uint32_t(v[3]);
    out += char(n >> 16);
    if (pad < 2) out += char(n >> 8 & 0xFF);
    if (pad < 1) out += char(n & 0xFF);
  }
  return true;
}

static bool hasZero(const std::string& s) { return s.find('\0') != std::string::npos; }

bool validPanelPassword(const std::string& pw) {
  return pw.size() >= 12 && pw.size() <= 64 && !hasZero(pw) && pw != "CAMBIA_ESTA_CLAVE";
}

bool validWifi(const std::string& ssid, const std::string& pass) {
  if (ssid.empty() || ssid.size() > 32 || hasZero(ssid) || hasZero(pass)) return false;
  return pass.empty() || (pass.size() >= 8 && pass.size() <= 63);
}

std::string makeApPassword(uint32_t (*rnd)()) {
  constexpr unsigned kN = sizeof(kApAlphabet) - 1;  // 31
  std::string p;
  while (p.size() < 10) {
    unsigned b = rnd() & 0xFF;
    if (b >= kN * 8) continue;  // 248..255: se descartan (muestreo con rechazo)
    p += kApAlphabet[b % kN];
  }
  return p;
}

std::string apSsid(const uint8_t mac[6]) {
  char s[16];
  snprintf(s, sizeof s, "SiBlock-%02X%02X", mac[4], mac[5]);
  return s;
}
}  // namespace siblock

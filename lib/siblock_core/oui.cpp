#include "oui.h"

#include "oui_table.h"

namespace siblock {

static_assert(oui_data::kBrandCount < kOuiPrivate, "demasiadas marcas para un byte");

namespace {
uint32_t prefixAt(size_t i) {
  const uint8_t* p = oui_data::kPrefixes + i * 3;
  return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
}

// Búsqueda binaria dentro del bloque [lo, hi) de una marca.
bool inBlock(size_t lo, size_t hi, uint32_t want) {
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    uint32_t v = prefixAt(mid);
    if (v == want) return true;
    if (v < want)
      lo = mid + 1;
    else
      hi = mid;
  }
  return false;
}
}  // namespace

bool macIsPrivate(const uint8_t mac[6]) { return (mac[0] & 0x03) != 0; }

uint8_t ouiCode(const uint8_t mac[6]) {
  if (macIsPrivate(mac)) return kOuiPrivate;
  uint32_t want = uint32_t(mac[0]) << 16 | uint32_t(mac[1]) << 8 | mac[2];
  for (size_t b = 0; b < oui_data::kBrandCount; b++)
    if (inBlock(oui_data::kStart[b], oui_data::kStart[b + 1], want)) return uint8_t(b + 1);
  return kOuiNone;
}

const char* ouiLabel(uint8_t code) {
  if (code == kOuiNone || code == kOuiPrivate || code > oui_data::kBrandCount) return nullptr;
  return oui_data::kLabels[code - 1];
}

const char* ouiBrand(const uint8_t mac[6]) { return ouiLabel(ouiCode(mac)); }

}  // namespace siblock

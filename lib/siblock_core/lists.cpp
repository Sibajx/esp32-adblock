#include "lists.h"

namespace siblock {

namespace {
// General: StevenBlack (~78k) + HaGeZi Light (~93k) = ~161k dominios únicos (medido el 2026-11-21).
// Ojo: el tope MAX_DOMAINS (180 000) cuenta los repetidos entre listas (~171k sumando las dos).
const char* const kGeneral[] = {
    "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts",
    "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/light-onlydomains.txt",
};
// Estafas: HaGeZi "Fake" (tiendas falsas, estafas). Medido el 2026-11-21: 10 824 dominios, 190 KB.
// La del plan (HaGeZi TIF mini, wildcard/tif.mini-onlydomains.txt) trae 244 986 dominios: no cabe
// junto a la general en MAX_DOMAINS (180 000) y, como va primero, dejaría fuera toda la general.
// Falta verificar (URL, tamaño, licencia, y si alcanza para "virus": esta no trae malware).
// Falta verificar en la placa: con estafas prendida se suman ~182k contando repetidos, unos 2k más
// que el tope. Se quedan fuera los últimos de HaGeZi Light (anuncios) y el panel lo avisa (skipped).
const char* const kScam = "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/fake-onlydomains.txt";
// Español: EasyList Spanish. Solo cuentan sus líneas "||dominio^" (parseListLine descarta las
// demás). Medido el 2026-11-21: 2 727 líneas, 23 de ellas "||dominio^".
// Falta verificar (URL, tamaño, licencia).
const char* const kSpanish = "https://easylist-downloads.adblockplus.org/easylistspanish.txt";
}  // namespace

bool validListMask(uint32_t mask) { return (mask & kListGeneral) && mask <= 7; }

uint8_t listsOffered() {
  uint8_t m = kListGeneral;
  if (kScam) m |= kListScam;
  if (kSpanish) m |= kListSpanish;
  return m;
}

size_t listUrls(uint8_t mask, const char** out, size_t max) {
  if (!validListMask(mask)) mask = kListGeneral;
  size_t n = 0;
  auto put = [&](const char* u) {
    if (u && n < max) out[n++] = u;
  };
  // Estafas primero y luego español (muy chica): si se llega al tope, lo que queda fuera es el final
  // de la general (anuncios), nunca estafas ni la lista en español entera.
  if (mask & kListScam) put(kScam);
  if (mask & kListSpanish) put(kSpanish);
  for (const char* u : kGeneral) put(u);
  return n;
}

bool acceptListSize(uint32_t total, uint32_t current, bool sizeUnknown, uint32_t rejectedTotal, bool allowShrink) {
  if (allowShrink) return true;
  // Si la misma lista chica llega dos veces seguidas (±1 %), es a propósito y se acepta.
  bool sameAsRejected =
      rejectedTotal && total + total / 100 >= rejectedTotal && total <= rejectedTotal + rejectedTotal / 100;
  // Sin tamaño (Content-Length) no se puede saber si se cortó: se pide el 90 % en vez de la mitad.
  uint32_t minTotal = sizeUnknown ? current - current / 10 : current / 2;
  return !(current > 1000 && total < minTotal && !sameAsRejected);
}

}  // namespace siblock

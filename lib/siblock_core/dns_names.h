// SiBlock: nombres DNS especiales (nombre de prueba, canario de Firefox, iCloud Private Relay,
// resolver.arpa de DDR) y nombres conocidos de DNS cifrado. Sin dependencias de Arduino.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace siblock {

constexpr const char kProbeSuffix[] = "prueba.siblock.internal";

enum class SpecialName : uint8_t { kNone, kProbe, kCanary, kRelay, kDdr };

// name: ya en minúsculas, sin punto final (como DnsQuestion::name).
// kProbe: igual a kProbeSuffix o termina en "." + kProbeSuffix. kCanary: "use-application-dns.net".
// kRelay: "mask.icloud.com" o "mask-h2.icloud.com". kDdr: "resolver.arpa" o cualquier nombre
// debajo (como "_dns.resolver.arpa", RFC 9462). Todo exacto (sin subdominios) salvo kProbe y kDdr.
SpecialName classifySpecial(const std::string& name);

// IP que contesta el nombre de prueba: la de la red de la casa; si todavía no hay (0, solo modo
// AP), la del AP. Si las dos son 0 devuelve 0 y quien llama no contesta con una dirección.
inline uint32_t probeAnswerIp(uint32_t staIp, uint32_t apIp) { return staIp ? staIp : apIp; }

enum BypassFlag : uint8_t { kBypassDoh = 1, kBypassRelay = 2 };

// kBypassDoh si name es o cuelga de un nombre de kDohNames; kBypassRelay si classifySpecial == kRelay.
uint8_t bypassFlagsFor(const std::string& name);

extern const char* const kDohNames[];
extern const size_t kDohNamesCount;

// Header Host: "<etiquetas>.prueba.siblock.internal" con ":puerto" opcional (1..5 dígitos,
// <= 65535), sin distinguir mayúsculas. Cada etiqueta 1..63 de [a-z0-9-]. Nada más vale.
bool isProbeHost(const std::string& host);

}  // namespace siblock

// SiBlock: base64, validaciones y nombres. Sin dependencias de Arduino.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace siblock {
bool base64Decode(const char* in, size_t len, std::string& out);  // false si es inválido
bool validPanelPassword(const std::string& pw);   // 12..64 bytes, sin byte 0 y != "CAMBIA_ESTA_CLAVE"
bool validWifi(const std::string& ssid, const std::string& pass);  // ssid 1..32 B; pass 0 u 8..63 B
constexpr const char kApAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";  // 31, sin i l o 0 1
// 10 caracteres de kApAlphabet. Usa el byte bajo de cada rnd() y descarta los >= 248 (31 x 8)
// para que todas las letras salgan igual de seguido.
std::string makeApPassword(uint32_t (*rnd)());
std::string apSsid(const uint8_t mac[6]);  // "SiBlock-" + hex en mayúsculas de mac[4], mac[5]
}  // namespace siblock

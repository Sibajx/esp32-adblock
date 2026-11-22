// SiBlock: SHA-256, PBKDF2-HMAC-SHA256 y utilidades de bytes. Sin dependencias de Arduino.
// En la placa (ESP_PLATFORM) pbkdf2Sha256 usa mbedtls; en la PC y el simulador, la copia portátil.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace siblock {
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void pbkdf2Sha256(const uint8_t* pass, size_t passLen, const uint8_t* salt, size_t saltLen,
                  uint32_t iterations, uint8_t out[32]);
bool equal32(const uint8_t a[32], const uint8_t b[32]);  // tiempo constante
bool pbkdf2SelfTest();                                   // vector RFC 7914 (c = 1)
std::string toHex(const uint8_t* p, size_t n);
bool fromHex(const std::string& s, uint8_t* out, size_t n);  // false si el largo o un dígito está mal
}  // namespace siblock

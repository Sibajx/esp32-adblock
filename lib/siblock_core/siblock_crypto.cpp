#include "siblock_crypto.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#endif

namespace siblock {
namespace {

// ---------- SHA-256 (FIPS 180-4) ----------
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t buf[64];
  size_t bufLen = 0;
  uint64_t total = 0;

  void block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  void update(const uint8_t* p, size_t n) {
    total += n;
    while (n) {
      size_t take = 64 - bufLen < n ? 64 - bufLen : n;
      memcpy(buf + bufLen, p, take);
      bufLen += take;
      p += take;
      n -= take;
      if (bufLen == 64) {
        block(buf);
        bufLen = 0;
      }
    }
  }

  void finish(uint8_t out[32]) {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (bufLen != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - 8 * i));
    update(len, 8);
    for (int i = 0; i < 8; i++)
      for (int j = 0; j < 4; j++) out[i * 4 + j] = uint8_t(h[i] >> (24 - 8 * j));
  }
};

#ifndef ESP_PLATFORM
// HMAC-SHA256 (RFC 2104). Claridad antes que velocidad: solo corre en la PC y el simulador.
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* a, size_t aLen, const uint8_t* b,
                size_t bLen, uint8_t out[32]) {
  uint8_t k[64] = {0};
  if (keyLen > 64) sha256(key, keyLen, k);
  else if (keyLen) memcpy(k, key, keyLen);
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  uint8_t inner[32];
  Sha256 s;
  s.update(ipad, 64);
  if (aLen) s.update(a, aLen);
  if (bLen) s.update(b, bLen);
  s.finish(inner);
  Sha256 o;
  o.update(opad, 64);
  o.update(inner, 32);
  o.finish(out);
}
#endif

}  // namespace

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  Sha256 s;
  if (len) s.update(data, len);
  s.finish(out);
}

void pbkdf2Sha256(const uint8_t* pass, size_t passLen, const uint8_t* salt, size_t saltLen,
                  uint32_t iterations, uint8_t out[32]) {
#ifdef ESP_PLATFORM
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  int rc = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  if (rc == 0) rc = mbedtls_pkcs5_pbkdf2_hmac(&ctx, pass, passLen, salt, saltLen, iterations, 32, out);
  mbedtls_md_free(&ctx);
  if (rc != 0) memset(out, 0, 32);  // nunca coincide con un hash guardado (la autoprueba lo nota)
#else
  // Un solo bloque (32 B = largo de SHA-256): T1 = U1 ^ U2 ^ ... con U1 = HMAC(P, S || 00000001).
  const uint8_t one[4] = {0, 0, 0, 1};
  uint8_t saltBlock[64 + 4];
  uint8_t u[32];
  if (saltLen <= 64) {
    memcpy(saltBlock, salt, saltLen);
    memcpy(saltBlock + saltLen, one, 4);
    hmacSha256(pass, passLen, saltBlock, saltLen + 4, nullptr, 0, u);
  } else {
    hmacSha256(pass, passLen, salt, saltLen, one, 4, u);
  }
  memcpy(out, u, 32);
  for (uint32_t i = 1; i < iterations; i++) {
    hmacSha256(pass, passLen, u, 32, nullptr, 0, u);
    for (int j = 0; j < 32; j++) out[j] ^= u[j];
  }
#endif
}

bool equal32(const uint8_t a[32], const uint8_t b[32]) {
  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) diff |= uint8_t(a[i] ^ b[i]);
  return diff == 0;
}

bool pbkdf2SelfTest() {
  // RFC 7914, sección 11: PBKDF2-HMAC-SHA256 (P="passwd", S="salt", c=1), primeros 32 bytes.
  static const uint8_t kWant[32] = {0x55, 0xac, 0x04, 0x6e, 0x56, 0xe3, 0x08, 0x9f, 0xec, 0x16, 0x91,
                                    0xc2, 0x25, 0x44, 0xb6, 0x05, 0xf9, 0x41, 0x85, 0x21, 0x6d, 0xde,
                                    0x04, 0x65, 0xe6, 0x8b, 0x9d, 0x57, 0xc2, 0x0d, 0xac, 0xbc};
  uint8_t got[32];
  pbkdf2Sha256(reinterpret_cast<const uint8_t*>("passwd"), 6, reinterpret_cast<const uint8_t*>("salt"), 4,
               1, got);
  return equal32(got, kWant);
}

std::string toHex(const uint8_t* p, size_t n) {
  static const char kDigits[] = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    s += kDigits[p[i] >> 4];
    s += kDigits[p[i] & 15];
  }
  return s;
}

bool fromHex(const std::string& s, uint8_t* out, size_t n) {
  if (s.size() != n * 2) return false;
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < n; i++) {
    int hi = val(s[i * 2]), lo = val(s[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = uint8_t(hi << 4 | lo);
  }
  return true;
}
}  // namespace siblock

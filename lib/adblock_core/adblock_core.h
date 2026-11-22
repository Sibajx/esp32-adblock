// Lógica pura del adblock: sin dependencias de Arduino para poder probarla en la PC.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace adblock {

// ---------- Dominios ----------

// Hash FNV-1a de 64 bits. Se aplica a dominios ya en minúsculas.
uint64_t hashDomain(const char* s, size_t len);
inline uint64_t hashDomain(const std::string& s) { return hashDomain(s.data(), s.size()); }

// Convierte a minúsculas, quita el punto final y valida caracteres.
// Devuelve false si no parece un dominio (vacío, sin punto, caracteres raros, >253).
bool normalizeDomain(std::string& d);

// Extrae el dominio de una línea de lista. Acepta:
//   "0.0.0.0 ads.example.com", "127.0.0.1 ads.example.com", "ads.example.com",
//   "||ads.example.com^"  (formato adblock simple). Ignora comentarios y basura.
// Devuelve false si la línea no aporta un dominio bloqueable.
bool parseListLine(const char* line, size_t len, std::string& out);

// Recorre el dominio y sus padres: "a.b.ads.com" -> "a.b.ads.com", "b.ads.com", "ads.com".
// No incluye el TLD solo ("com"). Llama a fn(ptr, len) y se detiene si devuelve true.
template <typename Fn>
bool forEachSuffix(const std::string& d, Fn fn) {
  size_t pos = 0;
  while (true) {
    size_t dot = d.find('.', pos);
    if (dot == std::string::npos) return false;  // lo que queda es el TLD: no se revisa
    if (fn(d.data() + pos, d.size() - pos)) return true;
    pos = dot + 1;
  }
}

// ---------- Lista ordenada + índice ----------

// En flash se guardan 40 bits del hash (5 bytes por dominio): con 200 mil dominios la
// probabilidad de bloquear por error un dominio cualquiera es ~1 en 5 millones.
constexpr int kKeyBits = 40;
constexpr size_t kKeyBytes = kKeyBits / 8;
inline uint64_t keyOf(uint64_t hash) { return hash >> (64 - kKeyBits); }

constexpr int kIndexBits = 12;                    // 4096 cubetas -> índice de 16 KB
constexpr size_t kIndexSize = (1u << kIndexBits) + 1;
inline uint32_t indexBucket(uint64_t key) { return uint32_t(key >> (kKeyBits - kIndexBits)); }

// Guarda/lee una clave de 40 bits en 5 bytes (little-endian).
inline void packKey(uint64_t key, uint8_t* out) {
  for (size_t i = 0; i < kKeyBytes; i++) out[i] = uint8_t(key >> (8 * i));
}
inline uint64_t unpackKey(const uint8_t* in) {
  uint64_t k = 0;
  for (size_t i = 0; i < kKeyBytes; i++) k |= uint64_t(in[i]) << (8 * i);
  return k;
}

// Construye el índice: idx[b] = primera posición del arreglo ordenado con cubeta >= b.
// idx[4096] = n. El arreglo debe estar ordenado.
void buildIndex(const uint64_t* sorted, size_t n, uint32_t* idx);

// Búsqueda binaria en un rango ordenado.
bool containsSorted(const uint64_t* arr, size_t n, uint64_t h);

// ---------- Reglas propias (desde el panel) ----------

// Dominios que el usuario bloquea o permite a mano. "Permitir" gana sobre todo lo demás.
class CustomRules {
 public:
  static constexpr size_t kMax = 200;
  enum Result { kNone = 0, kBlock = 1, kAllow = -1 };

  // Agrega o mueve un dominio a bloquear (block=true) o permitir. False si no es válido o no cabe.
  bool add(std::string domain, bool block);
  bool remove(std::string domain);
  // Revisa el dominio y sus padres.
  Result match(const std::string& name) const;

  // Texto para guardar: una regla por línea, "+dominio" bloquea y "-dominio" permite.
  std::string serialize() const;
  void parse(const std::string& text);

  const std::vector<std::string>& blocked() const { return block_; }
  const std::vector<std::string>& allowed() const { return allow_; }

 private:
  std::vector<std::string> block_, allow_;  // ordenados
};

// ---------- JSON ----------

// Escapa texto para ponerlo entre comillas en JSON. Deja pasar el UTF-8 válido (acentos) y
// cambia bytes sueltos inválidos (pueden venir en un nombre DNS) por U+FFFD.
std::string jsonEscape(const char* s);

// ---------- DNS ----------

struct DnsQuestion {
  uint16_t id = 0;
  uint16_t qtype = 0;
  uint16_t qclass = 0;
  std::string name;        // en minúsculas, sin punto final
  size_t questionEnd = 0;  // offset justo después de la pregunta
  bool rd = false;
  bool edns = false;       // trae registros adicionales (normalmente OPT/EDNS)
};

constexpr uint16_t kTypeA = 1;
constexpr uint16_t kTypeAAAA = 28;
constexpr size_t kMaxDnsPacket = 1500;

// Lee una consulta DNS estándar con exactamente una pregunta. False si está mal formada.
bool parseQuery(const uint8_t* buf, size_t len, DnsQuestion& q);
// Igual, pero para la respuesta del DNS de arriba (QR=1).
bool parseResponseQuestion(const uint8_t* buf, size_t len, DnsQuestion& q);

// TTL más corto de las respuestas (secciones respuesta y autoridad). False si la respuesta no se
// debe guardar en caché: truncada, error distinto de NXDOMAIN, mal formada o sin TTL.
bool responseMinTtl(const uint8_t* buf, size_t len, uint32_t& ttl);
// Resta `age` segundos a los TTL de todos los registros (mínimo 1). Ignora EDNS (OPT).
bool ageResponse(uint8_t* buf, size_t len, uint32_t age);

// Anti "DNS rebinding": true si la respuesta trae alguna IP privada, de loopback o de enlace
// local (10/8, 172.16/12, 192.168/16, 127/8, 169.254/16, 0/8, 100.64/10, ::1, fc00::/7,
// fe80::/10, ::ffff:privada). Un dominio público no debería apuntar a la red de la casa.
bool responseHasPrivateAddress(const uint8_t* buf, size_t len);
// Deja la respuesta sin registros y con REFUSED (en el mismo búfer). Devuelve el largo nuevo.
size_t refuseResponse(uint8_t* buf, size_t len);

// ---------- Caché de respuestas ----------

// Guarda respuestas del DNS real por (nombre, tipo, clase, EDNS) hasta que vence su TTL.
// No es segura entre hilos: el firmware la protege con un mutex.
class DnsCache {
 public:
  static constexpr size_t kMaxEntries = 64;
  static constexpr size_t kMaxResponse = 512;
  static constexpr uint32_t kMaxTtl = 3600;  // aunque el DNS diga más, se guarda máx. 1 h

  static std::string key(const DnsQuestion& q);
  // Guarda la respuesta. nowMs = reloj en milisegundos.
  void put(const std::string& key, const uint8_t* resp, size_t len, uint32_t nowMs);
  // Si hay respuesta vigente la copia en out con el ID de la consulta y TTL descontados.
  size_t get(const std::string& key, uint16_t id, uint32_t nowMs, uint8_t* out);
  size_t size() const { return entries_.size(); }
  void clear() { std::vector<Entry>().swap(entries_); }  // suelta también la memoria
  size_t bytes() const;

 private:
  struct Entry {
    std::string key;
    std::vector<uint8_t> resp;
    uint32_t storedMs, ttlMs, lastUsedMs;
  };
  std::vector<Entry> entries_;
};

// Escribe la respuesta de "bloqueado" en out (debe tener kMaxDnsPacket bytes).
// A -> 0.0.0.0, AAAA -> ::, otros tipos -> NOERROR sin respuestas. Devuelve el largo.
size_t buildBlockedResponse(const uint8_t* query, const DnsQuestion& q, uint8_t* out);

// Modo de configuración (portal cautivo): A -> ipv4 con TTL 0; otros tipos -> NOERROR sin
// respuestas. ipv4 va en el mismo orden que IPAddress (bytes en orden de red en memoria).
size_t buildAddressResponse(const uint8_t* query, const DnsQuestion& q, uint32_t ipv4, uint8_t* out);

// Respuesta SERVFAIL (cuando el DNS de arriba no contesta o no hay a dónde reenviar).
size_t buildServfail(const uint8_t* query, const DnsQuestion& q, uint8_t* out);

// NXDOMAIN ("ese nombre no existe"): mismo ID y pregunta, QR=1, RA=1, RD copiado, RCODE=3,
// sin respuestas ni EDNS. Se usa para el canario de Firefox y para Private Relay.
size_t buildNxdomain(const uint8_t* query, const DnsQuestion& q, uint8_t* out);

}  // namespace adblock

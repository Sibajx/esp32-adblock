// SiBlock: lista de permitidos de la comunidad, sin Arduino.
// Son sitios que la gente reportó con «No debió bloquearse» y que el dueño de siblock.s-dev.win
// revisó y aprobó a mano. La placa la baja (GET /api/v1/permitidos, sin mandar nada) solo si la
// persona enciende «Usar los permitidos de la comunidad», apagada de fábrica.
//
// En la placa no se guardan los nombres: solo una huella de 64 bits por sitio (8 B), ordenadas,
// para buscar rápido con poca RAM (500 sitios = 4 KB). Cuentan el sitio y sus subdominios, como
// «Permitir»; las reglas «Bloquear» de la persona ganan siempre (eso lo decide BlockList::check).
//
// La huella es SipHash-2-4 con una llave de 16 bytes al azar de cada placa (esp_random), nueva en
// cada bajada y guardada en /comunidad.bin. Sin llave (una huella fija, igual en todas las placas)
// alguien podría buscar un subdominio de anuncios que dé la misma huella que un sitio aprobado.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace siblock {

constexpr size_t kCommunityMaxBody = 32 * 1024;  // tope del cuerpo que se acepta
constexpr size_t kCommunityMax = 500;            // tope de sitios (el Worker tiene el mismo)

using CommunityKey = std::array<uint8_t, 16>;

// SipHash-2-4 de 64 bits (Aumasson y Bernstein) de los bytes de `p` con la llave `k`.
uint64_t sipHash24(const CommunityKey& k, const char* p, size_t n);
// Huella de un nombre (ya en minúsculas) con la llave de la placa.
inline uint64_t communityHash(const CommunityKey& k, const char* p, size_t n) { return sipHash24(k, p, n); }
inline uint64_t communityHash(const CommunityKey& k, const std::string& s) { return sipHash24(k, s.data(), s.size()); }

// true si `name` o alguno de sus padres está en `keys` (ordenadas de menor a mayor, hechas con la
// misma llave `k`).
bool communityAllows(const CommunityKey& k, const std::vector<uint64_t>& keys, const std::string& name);

struct CommunityList {
  uint32_t version = 0;        // el número que manda el Worker; sube con cada cambio
  uint32_t updated = 0;        // segundos desde 1970 de la última bajada buena (0 = sin hora)
  CommunityKey key{};          // la llave con la que se hicieron las huellas
  std::vector<uint64_t> keys;  // ordenadas, sin repetir, como mucho kCommunityMax
};

// Lee {"version": <entero>, "dominios": ["a.com", ...]} de a pedazos (el cuerpo llega por la red y
// no hace falta un búfer de 32 KB). Todo o nada: cualquier cosa rara rechaza el cuerpo entero.
// Reglas: un solo objeto; "version" entero de 0 a 4294967295 sin ceros a la izquierda; "dominios"
// lista de textos que pasan validReportDomain, como mucho kCommunityMax; las dos claves una sola
// vez; otras claves se saltan si su valor es texto, número, true, false o null; textos sin "\";
// como mucho kCommunityMaxBody bytes.
// La llave es la de la placa para esta bajada (en las pruebas, una fija).
class CommunityParser {
 public:
  explicit CommunityParser(const CommunityKey& key) : hashKey_(key) {}
  bool feed(const char* p, size_t n);  // false si ya se sabe que está mal
  bool finish(CommunityList& out);     // al terminar (una vez): true solo si todo cuadró; updated = 0

 private:
  enum class St : uint8_t { kStart, kKeyOrEnd, kKey, kColon, kValue, kAfterValue, kArrFirst, kArrItem, kArrAfter, kDone, kBad };
  enum class Key : uint8_t { kOther, kVersion, kDomains };
  bool step(char c);
  bool endString();
  bool endScalar();

  St st_ = St::kStart;
  Key key_ = Key::kOther;
  bool inString_ = false, inScalar_ = false, longString_ = false;
  bool sawVersion_ = false, sawDomains_ = false;
  size_t total_ = 0;
  std::string cur_;
  uint32_t version_ = 0;
  CommunityKey hashKey_;
  std::vector<uint64_t> keys_;
};

// Archivo /comunidad.bin: "SBC2", version, updated, cantidad (u32 little-endian), la llave (16 B) y
// las huellas (u64 little-endian), ordenadas y sin repetir. Como mucho 32 + 500 × 8 = 4032 B.
// Un "SBC1" (huellas sin llave, de versiones viejas) no se lee: queda sin permitidos hasta la
// próxima bajada.
constexpr size_t kCommunityFileHeader = 32;
std::string encodeCommunity(const CommunityList& l);
// false si la cabecera, el largo, el tope o el orden no cuadran (archivo roto: se ignora).
bool decodeCommunity(const uint8_t* p, size_t n, CommunityList& out);

}  // namespace siblock

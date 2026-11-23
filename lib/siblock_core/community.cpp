#include "community.h"

#include <algorithm>

#include "cloud.h"

namespace siblock {

namespace {
constexpr size_t kMaxString = 253;  // un dominio válido no pasa de aquí
constexpr size_t kMaxScalar = 32;   // números y true/false/null

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isScalarChar(char c) {
  return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '+' || c == '.';
}

void put32(std::string& out, uint32_t v) {
  for (int i = 0; i < 4; i++) out += char(uint8_t(v >> (8 * i)));
}
void put64(std::string& out, uint64_t v) {
  for (int i = 0; i < 8; i++) out += char(uint8_t(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32; }

uint64_t rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

void sipRound(uint64_t& v0, uint64_t& v1, uint64_t& v2, uint64_t& v3) {
  v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
  v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
  v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
  v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
}
}  // namespace

uint64_t sipHash24(const CommunityKey& k, const char* p, size_t n) {
  const uint64_t k0 = get64(k.data()), k1 = get64(k.data() + 8);
  uint64_t v0 = 0x736f6d6570736575ULL ^ k0, v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0, v3 = 0x7465646279746573ULL ^ k1;
  const uint8_t* in = reinterpret_cast<const uint8_t*>(p);
  size_t full = n & ~size_t(7);
  for (size_t i = 0; i < full; i += 8) {
    uint64_t m = get64(in + i);
    v3 ^= m;
    sipRound(v0, v1, v2, v3);
    sipRound(v0, v1, v2, v3);
    v0 ^= m;
  }
  uint64_t b = uint64_t(n) << 56;
  for (size_t i = full; i < n; i++) b |= uint64_t(in[i]) << (8 * (i - full));
  v3 ^= b;
  sipRound(v0, v1, v2, v3);
  sipRound(v0, v1, v2, v3);
  v0 ^= b;
  v2 ^= 0xff;
  for (int i = 0; i < 4; i++) sipRound(v0, v1, v2, v3);
  return v0 ^ v1 ^ v2 ^ v3;
}

bool communityAllows(const CommunityKey& k, const std::vector<uint64_t>& keys, const std::string& name) {
  if (keys.empty() || name.empty()) return false;
  // El nombre y cada padre: "a.b.com", "b.com", "com".
  for (size_t i = 0; i < name.size();) {
    if (std::binary_search(keys.begin(), keys.end(), communityHash(k, name.data() + i, name.size() - i))) return true;
    size_t dot = name.find('.', i);
    if (dot == std::string::npos) break;
    i = dot + 1;
  }
  return false;
}

// ---------- Lectura del JSON, de a pedazos ----------

bool CommunityParser::feed(const char* p, size_t n) {
  for (size_t i = 0; i < n && st_ != St::kBad; i++) {
    if (++total_ > kCommunityMaxBody || !step(p[i])) st_ = St::kBad;
  }
  return st_ != St::kBad;
}

bool CommunityParser::step(char c) {
  if (inString_) {
    if (c == '"') {
      inString_ = false;
      return endString();
    }
    if (c == '\\' || uint8_t(c) < 0x20) return false;  // sin escapes ni controles
    if (cur_.size() < kMaxString) cur_ += c;
    else longString_ = true;
    return true;
  }
  if (inScalar_) {
    if (isScalarChar(c)) {
      if (cur_.size() >= kMaxScalar) return false;
      cur_ += c;
      return true;
    }
    inScalar_ = false;
    if (!endScalar()) return false;
    // y el carácter que lo cortó se lee como siempre
  }
  if (isSpace(c)) return true;
  switch (st_) {
    case St::kStart:
      if (c != '{') return false;
      st_ = St::kKeyOrEnd;
      return true;
    case St::kKeyOrEnd:
    case St::kKey:
      if (c == '}' && st_ == St::kKeyOrEnd) {
        st_ = St::kDone;
        return true;
      }
      break;  // tiene que empezar un texto (la clave)
    case St::kColon:
      if (c != ':') return false;
      st_ = St::kValue;
      return true;
    case St::kValue:
      if (c == '[') {
        if (key_ != Key::kDomains) return false;  // otras claves: solo valores sencillos
        st_ = St::kArrFirst;
        return true;
      }
      if (c != '"' && isScalarChar(c)) {
        inScalar_ = true;
        cur_.assign(1, c);
        return true;
      }
      break;
    case St::kAfterValue:
      if (c == ',') {
        st_ = St::kKey;
        return true;
      }
      if (c == '}') {
        st_ = St::kDone;
        return true;
      }
      return false;
    case St::kArrFirst:
      if (c == ']') {
        st_ = St::kAfterValue;
        return true;
      }
      break;
    case St::kArrItem:
      break;
    case St::kArrAfter:
      if (c == ',') {
        st_ = St::kArrItem;
        return true;
      }
      if (c == ']') {
        st_ = St::kAfterValue;
        return true;
      }
      return false;
    default:  // kDone (solo espacios después) y kBad
      return false;
  }
  if (c != '"') return false;
  inString_ = true;
  longString_ = false;
  cur_.clear();
  return true;
}

bool CommunityParser::endString() {
  switch (st_) {
    case St::kKeyOrEnd:
    case St::kKey:
      key_ = Key::kOther;
      if (!longString_ && cur_ == "version") {
        if (sawVersion_) return false;
        sawVersion_ = true;
        key_ = Key::kVersion;
      } else if (!longString_ && cur_ == "dominios") {
        if (sawDomains_) return false;
        sawDomains_ = true;
        key_ = Key::kDomains;
      }
      st_ = St::kColon;
      return true;
    case St::kValue:
      if (key_ != Key::kOther) return false;  // "version" es número y "dominios" es lista
      st_ = St::kAfterValue;
      return true;
    case St::kArrFirst:
    case St::kArrItem:
      if (longString_ || !validReportDomain(cur_) || keys_.size() >= kCommunityMax) return false;
      keys_.push_back(communityHash(hashKey_, cur_));
      st_ = St::kArrAfter;
      return true;
    default:
      return false;
  }
}

bool CommunityParser::endScalar() {
  if (st_ != St::kValue || key_ == Key::kDomains) return false;
  if (key_ == Key::kVersion) {
    if (cur_.empty() || cur_.size() > 10 || (cur_.size() > 1 && cur_[0] == '0')) return false;
    uint64_t v = 0;
    for (char c : cur_) {
      if (!isDigit(c)) return false;
      v = v * 10 + uint64_t(c - '0');
    }
    if (v > 0xFFFFFFFFULL) return false;
    version_ = uint32_t(v);
  } else if (cur_ != "true" && cur_ != "false" && cur_ != "null") {
    // Un número cualquiera: empieza con '-' o un dígito y solo trae [0-9.eE+-].
    if (!isDigit(cur_[0]) && cur_[0] != '-') return false;
    for (char c : cur_)
      if (!isDigit(c) && c != '.' && c != 'e' && c != 'E' && c != '+' && c != '-') return false;
  }
  st_ = St::kAfterValue;
  return true;
}

bool CommunityParser::finish(CommunityList& out) {
  if (st_ != St::kDone || inString_ || inScalar_ || !sawVersion_ || !sawDomains_) return false;
  std::sort(keys_.begin(), keys_.end());
  keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
  out.version = version_;
  out.updated = 0;
  out.key = hashKey_;
  out.keys = std::move(keys_);  // el parser ya no se usa después
  return true;
}

// ---------- Archivo en la flash ----------

std::string encodeCommunity(const CommunityList& l) {
  std::string out("SBC2", 4);
  put32(out, l.version);
  put32(out, l.updated);
  put32(out, uint32_t(l.keys.size()));
  out.append(reinterpret_cast<const char*>(l.key.data()), l.key.size());
  for (uint64_t k : l.keys) put64(out, k);
  return out;
}

bool decodeCommunity(const uint8_t* p, size_t n, CommunityList& out) {
  constexpr size_t kHead = kCommunityFileHeader;
  if (!p || n < kHead || p[0] != 'S' || p[1] != 'B' || p[2] != 'C' || p[3] != '2') return false;
  uint32_t count = get32(p + 12);
  if (count > kCommunityMax || n != kHead + size_t(count) * 8) return false;
  std::vector<uint64_t> keys;
  keys.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    uint64_t k = get64(p + kHead + size_t(i) * 8);
    if (!keys.empty() && k <= keys.back()) return false;  // ordenadas y sin repetir
    keys.push_back(k);
  }
  out.version = get32(p + 4);
  out.updated = get32(p + 8);
  std::copy(p + 16, p + 32, out.key.begin());
  out.keys = std::move(keys);
  return true;
}

}  // namespace siblock

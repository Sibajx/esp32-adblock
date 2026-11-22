#include "adblock_core.h"

#include <algorithm>
#include <cstring>

namespace adblock {

uint64_t hashDomain(const char* s, size_t len) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; i++) {
    h ^= uint8_t(s[i]);
    h *= 0x100000001b3ULL;
  }
  return h;
}

static bool isDomainChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

bool normalizeDomain(std::string& d) {
  while (!d.empty() && d.back() == '.') d.pop_back();
  if (d.empty() || d.size() > 253) return false;
  for (char& c : d) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (!isDomainChar(c)) return false;
  }
  if (d.front() == '.' || d.find('.') == std::string::npos) return false;
  if (d.find("..") != std::string::npos) return false;
  return true;
}

static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool parseListLine(const char* line, size_t len, std::string& out) {
  // Recorta comentario y espacios.
  size_t end = 0;
  while (end < len && line[end] != '#' && line[end] != '!') end++;
  size_t i = 0;
  while (i < end && isSpace(line[i])) i++;
  while (end > i && isSpace(line[end - 1])) end--;
  if (i >= end) return false;

  // Primer token.
  size_t t1 = i;
  while (t1 < end && !isSpace(line[t1])) t1++;
  std::string first(line + i, t1 - i);

  std::string dom;
  if (first == "0.0.0.0" || first == "127.0.0.1" || first == "::" || first == "::1") {
    size_t j = t1;
    while (j < end && isSpace(line[j])) j++;
    size_t k = j;
    while (k < end && !isSpace(line[k])) k++;
    if (j == k) return false;
    dom.assign(line + j, k - j);
  } else if (first.size() > 3 && first.compare(0, 2, "||") == 0 && first.back() == '^') {
    dom = first.substr(2, first.size() - 3);
  } else if (t1 == end) {
    dom = first;  // lista de dominios simples, uno por línea
  } else {
    return false;
  }

  if (!normalizeDomain(dom)) return false;
  // Entradas típicas de los hosts que no son anuncios.
  static const char* const kSkip[] = {"localhost", "localhost.localdomain", "local",
                                      "broadcasthost", "ip6-localhost", "ip6-loopback",
                                      "0.0.0.0"};
  for (const char* s : kSkip)
    if (dom == s) return false;
  out = std::move(dom);
  return true;
}

void buildIndex(const uint64_t* sorted, size_t n, uint32_t* idx) {
  size_t p = 0;
  for (uint32_t b = 0; b < (1u << kIndexBits); b++) {
    while (p < n && indexBucket(sorted[p]) < b) p++;
    idx[b] = uint32_t(p);
  }
  idx[1u << kIndexBits] = uint32_t(n);
}

bool containsSorted(const uint64_t* arr, size_t n, uint64_t h) {
  return std::binary_search(arr, arr + n, h);
}

// ---------- Reglas propias ----------

static bool eraseSorted(std::vector<std::string>& v, const std::string& d) {
  auto it = std::lower_bound(v.begin(), v.end(), d);
  if (it == v.end() || *it != d) return false;
  v.erase(it);
  return true;
}

bool CustomRules::add(std::string domain, bool block) {
  if (!normalizeDomain(domain)) return false;
  auto& mine = block ? block_ : allow_;
  auto& other = block ? allow_ : block_;
  auto it = std::lower_bound(mine.begin(), mine.end(), domain);
  if (it != mine.end() && *it == domain) return true;  // ya estaba
  if (block_.size() + allow_.size() >= kMax && !eraseSorted(other, domain)) return false;
  eraseSorted(other, domain);
  mine.insert(std::lower_bound(mine.begin(), mine.end(), domain), domain);
  return true;
}

bool CustomRules::remove(std::string domain) {
  if (!normalizeDomain(domain)) return false;
  bool a = eraseSorted(block_, domain);
  bool b = eraseSorted(allow_, domain);
  return a || b;
}

CustomRules::Result CustomRules::match(const std::string& name) const {
  if (block_.empty() && allow_.empty()) return kNone;
  bool blocked = false;
  std::string tmp;
  bool allowed = forEachSuffix(name, [&](const char* p, size_t n) {
    tmp.assign(p, n);
    if (std::binary_search(allow_.begin(), allow_.end(), tmp)) return true;
    if (!blocked && std::binary_search(block_.begin(), block_.end(), tmp)) blocked = true;
    return false;
  });
  if (allowed) return kAllow;
  return blocked ? kBlock : kNone;
}

std::string CustomRules::serialize() const {
  std::string out;
  for (auto& d : block_) out += "+" + d + "\n";
  for (auto& d : allow_) out += "-" + d + "\n";
  return out;
}

void CustomRules::parse(const std::string& text) {
  block_.clear();
  allow_.clear();
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    size_t stop = (end > pos && text[end - 1] == '\r') ? end - 1 : end;
    if (stop > pos + 1 && (text[pos] == '+' || text[pos] == '-'))
      add(text.substr(pos + 1, stop - pos - 1), text[pos] == '+');
    pos = end + 1;
  }
}

// ---------- JSON ----------

// Largo de la secuencia UTF-8 válida que empieza en p (0 si no es válida).
static size_t utf8Len(const unsigned char* p) {
  unsigned char c = p[0];
  size_t n;
  uint32_t cp;
  if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1F;
  else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0F;
  else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07;
  else return 0;
  for (size_t i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) return 0;  // también corta en el '\0' final
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
      (cp >= 0xD800 && cp <= 0xDFFF))
    return 0;  // formas largas o sustitutos: inválidos
  return n;
}

std::string jsonEscape(const char* s) {
  std::string out;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (*p) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
      p++;
    } else if (c < 0x20 || c == 0x7f) {
      char buf[8];
      snprintf(buf, sizeof buf, "\\u%04x", c);
      out += buf;
      p++;
    } else if (c < 0x80) {
      out += char(c);
      p++;
    } else if (size_t n = utf8Len(p)) {
      out.append(reinterpret_cast<const char*>(p), n);
      p += n;
    } else {
      out += "\\ufffd";
      p++;
    }
  }
  return out;
}

// ---------- DNS ----------

static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
static void wr16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v >> 8);
  p[1] = uint8_t(v);
}

static bool parseQuestion(const uint8_t* buf, size_t len, DnsQuestion& q, bool response) {
  if (len < 12 || len > kMaxDnsPacket) return false;
  uint16_t flags = rd16(buf + 2);
  if (bool(flags & 0x8000) != response) return false;  // QR: consulta o respuesta
  if (((flags >> 11) & 0xF) != 0) return false;  // solo OPCODE QUERY
  if (rd16(buf + 4) != 1) return false;      // exactamente una pregunta
  q.id = rd16(buf);
  q.rd = flags & 0x0100;
  q.edns = rd16(buf + 10) != 0;

  size_t p = 12;
  std::string name;
  while (true) {
    if (p >= len) return false;
    uint8_t l = buf[p++];
    if (l == 0) break;
    if (l & 0xC0) return false;  // compresión: no válida en una pregunta
    if (p + l > len || name.size() + l + 1 > 254) return false;
    // Un punto (o un byte raro) dentro de una etiqueta haría que "bank.com" en una sola
    // etiqueta se viera igual que bank.com y compartiera entrada de caché: se rechaza.
    for (size_t i = 0; i < l; i++) {
      uint8_t c = buf[p + i];
      if (c == '.' || c < 0x21 || c > 0x7e) return false;
    }
    if (!name.empty()) name += '.';
    name.append(reinterpret_cast<const char*>(buf + p), l);
    p += l;
  }
  if (p + 4 > len) return false;
  q.qtype = rd16(buf + p);
  q.qclass = rd16(buf + p + 2);
  q.questionEnd = p + 4;
  for (char& c : name)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  q.name = std::move(name);
  return true;
}

bool parseQuery(const uint8_t* buf, size_t len, DnsQuestion& q) {
  return parseQuestion(buf, len, q, false);
}

bool parseResponseQuestion(const uint8_t* buf, size_t len, DnsQuestion& q) {
  return parseQuestion(buf, len, q, true);
}

// Salta un nombre (con o sin compresión). Devuelve el offset siguiente o 0 si está mal.
static size_t skipName(const uint8_t* buf, size_t len, size_t p) {
  for (int labels = 0; labels < 128; labels++) {
    if (p >= len) return 0;
    uint8_t l = buf[p];
    if (l == 0) return p + 1;
    if ((l & 0xC0) == 0xC0) return p + 2 <= len ? p + 2 : 0;  // puntero: el nombre termina aquí
    if (l & 0xC0) return 0;
    p += 1 + l;
  }
  return 0;
}

// Recorre los registros de respuesta. fn(offsetDelTTL, tipo, seccion) por cada uno.
template <typename Fn>
static bool walkRecords(const uint8_t* buf, size_t len, Fn fn) {
  if (len < 12) return false;
  size_t p = 12;
  for (uint16_t i = 0, n = rd16(buf + 4); i < n; i++) {  // preguntas
    p = skipName(buf, len, p);
    if (!p || p + 4 > len) return false;
    p += 4;
  }
  const uint16_t counts[3] = {rd16(buf + 6), rd16(buf + 8), rd16(buf + 10)};
  for (int sec = 0; sec < 3; sec++) {
    for (uint16_t i = 0; i < counts[sec]; i++) {
      p = skipName(buf, len, p);
      if (!p || p + 10 > len) return false;
      uint16_t type = rd16(buf + p);
      uint16_t rdlen = rd16(buf + p + 8);
      if (p + 10 + rdlen > len) return false;
      fn(p + 4, type, sec);
      p += 10 + rdlen;
    }
  }
  return true;
}

static uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

bool responseMinTtl(const uint8_t* buf, size_t len, uint32_t& ttl) {
  if (len < 12) return false;
  uint16_t flags = rd16(buf + 2);
  if (flags & 0x0200) return false;  // TC: truncada
  uint8_t rcode = flags & 0x0F;
  if (rcode != 0 && rcode != 3) return false;  // solo NOERROR y NXDOMAIN
  uint32_t best = UINT32_MAX;
  bool ok = walkRecords(buf, len, [&](size_t ttlOff, uint16_t type, int sec) {
    if (sec < 2 && type != 41) best = std::min(best, rd32(buf + ttlOff));
  });
  if (!ok || best == UINT32_MAX || best == 0) return false;
  ttl = best;
  return true;
}

bool ageResponse(uint8_t* buf, size_t len, uint32_t age) {
  return walkRecords(buf, len, [&](size_t ttlOff, uint16_t type, int) {
    if (type == 41) return;  // en OPT el "TTL" son banderas de EDNS
    uint32_t t = rd32(buf + ttlOff);
    t = t > age ? t - age : 1;
    buf[ttlOff] = uint8_t(t >> 24);
    buf[ttlOff + 1] = uint8_t(t >> 16);
    buf[ttlOff + 2] = uint8_t(t >> 8);
    buf[ttlOff + 3] = uint8_t(t);
  });
}

static bool privateV4(const uint8_t* a) {
  return a[0] == 10 || a[0] == 127 || a[0] == 0 || (a[0] == 172 && (a[1] & 0xF0) == 16) ||
         (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254) ||
         (a[0] == 100 && (a[1] & 0xC0) == 64);
}

static bool privateV6(const uint8_t* a) {
  static const uint8_t loop[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
  if (memcmp(a, loop, 16) == 0) return true;
  if ((a[0] & 0xFE) == 0xFC) return true;                // fc00::/7
  if (a[0] == 0xFE && (a[1] & 0xC0) == 0x80) return true;  // fe80::/10
  return memcmp(a, mapped, 12) == 0 && privateV4(a + 12);
}

bool responseHasPrivateAddress(const uint8_t* buf, size_t len) {
  bool hit = false;
  walkRecords(buf, len, [&](size_t ttlOff, uint16_t type, int sec) {
    if (sec != 0) return;
    size_t rdOff = ttlOff + 6;  // TTL(4) + RDLENGTH(2)
    uint16_t rdlen = rd16(buf + ttlOff + 4);
    if (type == kTypeA && rdlen == 4 && privateV4(buf + rdOff)) hit = true;
    if (type == kTypeAAAA && rdlen == 16 && privateV6(buf + rdOff)) hit = true;
  });
  return hit;
}

size_t refuseResponse(uint8_t* buf, size_t len) {
  DnsQuestion q;
  if (!parseResponseQuestion(buf, len, q)) return 0;
  buf[3] = uint8_t((buf[3] & 0xF0) | 5);  // REFUSED
  wr16(buf + 6, 0);
  wr16(buf + 8, 0);
  wr16(buf + 10, 0);
  return q.questionEnd;
}

// ---------- Caché ----------

std::string DnsCache::key(const DnsQuestion& q) {
  char tail[16];
  snprintf(tail, sizeof tail, "|%u|%u|%d", q.qtype, q.qclass, q.edns ? 1 : 0);
  return q.name + tail;
}

void DnsCache::put(const std::string& key, const uint8_t* resp, size_t len, uint32_t nowMs) {
  uint32_t ttl;
  if (len > kMaxResponse || !responseMinTtl(resp, len, ttl)) return;
  ttl = std::min(ttl, kMaxTtl);
  // Saca lo vencido; si sigue lleno, saca el que menos se ha usado.
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const Entry& e) {
                                  return e.key == key || nowMs - e.storedMs >= e.ttlMs;
                                }),
                 entries_.end());
  if (entries_.size() >= kMaxEntries) {
    auto lru = std::min_element(entries_.begin(), entries_.end(), [&](const Entry& a, const Entry& b) {
      return nowMs - a.lastUsedMs > nowMs - b.lastUsedMs;
    });
    entries_.erase(lru);
  }
  entries_.push_back({key, std::vector<uint8_t>(resp, resp + len), nowMs, ttl * 1000, nowMs});
}

size_t DnsCache::get(const std::string& key, uint16_t id, uint32_t nowMs, uint8_t* out) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->key != key) continue;
    uint32_t age = nowMs - it->storedMs;
    if (age >= it->ttlMs) {
      entries_.erase(it);
      return 0;
    }
    it->lastUsedMs = nowMs;
    size_t n = it->resp.size();
    memcpy(out, it->resp.data(), n);
    out[0] = uint8_t(id >> 8);
    out[1] = uint8_t(id);
    ageResponse(out, n, age / 1000);
    return n;
  }
  return 0;
}

size_t DnsCache::bytes() const {
  size_t b = 0;
  for (auto& e : entries_) b += e.resp.size() + e.key.size() + sizeof(Entry);
  return b;
}

static size_t copyHeaderAndQuestion(const uint8_t* query, const DnsQuestion& q, uint8_t* out,
                                    uint8_t rcode) {
  memcpy(out, query, q.questionEnd);
  // QR=1, OPCODE=0, AA=0, TC=0, RD copiado, RA=1, RCODE
  out[2] = uint8_t(0x80 | (q.rd ? 0x01 : 0x00));
  out[3] = uint8_t(0x80 | (rcode & 0x0F));
  wr16(out + 4, 1);  // QDCOUNT
  wr16(out + 6, 0);  // ANCOUNT
  wr16(out + 8, 0);  // NSCOUNT
  wr16(out + 10, 0); // ARCOUNT (se descarta EDNS)
  return q.questionEnd;
}

size_t buildBlockedResponse(const uint8_t* query, const DnsQuestion& q, uint8_t* out) {
  size_t n = copyHeaderAndQuestion(query, q, out, 0);
  size_t rdlen = q.qtype == kTypeA ? 4 : q.qtype == kTypeAAAA ? 16 : 0;
  if (rdlen == 0 || q.qclass != 1) return n;
  wr16(out + 6, 1);       // ANCOUNT = 1
  out[n++] = 0xC0;        // nombre = puntero a la pregunta (offset 12)
  out[n++] = 0x0C;
  wr16(out + n, q.qtype); n += 2;
  wr16(out + n, 1);       n += 2;  // IN
  out[n++] = 0; out[n++] = 0; out[n++] = 0x01; out[n++] = 0x2C;  // TTL 300 s
  wr16(out + n, uint16_t(rdlen)); n += 2;
  memset(out + n, 0, rdlen);  // 0.0.0.0 o ::
  n += rdlen;
  return n;
}

size_t buildAddressResponse(const uint8_t* query, const DnsQuestion& q, uint32_t ipv4, uint8_t* out) {
  size_t n = copyHeaderAndQuestion(query, q, out, 0);
  if (q.qtype != kTypeA || q.qclass != 1) return n;
  wr16(out + 6, 1);       // ANCOUNT = 1
  out[n++] = 0xC0;        // nombre = puntero a la pregunta (offset 12)
  out[n++] = 0x0C;
  wr16(out + n, kTypeA);  n += 2;
  wr16(out + n, 1);       n += 2;  // IN
  out[n++] = 0; out[n++] = 0; out[n++] = 0; out[n++] = 0;  // TTL 0: que nadie lo recuerde
  wr16(out + n, 4);       n += 2;
  memcpy(out + n, &ipv4, 4);
  n += 4;
  return n;
}

size_t buildServfail(const uint8_t* query, const DnsQuestion& q, uint8_t* out) {
  return copyHeaderAndQuestion(query, q, out, 2);
}

size_t buildNxdomain(const uint8_t* query, const DnsQuestion& q, uint8_t* out) {
  return copyHeaderAndQuestion(query, q, out, 3);
}

}  // namespace adblock

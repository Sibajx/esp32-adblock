// Pruebas de la lógica pura en la PC:  make -C test_host
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../lib/adblock_core/adblock_core.h"

using namespace adblock;

static int fails = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      std::printf("FALLA %s:%d  %s\n", __FILE__, __LINE__, #c);     \
      fails++;                                                      \
    }                                                               \
  } while (0)

static bool parse(const char* s, std::string& out) { return parseListLine(s, strlen(s), out); }

static void testParseListLine() {
  std::string d;
  CHECK(parse("0.0.0.0 ads.example.com", d) && d == "ads.example.com");
  CHECK(parse("127.0.0.1\tTracker.Example.COM  # comentario", d) && d == "tracker.example.com");
  CHECK(parse("||doubleclick.net^", d) && d == "doubleclick.net");
  CHECK(parse("plain.example.org\r", d) && d == "plain.example.org");
  CHECK(parse("example.org.", d) && d == "example.org");
  CHECK(!parse("# solo comentario", d));
  CHECK(!parse("! comentario adblock", d));
  CHECK(!parse("", d));
  CHECK(!parse("   ", d));
  CHECK(!parse("127.0.0.1 localhost", d));
  CHECK(!parse("0.0.0.0 0.0.0.0", d));
  CHECK(!parse("0.0.0.0", d));
  CHECK(!parse("comlocal", d));                  // sin punto
  CHECK(!parse("bad domain with spaces", d));
  CHECK(!parse("||ads.com^$third-party", d));    // reglas con opciones: se ignoran
  CHECK(!parse("ex<script>.com", d));
  CHECK(!parse("a..b.com", d));
}

static void testSuffixes() {
  std::vector<std::string> got;
  forEachSuffix(std::string("a.b.ads.com"), [&](const char* p, size_t n) {
    got.emplace_back(p, n);
    return false;
  });
  CHECK(got.size() == 3);
  CHECK(got[0] == "a.b.ads.com" && got[1] == "b.ads.com" && got[2] == "ads.com");

  got.clear();
  forEachSuffix(std::string("com"), [&](const char* p, size_t n) {
    got.emplace_back(p, n);
    return false;
  });
  CHECK(got.empty());

  // Se detiene en el primero que coincide.
  int calls = 0;
  bool hit = forEachSuffix(std::string("x.y.z.com"), [&](const char*, size_t) {
    calls++;
    return true;
  });
  CHECK(hit && calls == 1);
}

static void testIndexAndLookup() {
  std::vector<std::string> doms = {"ads.com", "tracker.net", "x.y.z.org", "doubleclick.net",
                                   "a.com", "b.com", "c.com"};
  std::vector<uint64_t> hs;
  for (auto& d : doms) hs.push_back(keyOf(hashDomain(d)));
  std::sort(hs.begin(), hs.end());
  std::vector<uint32_t> idx(kIndexSize);
  buildIndex(hs.data(), hs.size(), idx.data());
  CHECK(idx[0] == 0);
  CHECK(idx[kIndexSize - 1] == hs.size());
  for (size_t b = 1; b < kIndexSize; b++) CHECK(idx[b] >= idx[b - 1]);

  auto lookup = [&](const std::string& d) {
    uint64_t h = keyOf(hashDomain(d));
    uint32_t b = indexBucket(h);
    return containsSorted(hs.data() + idx[b], idx[b + 1] - idx[b], h);
  };
  for (auto& d : doms) CHECK(lookup(d));
  CHECK(!lookup("google.com"));
  CHECK(!lookup("ads.co"));

  auto blocked = [&](const std::string& d) {
    return forEachSuffix(d, [&](const char* p, size_t n) { return lookup(std::string(p, n)); });
  };
  CHECK(blocked("sub.ads.com"));
  CHECK(blocked("deep.sub.doubleclick.net"));
  CHECK(!blocked("notads.com"));
  CHECK(!blocked("z.org"));  // el padre no está bloqueado aunque el hijo sí
}

static void testKeys() {
  uint8_t b[kKeyBytes];
  for (uint64_t k : {0ULL, 1ULL, 0xFFFFFFFFFFULL, 0x123456789AULL}) {
    packKey(k, b);
    CHECK(unpackKey(b) == k);
  }
  uint64_t k = keyOf(hashDomain("ads.com"));
  CHECK(k < (1ULL << kKeyBits));
  CHECK(indexBucket(k) < (1u << kIndexBits));
  CHECK(indexBucket(keyOf(~0ULL)) == (1u << kIndexBits) - 1);
}

static void testCustomRules() {
  CustomRules r;
  CHECK(r.add("Ads.Example.com", true));
  CHECK(r.add("tracker.net", true));
  CHECK(r.add("good.ads.example.com", false));
  CHECK(!r.add("no valido", true));
  CHECK(r.match("ads.example.com") == CustomRules::kBlock);
  CHECK(r.match("x.ads.example.com") == CustomRules::kBlock);
  CHECK(r.match("good.ads.example.com") == CustomRules::kAllow);
  CHECK(r.match("a.good.ads.example.com") == CustomRules::kAllow);
  CHECK(r.match("example.com") == CustomRules::kNone);

  // Mover de bloquear a permitir.
  CHECK(r.add("tracker.net", false));
  CHECK(r.match("tracker.net") == CustomRules::kAllow);
  CHECK(r.blocked().size() == 1 && r.allowed().size() == 2);

  // Guardar y leer (con fin de línea de Windows y basura).
  CustomRules r2;
  r2.parse(r.serialize() + "+otro.com\r\n#basura\n-\n+mal dominio\n");
  CHECK(r2.match("otro.com") == CustomRules::kBlock);
  CHECK(r2.match("sub.ads.example.com") == CustomRules::kBlock);
  CHECK(r2.match("tracker.net") == CustomRules::kAllow);
  CHECK(r2.blocked().size() == 2 && r2.allowed().size() == 2);

  CHECK(r2.remove("OTRO.com"));
  CHECK(!r2.remove("nunca.estuvo.com"));
  CHECK(r2.match("otro.com") == CustomRules::kNone);

  // Tope.
  CustomRules full;
  for (size_t i = 0; i < CustomRules::kMax; i++)
    CHECK(full.add("d" + std::to_string(i) + ".com", true));
  CHECK(!full.add("uno-mas.com", true));
  CHECK(full.add("d0.com", false));  // mover no cuenta como uno más
}

static void testJsonEscape() {
  CHECK(jsonEscape("abc.com") == "abc.com");
  CHECK(jsonEscape("conexión") == "conexión");          // UTF-8 válido pasa tal cual
  CHECK(jsonEscape("a\"b\\c") == "a\\\"b\\\\c");
  CHECK(jsonEscape("x\n\x01") == "x\\u000a\\u0001");
  CHECK(jsonEscape("\xff" "a") == "\\ufffda");            // byte suelto inválido
  CHECK(jsonEscape("\xc3") == "\\ufffd");                 // secuencia cortada al final
  CHECK(jsonEscape("\xc0\xaf") == "\\ufffd\\ufffd");      // forma larga prohibida
  CHECK(jsonEscape("\xed\xa0\x80") == "\\ufffd\\ufffd\\ufffd");  // sustituto
  CHECK(jsonEscape("😀") == "😀");
  CHECK(jsonEscape("<img src=x>") == "<img src=x>");      // el panel usa textContent
}

// Consulta "www.Ads.com" tipo A con EDNS (registro adicional OPT).
static std::vector<uint8_t> makeQuery(const char* name, uint16_t qtype, bool edns) {
  std::vector<uint8_t> q = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, uint8_t(edns ? 1 : 0)};
  const char* p = name;
  while (*p) {
    const char* dot = strchr(p, '.');
    size_t n = dot ? size_t(dot - p) : strlen(p);
    q.push_back(uint8_t(n));
    q.insert(q.end(), p, p + n);
    p += n;
    if (*p == '.') p++;
  }
  q.push_back(0);
  q.push_back(uint8_t(qtype >> 8));
  q.push_back(uint8_t(qtype));
  q.push_back(0);
  q.push_back(1);
  if (edns) {
    uint8_t opt[] = {0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0};
    q.insert(q.end(), opt, opt + sizeof(opt));
  }
  return q;
}

// Respuesta tipo: pregunta + 2 registros A (con puntero al nombre) + OPT opcional.
static std::vector<uint8_t> makeResponse(const char* name, uint32_t ttl1, uint32_t ttl2,
                                         uint8_t rcode = 0, bool tc = false, bool opt = true) {
  auto r = makeQuery(name, kTypeA, false);
  r[2] = uint8_t(0x81 | (tc ? 0x02 : 0));
  r[3] = uint8_t(0x80 | rcode);
  r[7] = 2;                // ANCOUNT
  r[11] = opt ? 1 : 0;     // ARCOUNT
  for (uint32_t ttl : {ttl1, ttl2}) {
    uint8_t rr[] = {0xC0, 0x0C, 0, 1, 0, 1, uint8_t(ttl >> 24), uint8_t(ttl >> 16),
                    uint8_t(ttl >> 8), uint8_t(ttl), 0, 4, 93, 184, 216, 34};
    r.insert(r.end(), rr, rr + sizeof rr);
  }
  if (opt) {
    uint8_t o[] = {0, 0, 41, 0x04, 0xD0, 0, 0, 0x80, 0, 0, 0};  // DO=1 en el "TTL"
    r.insert(r.end(), o, o + sizeof o);
  }
  return r;
}

static uint32_t ttlAt(const std::vector<uint8_t>& r, size_t i) {  // TTL del registro A número i
  size_t q = r.size() - (r[11] ? 11 : 0) - 32 + i * 16 + 6;
  return uint32_t(r[q]) << 24 | uint32_t(r[q + 1]) << 16 | uint32_t(r[q + 2]) << 8 | r[q + 3];
}

static void testTtlAndCache() {
  auto r = makeResponse("example.com", 300, 60);
  uint32_t ttl = 0;
  CHECK(responseMinTtl(r.data(), r.size(), ttl) && ttl == 60);
  auto bad = makeResponse("example.com", 300, 60, 2);
  CHECK(!responseMinTtl(bad.data(), bad.size(), ttl));  // SERVFAIL no se guarda
  auto trunc = makeResponse("example.com", 300, 60, 0, true);
  CHECK(!responseMinTtl(trunc.data(), trunc.size(), ttl));
  auto zero = makeResponse("example.com", 0, 0);
  CHECK(!responseMinTtl(zero.data(), zero.size(), ttl));
  std::vector<uint8_t> cut(r.begin(), r.end() - 20);
  CHECK(!responseMinTtl(cut.data(), cut.size(), ttl));  // cortada: mal formada

  auto aged = r;
  CHECK(ageResponse(aged.data(), aged.size(), 50));
  CHECK(ttlAt(aged, 0) == 250 && ttlAt(aged, 1) == 10);
  CHECK(aged[aged.size() - 4] == 0x80);  // las banderas del OPT no se tocan
  CHECK(ageResponse(aged.data(), aged.size(), 100) && ttlAt(aged, 1) == 1);  // nunca 0

  DnsQuestion q;
  auto query = makeQuery("Example.com", kTypeA, true);
  CHECK(parseQuery(query.data(), query.size(), q) && q.edns);
  DnsQuestion rq;
  CHECK(parseResponseQuestion(r.data(), r.size(), rq) && rq.name == "example.com");
  CHECK(!parseQuery(r.data(), r.size(), rq));  // una respuesta no es consulta

  DnsCache c;
  std::string k = DnsCache::key(q);
  uint8_t out[kMaxDnsPacket];
  CHECK(c.get(k, 0x4242, 0, out) == 0);
  c.put(k, r.data(), r.size(), 1000);
  size_t n = c.get(k, 0x4242, 31000, out);  // 30 s después
  CHECK(n == r.size() && out[0] == 0x42 && out[1] == 0x42);
  std::vector<uint8_t> got(out, out + n);
  CHECK(ttlAt(got, 0) == 270 && ttlAt(got, 1) == 30);
  CHECK(c.get(k, 1, 61000, out) == 0);   // venció a los 60 s
  CHECK(c.size() == 0);

  // Otra clave (sin EDNS o tipo distinto) no choca.
  q.edns = false;
  CHECK(DnsCache::key(q) != k);

  // Tope de entradas: saca la menos usada.
  for (size_t i = 0; i < DnsCache::kMaxEntries + 10; i++) {
    std::string kk = "d" + std::to_string(i) + ".com|1|1|1";
    c.put(kk, r.data(), r.size(), 2000 + 2 * uint32_t(i));
    if (i == 0) CHECK(c.get(kk, 1, 2001, out) > 0);  // "d0" se usa: no debe salir
    if (i == 40) CHECK(c.get("d0.com|1|1|1", 1, 2081, out) > 0);
  }
  CHECK(c.size() == DnsCache::kMaxEntries);
  CHECK(c.get("d0.com|1|1|1", 1, 3000, out) > 0);
  CHECK(c.get("d1.com|1|1|1", 1, 3000, out) == 0);
  CHECK(c.bytes() < 64 * 1024);

  // Respuestas grandes no se guardan.
  std::vector<uint8_t> big = r;
  big.resize(DnsCache::kMaxResponse + 1);
  DnsCache c2;
  c2.put("x", big.data(), big.size(), 0);
  CHECK(c2.size() == 0);
}

static void testRebind() {
  auto pub = makeResponse("evil.example", 60, 60);
  CHECK(!responseHasPrivateAddress(pub.data(), pub.size()));
  // Cambia la IP del primer registro A.
  size_t ip = pub.size() - 11 - 32 + 12;
  auto withIp = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    auto r = pub;
    r[ip] = a; r[ip + 1] = b; r[ip + 2] = c; r[ip + 3] = d;
    return responseHasPrivateAddress(r.data(), r.size());
  };
  CHECK(withIp(192, 168, 1, 50));
  CHECK(withIp(10, 0, 0, 1));
  CHECK(withIp(172, 16, 0, 1) && withIp(172, 31, 255, 255) && !withIp(172, 32, 0, 1));
  CHECK(withIp(127, 0, 0, 1) && withIp(169, 254, 1, 1) && withIp(0, 0, 0, 0));
  CHECK(withIp(100, 64, 0, 1) && !withIp(100, 128, 0, 1));
  CHECK(!withIp(8, 8, 8, 8) && !withIp(192, 169, 1, 1));

  auto r = pub;
  r[ip] = 192; r[ip + 1] = 168;
  size_t n = refuseResponse(r.data(), r.size());
  DnsQuestion q;
  CHECK(n > 12 && (r[3] & 0x0F) == 5 && r[7] == 0 && r[11] == 0);
  CHECK(parseResponseQuestion(r.data(), n, q) && q.name == "evil.example");
  CHECK(!responseHasPrivateAddress(r.data(), n));
}

static void testDns() {
  auto q = makeQuery("www.Ads.com", kTypeA, true);
  DnsQuestion dq;
  CHECK(parseQuery(q.data(), q.size(), dq));
  CHECK(dq.id == 0x1234 && dq.name == "www.ads.com" && dq.qtype == kTypeA && dq.rd);

  uint8_t out[kMaxDnsPacket];
  size_t n = buildBlockedResponse(q.data(), dq, out);
  CHECK(n == dq.questionEnd + 16);
  CHECK(out[0] == 0x12 && out[1] == 0x34);
  CHECK(out[2] == 0x81 && out[3] == 0x80);  // QR, RD, RA, NOERROR
  CHECK(out[7] == 1 && out[11] == 0);       // 1 respuesta, sin EDNS
  CHECK(out[n - 4] == 0 && out[n - 1] == 0);  // 0.0.0.0

  auto q6 = makeQuery("ads.com", kTypeAAAA, false);
  CHECK(parseQuery(q6.data(), q6.size(), dq));
  n = buildBlockedResponse(q6.data(), dq, out);
  CHECK(n == dq.questionEnd + 28);

  auto qtxt = makeQuery("ads.com", 16, false);
  CHECK(parseQuery(qtxt.data(), qtxt.size(), dq));
  n = buildBlockedResponse(qtxt.data(), dq, out);
  CHECK(n == dq.questionEnd && out[7] == 0);

  n = buildServfail(q.data(), dq, out);
  CHECK((out[3] & 0x0F) == 2);

  // Paquetes malos.
  CHECK(!parseQuery(q.data(), 5, dq));
  auto bad = q;
  bad[2] |= 0x80;  // es respuesta
  CHECK(!parseQuery(bad.data(), bad.size(), dq));
  bad = q;
  bad[5] = 2;  // dos preguntas
  CHECK(!parseQuery(bad.data(), bad.size(), dq));
  bad = q;
  bad[12] = 0xC0;  // compresión en la pregunta
  CHECK(!parseQuery(bad.data(), bad.size(), dq));
  bad = q;
  bad[12] = 60;  // etiqueta más larga que el paquete
  CHECK(!parseQuery(bad.data(), bad.size(), dq));
  std::vector<uint8_t> trunc(q.begin(), q.begin() + q.size() - 11 - 2);  // sin qtype/qclass completos
  CHECK(!parseQuery(trunc.data(), trunc.size(), dq));

  // Una sola etiqueta "bank.com" no puede pasar por bank.com (envenenaría la caché).
  std::vector<uint8_t> dotted = {0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 8};
  for (char c : std::string("bank.com")) dotted.push_back(uint8_t(c));
  for (uint8_t b : {0, 0, 1, 0, 1}) dotted.push_back(b);
  CHECK(!parseQuery(dotted.data(), dotted.size(), dq));
  dotted[13 + 4] = ' ';  // espacio: tampoco
  CHECK(!parseQuery(dotted.data(), dotted.size(), dq));
  dotted[13 + 4] = '_';  // guion bajo (_dmarc, _sip): sí
  CHECK(parseQuery(dotted.data(), dotted.size(), dq) && dq.name == "bank_com");

  DnsCache c;
  c.put("a|1|1|0", out, 40, 0);
  c.clear();
  CHECK(c.size() == 0);
}

static void testCaptiveResponse() {
  // Modo de configuración: todo nombre apunta a la placa (192.168.4.1) con TTL 0.
  const uint8_t ipBytes[4] = {192, 168, 4, 1};
  uint32_t ip;
  memcpy(&ip, ipBytes, 4);  // mismo orden que IPAddress (bytes en orden de red en memoria)
  auto q = makeQuery("connectivitycheck.gstatic.com", kTypeA, true);
  DnsQuestion dq;
  CHECK(parseQuery(q.data(), q.size(), dq));
  uint8_t out[kMaxDnsPacket];
  size_t n = buildAddressResponse(q.data(), dq, ip, out);
  CHECK(n == dq.questionEnd + 16);
  CHECK(out[0] == 0x12 && out[1] == 0x34);                       // mismo ID
  CHECK(memcmp(out + 12, q.data() + 12, dq.questionEnd - 12) == 0);  // misma pregunta
  CHECK((out[3] & 0x0F) == 0 && out[7] == 1);                    // NOERROR, 1 respuesta
  CHECK(out[n - 10] == 0 && out[n - 9] == 0 && out[n - 8] == 0 && out[n - 7] == 0);  // TTL 0
  CHECK(out[n - 4] == 192 && out[n - 3] == 168 && out[n - 2] == 4 && out[n - 1] == 1);

  auto q6 = makeQuery("example.com", kTypeAAAA, false);
  CHECK(parseQuery(q6.data(), q6.size(), dq));
  n = buildAddressResponse(q6.data(), dq, ip, out);
  CHECK(n == dq.questionEnd && out[7] == 0 && (out[3] & 0x0F) == 0);  // AAAA: vacío
  CHECK(out[0] == 0x12 && out[1] == 0x34);
}

static void testNxdomain() {
  for (bool edns : {false, true}) {
    auto q = makeQuery("use-application-dns.net", kTypeA, edns);
    q[0] = 0xBE;
    q[1] = 0xEF;
    DnsQuestion dq;
    CHECK(parseQuery(q.data(), q.size(), dq));
    uint8_t out[kMaxDnsPacket];
    size_t n = buildNxdomain(q.data(), dq, out);
    CHECK(n == dq.questionEnd);
    CHECK(out[0] == 0xBE && out[1] == 0xEF);
    CHECK((out[3] & 0x0F) == 3);
    CHECK((out[2] & 0x80) != 0);  // QR
    CHECK((out[2] & 0x01) != 0);  // RD copiado
    CHECK((out[3] & 0x80) != 0);  // RA
    CHECK(out[4] == 0 && out[5] == 1);
    CHECK(out[6] == 0 && out[7] == 0 && out[8] == 0 && out[9] == 0);
    CHECK(out[10] == 0 && out[11] == 0);  // sin EDNS aunque la consulta lo traiga
    CHECK(memcmp(out + 12, q.data() + 12, dq.questionEnd - 12) == 0);
  }
}

// DDR (RFC 9462): _dns.resolver.arpa contesta NOERROR sin respuestas, para SVCB (64) y para A.
static void testNodata() {
  for (uint16_t t : {uint16_t(64), kTypeA}) {
    for (bool edns : {false, true}) {
      auto q = makeQuery("_dns.resolver.arpa", t, edns);
      q[0] = 0xCA;
      q[1] = 0xFE;
      DnsQuestion dq;
      CHECK(parseQuery(q.data(), q.size(), dq));
      CHECK(dq.qtype == t);
      uint8_t out[kMaxDnsPacket];
      size_t n = buildNodata(q.data(), dq, out);
      CHECK(n == dq.questionEnd);
      CHECK(out[0] == 0xCA && out[1] == 0xFE);
      CHECK((out[3] & 0x0F) == 0);   // NOERROR
      CHECK((out[2] & 0x80) != 0);   // QR
      CHECK((out[3] & 0x80) != 0);   // RA
      CHECK(out[4] == 0 && out[5] == 1);
      CHECK(out[6] == 0 && out[7] == 0 && out[8] == 0 && out[9] == 0);  // sin respuestas
      CHECK(out[10] == 0 && out[11] == 0);
      CHECK(memcmp(out + 12, q.data() + 12, dq.questionEnd - 12) == 0);
    }
  }
}

int main() {
  testParseListLine();
  testSuffixes();
  testIndexAndLookup();
  testKeys();
  testCustomRules();
  testJsonEscape();
  testTtlAndCache();
  testRebind();
  testDns();
  testCaptiveResponse();
  testNxdomain();
  testNodata();
  if (fails) {
    std::printf("%d fallas\n", fails);
    return 1;
  }
  std::printf("OK: todas las pruebas pasan\n");
  return 0;
}

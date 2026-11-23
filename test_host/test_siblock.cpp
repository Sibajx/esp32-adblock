// Pruebas de lib/siblock_core en la PC:  make -C test_host
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

#include "../lib/siblock_core/siblock_crypto.h"
#include "../lib/siblock_core/siblock_text.h"
#include "../lib/siblock_core/improv.h"
#include "../lib/siblock_core/modes.h"
#include "../lib/siblock_core/dns_names.h"
#include "../lib/siblock_core/usage.h"
#include "../lib/siblock_core/diagnose.h"
#include "../lib/siblock_core/cloud.h"
#include "../lib/siblock_core/family.h"
#include "../lib/siblock_core/backup.h"
#include "../lib/siblock_core/lists.h"
#include "../lib/siblock_core/community.h"
#include "../lib/siblock_core/notify.h"
#include "../lib/siblock_core/oui.h"

// family/backup usan adblock_core (sufijos, normalizeDomain); el Makefile solo enlaza
// siblock_core, así que se compila aquí junto con la prueba.
#include "../lib/adblock_core/adblock_core.cpp"

// Gancho de prueba de family.cpp (no está en family.h a propósito).
namespace siblock {
bool groupListsSortedForTest(size_t& social, size_t& games);
}

using namespace siblock;

static int fails = 0, checks = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    checks++;                                                       \
    if (!(c)) {                                                     \
      std::printf("FALLA %s:%d  %s\n", __FILE__, __LINE__, #c);     \
      fails++;                                                      \
    }                                                               \
  } while (0)

static std::string hexOf(const uint8_t* p, size_t n) { return toHex(p, n); }

static std::string pbkdf2Hex(const std::string& pass, const std::vector<uint8_t>& salt, uint32_t it) {
  uint8_t out[32];
  pbkdf2Sha256(reinterpret_cast<const uint8_t*>(pass.data()), pass.size(), salt.data(), salt.size(), it, out);
  return hexOf(out, 32);
}

static std::vector<uint8_t> bytesOf(const char* s) { return std::vector<uint8_t>(s, s + strlen(s)); }

// ---------- Tarea 1: cripto, base64, validaciones, contraseña del AP ----------

static void testSha256() {
  uint8_t out[32];
  sha256(reinterpret_cast<const uint8_t*>("abc"), 3, out);
  CHECK(hexOf(out, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  sha256(nullptr, 0, out);
  CHECK(hexOf(out, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  // 56 bytes: el relleno cae en un segundo bloque.
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256(reinterpret_cast<const uint8_t*>(two), strlen(two), out);
  CHECK(hexOf(out, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void testPbkdf2() {
  CHECK(pbkdf2Hex("passwd", bytesOf("salt"), 1) ==
        "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc");
  CHECK(pbkdf2Hex("Password", bytesOf("NaCl"), 80000) ==
        "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56");
  std::vector<uint8_t> salt;
  for (int i = 0; i < 16; i++) salt.push_back(uint8_t(i));
  CHECK(pbkdf2Hex("clave-de-prueba", salt, 10000) ==
        "7b2fee5e1607395507ca2347b2d3b5ff43036febbe50d3d487e3d30a72ef4cd5");
  CHECK(pbkdf2SelfTest());

  uint8_t a[32] = {0}, b[32] = {0};
  CHECK(equal32(a, b));
  b[31] = 0x01;
  CHECK(!equal32(a, b));
  b[31] = 0;
  b[0] = 0x80;
  CHECK(!equal32(a, b));
}

static void testHex() {
  uint8_t in[3] = {0x00, 0xab, 0xff};
  CHECK(toHex(in, 3) == "00abff");
  uint8_t out[3];
  CHECK(fromHex("00abff", out, 3) && memcmp(in, out, 3) == 0);
  CHECK(fromHex("00ABFF", out, 3) && memcmp(in, out, 3) == 0);
  CHECK(!fromHex("00abf", out, 3));    // largo malo
  CHECK(!fromHex("00abffff", out, 3)); // largo malo
  CHECK(!fromHex("00abfg", out, 3));   // dígito malo
}

static void testBase64() {
  std::string out;
  const char* s = "YWRtaW46Y2xhdmU6Y29uOmRvc+KCrA==";
  CHECK(base64Decode(s, strlen(s), out) && out == "admin:clave:con:dos\xe2\x82\xac");
  CHECK(base64Decode("YQ==", 4, out) && out == "a");
  CHECK(base64Decode("YWI=", 4, out) && out == "ab");
  CHECK(base64Decode("YWJj", 4, out) && out == "abc");
  CHECK(base64Decode("", 0, out) && out.empty());
  CHECK(!base64Decode("YW*", 3, out));
  CHECK(!base64Decode("YW*=", 4, out));
  CHECK(!base64Decode("Y", 1, out));
  CHECK(!base64Decode("YQ==YWJj", 8, out));  // relleno en medio
  CHECK(!base64Decode("Y=Jj", 4, out));
  CHECK(!base64Decode("Y===", 4, out));
}

static void testValidPanelPassword() {
  CHECK(!validPanelPassword(std::string(11, 'a')));
  CHECK(validPanelPassword(std::string(12, 'a')));
  CHECK(validPanelPassword(std::string(64, 'a')));
  CHECK(!validPanelPassword(std::string(65, 'a')));
  CHECK(!validPanelPassword("CAMBIA_ESTA_CLAVE"));
  CHECK(validPanelPassword("\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1"));  // ñ x7 = 14 B
  CHECK(!validPanelPassword(std::string("clave\0con-cero", 14)));  // un byte 0 no se guarda bien
}

static void testValidWifi() {
  CHECK(!validWifi("", ""));
  CHECK(validWifi("Casa", ""));
  CHECK(!validWifi("Casa", "1234567"));
  CHECK(validWifi("Casa", std::string(8, 'x')));
  CHECK(validWifi("Casa", std::string(63, 'x')));
  CHECK(!validWifi("Casa", std::string(64, 'x')));
  CHECK(validWifi(std::string(32, 's'), "clave123"));
  CHECK(!validWifi(std::string(33, 's'), "clave123"));
  CHECK(validWifi("Casa de Mam\xc3\xa1 \xc3\xb1", ""));
  CHECK(!validWifi(std::string("Ca\0sa", 5), ""));
  CHECK(!validWifi("Casa", std::string("clave\0" "123", 9)));
}

static uint32_t gCounter = 0;
static uint32_t counterRnd() { return gCounter++; }
static std::vector<uint32_t> gSeq;
static size_t gSeqPos = 0;
static uint32_t seqRnd() { return gSeqPos < gSeq.size() ? gSeq[gSeqPos++] : 0; }

static void testApPassword() {
  gCounter = 0;
  std::string p = makeApPassword(counterRnd);
  CHECK(p.size() == 10);
  bool allIn = true;
  for (char c : p)
    if (!strchr(kApAlphabet, c)) allIn = false;
  CHECK(allIn);
  CHECK(strlen(kApAlphabet) == 31);
  for (char bad : {'i', 'l', 'o', '0', '1'}) CHECK(!strchr(kApAlphabet, bad));

  // 255 (>= 248) se descarta; luego 0 -> 'a', 1 -> 'b'...
  gSeq = {255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  gSeqPos = 0;
  p = makeApPassword(seqRnd);
  CHECK(p == "abcdefghjk");
  // Se usa el byte bajo de cada llamada: 248 (= 31 x 8) se descarta, 247 sí vale (247 % 31 = 30).
  gSeq = {248, 0x100 + 247, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  gSeqPos = 0;
  p = makeApPassword(seqRnd);
  CHECK(p.size() == 10 && p[0] == kApAlphabet[30] && p[1] == 'a');
}

static void testApSsid() {
  const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xab, 0x0c, 0x3f};
  CHECK(apSsid(mac) == "SiBlock-0C3F");
}


// ---------- Tarea 2: Improv Serial ----------

static std::vector<uint8_t> hexBytes(const char* h) {
  std::vector<uint8_t> v;
  std::string s;
  for (const char* p = h; *p; p++)
    if (*p != ' ') s += *p;
  v.resize(s.size() / 2);
  fromHex(s, v.data(), v.size());
  return v;
}

static const char* kWifiPkt =
    "49 4d 50 52 4f 56 01 03 10 01 0e 04 43 61 73 61 08 63 6c 61 76 65 31 32 33 25";

// Pasa todos los bytes por el parser y devuelve los paquetes leídos (y cuántos checksums malos).
static std::vector<improv::Packet> feedAll(improv::Parser& ps, const std::vector<uint8_t>& in, int* bad = nullptr) {
  std::vector<improv::Packet> got;
  for (uint8_t b : in) {
    improv::Packet pk;
    auto st = ps.feed(b, pk);
    if (st == improv::Parser::kPacket) got.push_back(pk);
    if (st == improv::Parser::kBadChecksum && bad) (*bad)++;
  }
  return got;
}

static void append(std::vector<uint8_t>& v, const std::vector<uint8_t>& w) { v.insert(v.end(), w.begin(), w.end()); }
static void append(std::vector<uint8_t>& v, const char* text) { v.insert(v.end(), text, text + strlen(text)); }

static void testImprovParser() {
  using namespace improv;
  std::vector<uint8_t> pkt = hexBytes(kWifiPkt);
  {
    Parser ps;
    Packet pk;
    for (size_t i = 0; i + 1 < pkt.size(); i++) CHECK(ps.feed(pkt[i], pk) == Parser::kNone);
    CHECK(ps.feed(pkt.back(), pk) == Parser::kPacket);
    CHECK(pk.type == kRpc && pk.len == 0x10);
    Command c;
    CHECK(parseRpc(pk, c));
    CHECK(c.id == kCmdWifi && c.args.size() == 2 && c.args[0] == "Casa" && c.args[1] == "clave123");
  }
  {  // checksum malo y luego uno bueno
    Parser ps;
    std::vector<uint8_t> badPkt = pkt;
    badPkt.back() = 0x26;
    std::vector<uint8_t> in = badPkt;
    append(in, pkt);
    int bad = 0;
    auto got = feedAll(ps, in, &bad);
    CHECK(bad == 1);
    CHECK(got.size() == 1 && got[0].len == 0x10);
  }
  {  // texto de la placa antes y entre paquetes
    Parser ps;
    std::vector<uint8_t> in;
    append(in, "STATS,10,2,161000,120000\n[wifi] conectado\n");
    append(in, pkt);
    append(in, "\nSTATS,11,2,161000,120000\nIMPRO\n");
    append(in, hexBytes("49 4d 50 52 4f 56 01 03 02 a0 00 83"));
    auto got = feedAll(ps, in);
    CHECK(got.size() == 2);
    Command c;
    CHECK(got.size() == 2 && parseRpc(got[1], c) && c.id == kCmdBackupAp && c.args.empty());
  }
  {  // paquete cortado a la mitad seguido de uno completo
    Parser ps;
    std::vector<uint8_t> in(pkt.begin(), pkt.begin() + 13);
    append(in, pkt);
    auto got = feedAll(ps, in);
    Command c;
    CHECK(got.size() == 1 && parseRpc(got[0], c) && c.args.size() == 2 && c.args[1] == "clave123");
  }
  {  // cortado justo después de la cabecera, seguido de un paquete corto que cabe entero dentro
    Parser ps;
    std::vector<uint8_t> in(pkt.begin(), pkt.begin() + 9);
    append(in, hexBytes("49 4d 50 52 4f 56 01 03 02 a0 00 83"));
    append(in, "\n");
    append(in, hexBytes("49 4d 50 52 4f 56 01 03 02 a0 00 83"));
    append(in, "\n");
    auto got = feedAll(ps, in);
    CHECK(got.size() >= 1);
  }
  {  // versión distinta de 1: se ignora
    Parser ps;
    std::vector<uint8_t> v2 = hexBytes("49 4d 50 52 4f 56 02 03 02 a0 00 84");
    CHECK(feedAll(ps, v2).empty());
  }
  {
    Parser ps;
    std::vector<uint8_t> a0 = hexBytes("49 4d 50 52 4f 56 01 03 02 a0 00 83");
    auto got = feedAll(ps, a0);
    Command c;
    CHECK(got.size() == 1 && parseRpc(got[0], c) && c.id == 0xA0 && c.args.empty());
  }
}

static void testImprovRpc() {
  using namespace improv;
  Packet pk;
  pk.type = kRpc;
  Command c;
  // largo interno mayor que el paquete
  pk.len = 3;
  pk.data[0] = 1; pk.data[1] = 5; pk.data[2] = 0;
  CHECK(!parseRpc(pk, c));
  // texto que se sale de los datos
  pk.len = 4;
  pk.data[0] = 1; pk.data[1] = 2; pk.data[2] = 9; pk.data[3] = 'a';
  CHECK(!parseRpc(pk, c));
  // sin bytes
  pk.len = 0;
  CHECK(!parseRpc(pk, c));
  pk.len = 1;
  pk.data[0] = 1;
  CHECK(!parseRpc(pk, c));
  // no es RPC
  pk.type = kState;
  pk.len = 2;
  pk.data[0] = 1; pk.data[1] = 0;
  CHECK(!parseRpc(pk, c));
  // datos de más después del largo interno: se rechaza
  pk.type = kRpc;
  pk.len = 3;
  pk.data[0] = 1; pk.data[1] = 0; pk.data[2] = 0;
  CHECK(!parseRpc(pk, c));
}

static void testImprovBuild() {
  using namespace improv;
  CHECK(toHex(buildState(kReady).data(), buildState(kReady).size()) == "494d50524f56010101" "02e20a");
  auto r = buildRpcResult(1, {"http://192.168.1.50/"});
  CHECK(toHex(r.data(), r.size()) ==
        "494d50524f5601041701151468747470" "3a2f2f3139322e3136382e312e35302f" "050a");
  auto e = buildError(kErrUnknownCmd);
  CHECK(e.size() == 12 && e[7] == kError && e[8] == 1 && e[9] == kErrUnknownCmd && e[11] == '\n');
  // Más de 255 bytes de datos: vacío.
  CHECK(buildRpcResult(1, {std::string(200, 'a'), std::string(100, 'b')}).empty());
  CHECK(buildRpcResult(1, {std::string(256, 'a')}).empty());
  // Ida y vuelta: SSID UTF-8 y contraseña vacía, byte por byte.
  std::string ssid = "Casa de Mam\xc3\xa1 \xc3\xb1";
  std::vector<uint8_t> d = {kCmdWifi, uint8_t(1 + ssid.size() + 1), uint8_t(ssid.size())};
  d.insert(d.end(), ssid.begin(), ssid.end());
  d.push_back(0);
  auto pkt = buildPacket(kRpc, d.data(), uint8_t(d.size()));
  CHECK(pkt.back() == '\n');
  Parser ps;
  auto got = feedAll(ps, pkt);
  Command c;
  CHECK(got.size() == 1 && parseRpc(got[0], c) && c.id == kCmdWifi && c.args.size() == 2 &&
        c.args[0] == ssid && c.args[1].empty());
  // Resultado vacío (fin del escaneo): id, largo 0.
  auto end = buildRpcResult(kCmdScan, {});
  CHECK(end.size() == 13 && end[8] == 2 && end[9] == kCmdScan && end[10] == 0);
}

// ---------- Tarea 3: botón BOOT, LED, ventana de creación y política del portal ----------

// Aprieta el botón `ms` milisegundos (leyendo cada 10 ms), lo suelta y sigue leyendo 200 ms.
// Devuelve la última acción vista (y cuántas hubo) y deja `t` al final.
static ButtonAction pressFor(ButtonTracker& b, uint32_t& t, uint32_t ms, int* count = nullptr,
                             ButtonAction* beforeRelease = nullptr) {
  ButtonAction last = ButtonAction::kNone;
  int n = 0;
  for (uint32_t e = 0; e < ms; e += 10, t += 10) {
    b.update(true, t);
    ButtonAction a = b.take();
    if (a != ButtonAction::kNone) {
      last = a;
      n++;
    }
  }
  if (beforeRelease) *beforeRelease = last;
  for (uint32_t e = 0; e < 200; e += 10, t += 10) {
    b.update(false, t);
    ButtonAction a = b.take();
    if (a != ButtonAction::kNone) {
      last = a;
      n++;
    }
  }
  if (count) *count = n;
  return last;
}

static void testButtonAt(uint32_t start) {
  ButtonTracker b;
  uint32_t t = start;
  for (int i = 0; i < 10; i++, t += 10) b.update(false, t);  // reposo
  CHECK(pressFor(b, t, 30) == ButtonAction::kNone);
  CHECK(pressFor(b, t, 1000) == ButtonAction::kOpenSetupWindow);
  CHECK(pressFor(b, t, 2500) == ButtonAction::kNone);
  CHECK(pressFor(b, t, 5000) == ButtonAction::kConfigMode);
  CHECK(pressFor(b, t, 9000) == ButtonAction::kNone);
  int n = 0;
  ButtonAction before = ButtonAction::kNone;
  CHECK(pressFor(b, t, 12000, &n, &before) == ButtonAction::kFactoryReset);
  CHECK(before == ButtonAction::kFactoryReset && n == 1);  // antes de soltar y una sola vez
  // Rebote: el contacto se abre 20 ms a mitad de una pulsación de 1 s; cuenta como una sola.
  ButtonTracker r;
  t = start;
  int opens = 0;
  for (uint32_t e = 0; e < 1500; e += 5, t += 5) {
    bool pressed = e < 1000 && !(e >= 500 && e < 520);
    r.update(pressed, t);
    if (r.take() == ButtonAction::kOpenSetupWindow) opens++;
  }
  CHECK(opens == 1);
  // held / heldMs
  ButtonTracker h;
  t = start;
  h.update(false, t);
  CHECK(!h.held() && h.heldMs(t) == 0);
  h.update(true, t + 10);
  h.update(true, t + 100);
  CHECK(h.held() && h.heldMs(t + 1010) == 1000);
}

static void testLed() {
  // < 3 s: parpadea cada 100 ms
  CHECK(ledWhileHeld(1000, 0) != ledWhileHeld(1000, 100));
  CHECK(ledWhileHeld(1000, 0) == ledWhileHeld(1000, 200));
  CHECK(ledWhileHeld(4000, 0) && ledWhileHeld(4000, 100));             // fijo
  CHECK(ledWhileHeld(9000, 0) != ledWhileHeld(9000, 100));             // parpadea
  CHECK(!ledWhileHeld(11000, 0) && !ledWhileHeld(11000, 100));         // apagado
}

static void testSetupWindowAt(uint32_t t0) {
  SetupWindow w;
  CHECK(!w.isOpen(t0));
  w.open(t0);
  CHECK(w.isOpen(t0));
  CHECK(w.isOpen(t0 + 29 * 60000 + 59000));
  CHECK(w.secondsLeft(t0 + 29 * 60000 + 59000) == 1);
  CHECK(!w.isOpen(t0 + 30 * 60000));
  CHECK(w.secondsLeft(t0 + 30 * 60000) == 0);
  // Una vez cerrada no se reabre sola aunque millis() dé la vuelta.
  CHECK(!w.isOpen(t0 + 10));
  w.open(t0 + 31 * 60000);
  CHECK(w.isOpen(t0 + 40 * 60000));
  CHECK(w.secondsLeft(t0 + 31 * 60000) == 1800);
}

static void testPortalAt(uint32_t t0) {
  {  // sin WiFi guardado: abre a los 120 s
    PortalPolicy p;
    p.tick(t0, false, false, 0);
    p.tick(t0 + 119000, false, false, 0);
    CHECK(!p.wantPortal());
    p.tick(t0 + 120000, false, false, 0);
    CHECK(p.wantPortal());
  }
  {  // Improv reinicia la espera
    PortalPolicy p;
    p.tick(t0, false, false, 0);
    p.tick(t0 + 100000, false, false, 0);
    p.improvSeen(t0 + 100000);
    p.tick(t0 + 200000, false, false, 0);
    CHECK(!p.wantPortal());
    p.tick(t0 + 220000, false, false, 0);
    CHECK(p.wantPortal());
  }
  {  // WiFi guardado sin conexión: 600 s seguidos
    PortalPolicy p;
    p.tick(t0, true, false, 0);
    p.tick(t0 + 599000, true, false, 0);
    CHECK(!p.wantPortal());
    p.tick(t0 + 600000, true, false, 0);
    CHECK(p.wantPortal());
    // abierto, nadie conectado 900 s: cierra y el conteo de 600 s empieza otra vez
    p.tick(t0 + 600000 + 899000, true, false, 0);
    CHECK(p.wantPortal());
    p.tick(t0 + 600000 + 900000, true, false, 0);
    CHECK(!p.wantPortal());
    p.tick(t0 + 1500000 + 599000, true, false, 0);
    CHECK(!p.wantPortal());
    p.tick(t0 + 1500000 + 600000, true, false, 0);
    CHECK(p.wantPortal());
  }
  {  // la conexión que vuelve reinicia el conteo
    PortalPolicy p;
    p.tick(t0, true, false, 0);
    p.tick(t0 + 500000, true, true, 0);
    p.tick(t0 + 500010, true, false, 0);
    p.tick(t0 + 1000000, true, false, 0);
    CHECK(!p.wantPortal());
  }
  {  // abierto y la casa conecta: cierra
    PortalPolicy p;
    p.tick(t0, true, false, 0);
    p.tick(t0 + 600000, true, false, 0);
    CHECK(p.wantPortal());
    p.tick(t0 + 610000, true, true, 0);
    CHECK(!p.wantPortal());
  }
  {  // con estaciones conectadas no cierra
    PortalPolicy p;
    p.forceOpen(t0);
    CHECK(p.wantPortal());
    p.tick(t0 + 100000, true, false, 1);
    p.tick(t0 + 2000000, true, false, 1);
    CHECK(p.wantPortal());
    p.tick(t0 + 2000000 + 899000, true, false, 0);
    CHECK(p.wantPortal());
    p.tick(t0 + 2000000 + 900000, true, false, 0);
    CHECK(!p.wantPortal());
  }
  {  // forceClose (WiFi nuevo guardado desde el portal)
    PortalPolicy p;
    p.forceOpen(t0);
    p.forceClose(t0 + 1000);
    CHECK(!p.wantPortal());
    p.tick(t0 + 2000, true, false, 0);
    p.tick(t0 + 1000 + 599000, true, false, 0);
    CHECK(!p.wantPortal());
    p.tick(t0 + 1000 + 600000, true, false, 0);
    CHECK(p.wantPortal());
  }
  {  // abierto a mano (BOOT 3-8 s) con la casa conectada: no se cierra por estar conectada,
     // solo tras 15 min sin nadie
    PortalPolicy p;
    p.tick(t0, true, true, 0);
    p.forceOpen(t0 + 1000);
    p.tick(t0 + 2000, true, true, 0);
    CHECK(p.wantPortal());
    p.tick(t0 + 1000 + 899000, true, true, 0);
    CHECK(p.wantPortal());
    p.tick(t0 + 1000 + 900000, true, true, 0);
    CHECK(!p.wantPortal());
    // y la siguiente vez que abre solo, vuelve a cerrar al conectar
    p.tick(t0 + 1000 + 900000 + 600000, true, false, 0);
    p.tick(t0 + 1000 + 900000 + 1200000, true, false, 0);
    CHECK(p.wantPortal());
    p.tick(t0 + 1000 + 900000 + 1200010, true, true, 0);
    CHECK(!p.wantPortal());
  }
  {  // conectado: nunca abre solo
    PortalPolicy p;
    for (uint32_t e = 0; e <= 3600000; e += 60000) p.tick(t0 + e, true, true, 0);
    CHECK(!p.wantPortal());
  }
}

// La red propia de SiBlock nunca comparte direcciones con la red de la casa.
static void testApAddress() {
  CHECK(ipv4(192, 168, 4, 1) == 0xC0A80401u);
  const uint32_t m24 = ipv4(255, 255, 255, 0), m16 = ipv4(255, 255, 0, 0), m8 = ipv4(255, 0, 0, 0);
  CHECK(subnetsOverlap(ipv4(192, 168, 4, 1), m24, ipv4(192, 168, 4, 77), m24));
  CHECK(!subnetsOverlap(ipv4(192, 168, 4, 1), m24, ipv4(192, 168, 5, 1), m24));
  CHECK(subnetsOverlap(ipv4(192, 168, 4, 1), m24, ipv4(192, 168, 200, 9), m16));  // la casa /16 la abarca
  CHECK(!subnetsOverlap(ipv4(192, 168, 4, 1), m24, ipv4(192, 168, 4, 9), 0));     // sin red no choca
  // Sin WiFi o casa en otra red: la de siempre.
  CHECK(pickApAddress(0, 0) == ipv4(192, 168, 4, 1));
  CHECK(pickApAddress(0, m24) == ipv4(192, 168, 4, 1));
  CHECK(pickApAddress(ipv4(192, 168, 1, 50), m24) == ipv4(192, 168, 4, 1));
  // La casa también es 192.168.4.x: se usa otra.
  CHECK(pickApAddress(ipv4(192, 168, 4, 23), m24) == ipv4(192, 168, 243, 1));
  CHECK(pickApAddress(ipv4(192, 168, 4, 23), ipv4(255, 255, 252, 0)) == ipv4(192, 168, 243, 1));
  CHECK(pickApAddress(ipv4(192, 168, 4, 23), m16) == ipv4(172, 31, 243, 1));
  CHECK(pickApAddress(ipv4(172, 31, 1, 2), ipv4(255, 240, 0, 0)) == ipv4(192, 168, 4, 1));
  CHECK(pickApAddress(ipv4(10, 0, 0, 5), m8) == ipv4(192, 168, 4, 1));
  // Para cualquier casa razonable (/8 o más chica) la elegida no choca.
  for (uint32_t ip : {ipv4(192, 168, 4, 1), ipv4(192, 168, 243, 7), ipv4(172, 31, 243, 9), ipv4(10, 243, 77, 1)})
    for (uint32_t m : {m24, m16, m8, ipv4(255, 255, 255, 128)}) {
      uint32_t ap = pickApAddress(ip, m);
      CHECK(!subnetsOverlap(ap, kApMask, ip, m));
      CHECK((ap & 0xFF) == 1);
    }
}

// Improv: un WiFi nuevo solo cuenta como conectado si ya pasó un rato y es la red probada.
static void testImprovTry() {
  using namespace improv;
  CHECK(evalTry(0, true, true) == TryResult::kWaiting);    // el estado aún puede ser el viejo
  CHECK(evalTry(499, true, true) == TryResult::kWaiting);
  CHECK(evalTry(500, true, true) == TryResult::kConnected);
  CHECK(evalTry(5000, true, false) == TryResult::kWaiting);  // conectado, pero a otra red
  CHECK(evalTry(5000, false, true) == TryResult::kWaiting);
  CHECK(evalTry(19999, true, false) == TryResult::kWaiting);
  CHECK(evalTry(20000, true, false) == TryResult::kFailed);
  CHECK(evalTry(20000, false, false) == TryResult::kFailed);
}

static void testModes() {
  testButtonAt(1000);
  testButtonAt(0xFFFFFFFFu - 60000);
  testLed();
  testSetupWindowAt(0);
  testSetupWindowAt(0xFFFFFFFFu - 60000);
  testPortalAt(5);
  testPortalAt(0xFFFFFFFFu - 60000);
}

// ---------- Fase 2, tarea 2: nombres especiales ----------

static void testDnsNames() {
  CHECK(classifySpecial("ab12.prueba.siblock.internal") == SpecialName::kProbe);
  CHECK(classifySpecial("prueba.siblock.internal") == SpecialName::kProbe);
  CHECK(classifySpecial("xprueba.siblock.internal") == SpecialName::kNone);
  CHECK(classifySpecial("prueba.siblock.internal.com") == SpecialName::kNone);
  CHECK(classifySpecial("use-application-dns.net") == SpecialName::kCanary);
  CHECK(classifySpecial("a.use-application-dns.net") == SpecialName::kNone);
  CHECK(classifySpecial("mask.icloud.com") == SpecialName::kRelay);
  CHECK(classifySpecial("mask-h2.icloud.com") == SpecialName::kRelay);
  CHECK(classifySpecial("icloud.com") == SpecialName::kNone);
  CHECK(classifySpecial("x.mask.icloud.com") == SpecialName::kNone);
  CHECK(classifySpecial("") == SpecialName::kNone);
  CHECK(classifySpecial("google.com") == SpecialName::kNone);
  // DDR (RFC 9462): resolver.arpa y todo lo de debajo se contesta aquí; parecidos no.
  CHECK(classifySpecial("_dns.resolver.arpa") == SpecialName::kDdr);
  CHECK(classifySpecial("resolver.arpa") == SpecialName::kDdr);
  CHECK(classifySpecial("_853._dns.resolver.arpa") == SpecialName::kDdr);
  CHECK(classifySpecial("xresolver.arpa") == SpecialName::kNone);
  CHECK(classifySpecial("resolver.arpa.com") == SpecialName::kNone);
  CHECK(classifySpecial("arpa") == SpecialName::kNone);
  CHECK(classifySpecial("1.168.192.in-addr.arpa") == SpecialName::kNone);
  CHECK(bypassFlagsFor("_dns.resolver.arpa") == 0);  // no quiere decir que el aparato se salte SiBlock

  CHECK(bypassFlagsFor("dns.google") == kBypassDoh);
  CHECK(bypassFlagsFor("chrome.cloudflare-dns.com") == kBypassDoh);
  CHECK(bypassFlagsFor("abc123.dns.nextdns.io") == kBypassDoh);
  CHECK(bypassFlagsFor("notdns.google") == 0);
  CHECK(bypassFlagsFor("google.com") == 0);
  CHECK(bypassFlagsFor("cloudflare-dns.com.evil.net") == 0);
  CHECK(bypassFlagsFor("mask.icloud.com") == kBypassRelay);
  CHECK(bypassFlagsFor("") == 0);
  CHECK(kDohNamesCount == 15);
  for (size_t i = 0; i < kDohNamesCount; i++) CHECK(bypassFlagsFor(kDohNames[i]) == kBypassDoh);

  CHECK(isProbeHost("ab12.prueba.siblock.internal"));
  CHECK(isProbeHost("AB12.Prueba.Siblock.Internal:8080"));
  CHECK(isProbeHost("a.b.prueba.siblock.internal"));
  CHECK(isProbeHost("a-1.prueba.siblock.internal:80"));
  CHECK(!isProbeHost("prueba.siblock.internal"));
  CHECK(!isProbeHost("x.prueba.siblock.internal.evil.com"));
  CHECK(!isProbeHost("xprueba.siblock.internal"));
  CHECK(!isProbeHost("a.prueba.siblock.internal:80x"));
  CHECK(!isProbeHost("a.prueba.siblock.internal:"));
  CHECK(!isProbeHost("a.prueba.siblock.internal."));
  CHECK(!isProbeHost("a_b.prueba.siblock.internal"));
  CHECK(!isProbeHost(""));
  CHECK(!isProbeHost("..prueba.siblock.internal"));
  CHECK(!isProbeHost(".prueba.siblock.internal"));
  CHECK(!isProbeHost("a..b.prueba.siblock.internal"));
  CHECK(!isProbeHost(std::string(64, 'a') + ".prueba.siblock.internal"));
  CHECK(isProbeHost(std::string(63, 'a') + ".prueba.siblock.internal"));
  CHECK(!isProbeHost(std::string("a\0b.prueba.siblock.internal", sizeof("a\0b.prueba.siblock.internal") - 1)));
  CHECK(!isProbeHost("a.prueba.siblock.internal:123456789012"));
}

// ---------- Fase 2, tarea 3: historia de 168 h y top de bloqueados ----------

static bool slotIs(const UsageHistory::Slot& s, uint32_t q, uint32_t b) { return s.queries == q && s.blocked == b; }

static uint32_t sumQueries(const UsageHistory& u, uint32_t now) {
  UsageHistory::Slot s[UsageHistory::kHours];
  u.snapshot(now, s);
  uint32_t t = 0;
  for (auto& x : s) t += x.queries;
  return t;
}

static void testUsageHistory() {
  CHECK(epochHour(1699999999) == 0);
  CHECK(epochHour(1700000000) == 472222);
  CHECK(epochHour(-5) == 0);
  CHECK(epochHour(0) == 0);

  const uint32_t h = 472222;
  UsageHistory::Slot s[UsageHistory::kHours];
  {
    UsageHistory u;
    u.snapshot(h, s);
    bool zero = true;
    for (auto& x : s) zero = zero && slotIs(x, 0, 0);
    CHECK(zero);
    u.add(h, false); u.add(h, false); u.add(h, false); u.add(h, true);
    u.snapshot(h, s);
    CHECK(slotIs(s[167], 4, 1));
    CHECK(slotIs(s[166], 0, 0));
    u.snapshot(h + 1, s);
    CHECK(slotIs(s[166], 4, 1));
    CHECK(slotIs(s[167], 0, 0));
    // Hueco de 5 horas.
    u.add(h + 5, true);
    u.snapshot(h + 5, s);
    CHECK(slotIs(s[167], 1, 1));
    CHECK(slotIs(s[162], 4, 1));
    for (int i = 163; i < 167; i++) CHECK(slotIs(s[i], 0, 0));
    // Snapshot hacia el pasado no muestra horas futuras.
    u.snapshot(h, s);
    CHECK(slotIs(s[167], 4, 1));
    CHECK(sumQueries(u, h) == 4);
  }
  {
    // Salto de 168 h o más: se limpia todo.
    UsageHistory u;
    u.add(h, true);
    u.add(h + 168, false);
    u.snapshot(h + 168, s);
    CHECK(slotIs(s[167], 1, 0));
    CHECK(sumQueries(u, h + 168) == 1);
    UsageHistory v;
    v.add(h, true);
    v.add(h + 1000, false);
    CHECK(sumQueries(v, h + 1000) == 1);
    // La casilla vieja de h (misma posición h % 168 que h+168) no reaparece en una ventana vieja.
    u.snapshot(h, s);
    CHECK(slotIs(s[167], 0, 0));
  }
  {
    // Hora vieja dentro de la ventana suma en su casilla; más vieja se ignora; 0 no cuenta.
    UsageHistory u;
    u.add(h + 10, false);
    u.add(h + 3, true);
    u.add(h + 10 - 168, true);
    u.add(0, true);
    u.snapshot(h + 10, s);
    CHECK(slotIs(s[167], 1, 0));
    CHECK(slotIs(s[160], 1, 1));
    CHECK(sumQueries(u, h + 10) == 2);
    u.add(h + 10 - 167, true);  // la más vieja que cabe
    u.snapshot(h + 10, s);
    CHECK(slotIs(s[0], 1, 1));
    UsageHistory e;
    e.add(0, true);
    CHECK(sumQueries(e, h) == 0);
  }
  {
    // Una hora equivocada hacia el futuro no congela la historia para siempre.
    UsageHistory u;
    u.add(h, true);
    u.add(h + 5000, false);  // hora falsa (≈7 meses adelante)
    for (uint32_t i = 0; i + 1 < UsageHistory::kStaleReset; i++) u.add(h + 1, false);
    CHECK(sumQueries(u, h + 1) == 0);  // pocas seguidas: todavía se ignoran
    u.add(h + 1, true);                // la número kStaleReset: empieza de nuevo
    u.snapshot(h + 1, s);
    CHECK(slotIs(s[167], 1, 1));
    CHECK(sumQueries(u, h + 1) == 1);
    u.add(h + 2, false);  // y sigue normal
    CHECK(sumQueries(u, h + 2) == 2);
    // Una vieja suelta entre horas buenas no borra nada (la racha se corta).
    UsageHistory v;
    v.add(h + 500, false);
    for (uint32_t i = 0; i < 3 * UsageHistory::kStaleReset; i++) {
      v.add(h, false);
      v.add(h + 500, false);
    }
    CHECK(sumQueries(v, h + 500) == 1 + 3 * UsageHistory::kStaleReset);
    // También si la hora falsa venía del archivo guardado.
    UsageHistory f;
    f.add(UsageHistory::kMaxHour, false);
    uint8_t buf[UsageHistory::kBytes];
    CHECK(f.serialize(buf, sizeof(buf)) == UsageHistory::kBytes);
    UsageHistory g;
    CHECK(g.deserialize(buf, sizeof(buf)));
    for (uint32_t i = 0; i < UsageHistory::kStaleReset; i++) g.add(h, i + 1 == UsageHistory::kStaleReset);
    g.snapshot(h, s);
    CHECK(slotIs(s[167], 1, 1));
  }
  {
    // Guardar y leer.
    UsageHistory u;
    u.add(h, true); u.add(h, false); u.add(h + 2, true);
    uint8_t buf[UsageHistory::kBytes + 10];
    CHECK(UsageHistory::kBytes == 1352);
    CHECK(u.serialize(buf, UsageHistory::kBytes - 1) == 0);
    size_t n = u.serialize(buf, sizeof(buf));
    CHECK(n == 1352);
    CHECK(memcmp(buf, "SBH1", 4) == 0);
    UsageHistory r;
    CHECK(r.deserialize(buf, n));
    UsageHistory::Slot a[UsageHistory::kHours], b[UsageHistory::kHours];
    u.snapshot(h + 2, a);
    r.snapshot(h + 2, b);
    CHECK(memcmp(a, b, sizeof(a)) == 0);
    // Malos: no cambian nada.
    UsageHistory k;
    k.add(h, true);
    CHECK(!k.deserialize(buf, n - 1));
    uint8_t bad[UsageHistory::kBytes];
    memcpy(bad, buf, n);
    memcpy(bad, "XXXX", 4);
    CHECK(!k.deserialize(bad, n));
    k.snapshot(h, a);
    CHECK(slotIs(a[167], 1, 1));
    CHECK(sumQueries(k, h) == 1);
    // Contenido incoherente (archivo dañado con cabecera buena): se rechaza sin cambiar nada.
    auto put = [](uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); };
    memcpy(bad, buf, n);
    put(bad + 4, 0xFFFFFF00u);  // última hora enorme: congelaría la historia
    CHECK(!k.deserialize(bad, n));
    put(bad + 4, UsageHistory::kMaxHour + 1);
    CHECK(!k.deserialize(bad, n));
    put(bad + 4, UsageHistory::kMinHour - 1);
    CHECK(!k.deserialize(bad, n));
    put(bad + 4, 0);  // sin última hora pero con casillas llenas: horas fantasma
    CHECK(!k.deserialize(bad, n));
    memcpy(bad, buf, n);
    put(bad + 8 + (h % 168) * 8, 1);   // consultas 1
    put(bad + 12 + (h % 168) * 8, 2);  // bloqueadas 2 > consultas
    CHECK(!k.deserialize(bad, n));
    CHECK(sumQueries(k, h) == 1);
    // Límites que sí valen.
    memcpy(bad, buf, n);
    put(bad + 4, UsageHistory::kMaxHour);
    CHECK(k.deserialize(bad, n));
    put(bad + 4, UsageHistory::kMinHour);
    CHECK(k.deserialize(bad, n));
    UsageHistory empty;
    size_t en = empty.serialize(bad, sizeof(bad));
    CHECK(en == UsageHistory::kBytes && k.deserialize(bad, en));  // vacía (última hora 0) vale
    CHECK(UsageHistory::kMinHour == epochHour(1700000000));
    CHECK(UsageHistory::kMaxHour == epochHour(4102444800LL));
  }
}

static std::vector<std::pair<std::string, uint32_t>> topOf(const TopBlocked& t, size_t max = 10) {
  TopBlocked::Entry e[TopBlocked::kSlots];
  size_t n = t.top(e, max < TopBlocked::kSlots ? max : TopBlocked::kSlots);
  std::vector<std::pair<std::string, uint32_t>> v;
  for (size_t i = 0; i < n; i++) v.push_back({e[i].name, e[i].count});
  return v;
}

static void testTopBlocked() {
  {
    TopBlocked t;
    CHECK(topOf(t).empty());
    for (int i = 0; i < 5; i++) t.add("a");
    for (int i = 0; i < 3; i++) t.add("b");
    t.add("c");
    auto v = topOf(t);
    CHECK(v.size() == 3);
    CHECK(v.size() == 3 && v[0].first == "a" && v[0].second == 5 && v[1].first == "b" &&
          v[1].second == 3 && v[2].first == "c" && v[2].second == 1);
    CHECK(topOf(t, 2).size() == 2);
    t.halve();
    v = topOf(t);
    CHECK(v.size() == 2 && v[0].first == "a" && v[0].second == 2 && v[1].first == "b" && v[1].second == 1);
  }
  {
    TopBlocked t;
    t.add("z"); t.add("y");
    auto v = topOf(t);
    CHECK(v.size() == 2 && v[0].first == "y" && v[1].first == "z");  // empate: por nombre
  }
  {
    TopBlocked t;
    char name[32];
    for (int i = 0; i < 32; i++) {
      snprintf(name, sizeof(name), "lleno%d.com", i);
      t.add(name);
    }
    for (int i = 0; i < 200; i++) {
      snprintf(name, sizeof(name), "unico%d.com", i);
      t.add(name);
      if (i % 2 == 0) t.add("pesado");
    }
    auto v = topOf(t);
    CHECK(!v.empty() && v[0].first == "pesado");
    CHECK(topOf(t, 100).size() == 32);
  }
  {
    TopBlocked t;
    std::string big(253, 'x');
    t.add(big.c_str());
    t.add(big.c_str());
    TopBlocked::Entry e[2];
    CHECK(t.top(e, 2) == 1);
    CHECK(strlen(e[0].name) == 63 && e[0].count == 2);
    t.add("");      // vacío: no cuenta
    t.add(nullptr);
    CHECK(t.top(e, 2) == 1);
  }
}

// ---------- Fase 2, tarea 4: aparatos que se saltan y diagnóstico ----------

static void testBypassTableAt(uint32_t t0) {
  const uint32_t ip1 = 0x1401A8C0;  // 192.168.1.20
  BypassTable t;
  CHECK(t.flagsFor(ip1, t0) == 0);
  t.note(ip1, kBypassDoh, t0);
  t.note(ip1, kBypassRelay, t0 + 10);
  t.note(ip1, 0, t0 + 15);  // flags 0: nada
  CHECK(t.flagsFor(ip1, t0 + 20) == 3);
  CHECK(t.flagsFor(ip1, t0 + 10 + BypassTable::kForgetMs - 1) == 3);
  CHECK(t.flagsFor(ip1, t0 + BypassTable::kForgetMs + 11) == 0);
  BypassTable::Entry e[BypassTable::kSlots + 2];
  CHECK(t.list(e, 4, t0 + BypassTable::kForgetMs + 11) == 0);
  CHECK(t.list(e, 4, t0 + 20) == 1 && e[0].ip == ip1 && e[0].flags == 3 && e[0].lastMs == t0 + 10);

  // flags 0 en una IP nueva no ocupa casilla.
  BypassTable z;
  z.note(ip1, 0, t0);
  CHECK(z.list(e, 4, t0) == 0);

  // 17 IPs: la primera (más vieja) se pierde.
  BypassTable u;
  for (uint32_t i = 0; i < 17; i++) u.note(100 + i, kBypassDoh, t0 + i * 1000);
  uint32_t now = t0 + 17 * 1000;
  CHECK(u.flagsFor(100, now) == 0);
  CHECK(u.flagsFor(101, now) == kBypassDoh);
  CHECK(u.flagsFor(116, now) == kBypassDoh);
  size_t n = u.list(e, BypassTable::kSlots + 2, now);
  CHECK(n == 16);
  CHECK(n == 16 && e[0].ip == 116 && e[15].ip == 101);
  bool ordered = true;
  for (size_t i = 1; i < n; i++) ordered = ordered && (now - e[i - 1].lastMs) <= (now - e[i].lastMs);
  CHECK(ordered);
  CHECK(u.list(e, 3, now) == 3 && e[0].ip == 116);
  // Una vencida se reusa antes que pisar una vigente.
  BypassTable w;
  w.note(205, kBypassDoh, t0);
  for (uint32_t i = 0; i < 16; i++)
    if (i != 5) w.note(200 + i, kBypassDoh, t0 + BypassTable::kForgetMs);
  w.note(999, kBypassRelay, t0 + BypassTable::kForgetMs + 5);
  CHECK(w.flagsFor(205, t0 + BypassTable::kForgetMs + 5) == 0);
  CHECK(w.flagsFor(200, t0 + BypassTable::kForgetMs + 5) == kBypassDoh);
  CHECK(w.flagsFor(999, t0 + BypassTable::kForgetMs + 5) == kBypassRelay);
}

static std::string diagStr(const DiagInput& in) {
  std::string out;
  for (auto& f : diagnose(in)) {
    if (!out.empty()) out += ",";
    out += f.code;
    out += "/";
    out += levelName(f.level);
  }
  return out;
}

static bool has(const std::string& s, const char* code) { return s.find(code) != std::string::npos; }

static void testDiagnose() {
  CHECK(parseProbe("ok") == ProbeResult::kOk);
  CHECK(parseProbe("fail") == ProbeResult::kFail);
  CHECK(parseProbe("OK") == ProbeResult::kSkip);
  CHECK(parseProbe("") == ProbeResult::kSkip);
  CHECK(parseProbe("okk") == ProbeResult::kSkip);
  CHECK(std::string(levelName(DiagLevel::kOk)) == "ok");
  CHECK(std::string(levelName(DiagLevel::kInfo)) == "info");
  CHECK(std::string(levelName(DiagLevel::kWarn)) == "warn");
  CHECK(std::string(levelName(DiagLevel::kBad)) == "bad");

  //            probe               seen  bypass        relayOn paused list   upDown viaRouter
  CHECK(diagStr({ProbeResult::kOk, true, 0, false, false, true, false, false, false, 0}) ==
        "usa_siblock/ok,todo_bien/ok");
  std::string s = diagStr({ProbeResult::kFail, false, 0, false, false, true, false, false, false, 0});
  CHECK(s == "no_usa_siblock/bad,ipv6/info");
  s = diagStr({ProbeResult::kFail, true, 0, false, false, true, false, false, false, 0});
  CHECK(has(s, "navegador_dns_propio/warn") && !has(s, "todo_bien") && !has(s, "no_usa_siblock"));
  CHECK(s == "navegador_dns_propio/warn,ipv6/info");  // también puede ser el DNS por IPv6 del módem
  s = diagStr({ProbeResult::kOk, true, kBypassRelay, false, false, true, false, false, false, 0});
  CHECK(has(s, "private_relay/warn") && !has(s, "todo_bien"));
  s = diagStr({ProbeResult::kOk, true, kBypassRelay, true, false, true, false, false, false, 0});
  CHECK(!has(s, "private_relay") && has(s, "todo_bien"));
  s = diagStr({ProbeResult::kOk, true, kBypassDoh, false, false, true, false, false, false, 0});
  CHECK(has(s, "dns_cifrado/warn"));
  s = diagStr({ProbeResult::kSkip, true, 0, false, true, false, true, false, false, 0});
  CHECK(s == "sin_lista/bad,sin_internet/bad,en_pausa/warn");
  s = diagStr({ProbeResult::kSkip, false, 0, false, false, true, false, false, false, 0});
  CHECK(s == "sin_consultas/warn");
  s = diagStr({ProbeResult::kSkip, true, 0, false, false, true, false, false, false, 0});
  CHECK(s == "todo_bien/ok");
  s = diagStr({ProbeResult::kOk, true, 0, false, false, true, false, true, false, 0});
  CHECK(s == "router_dns/warn,usa_siblock/ok");
  // Fase 5: pausa propia del aparato y horarios activos ahora.
  s = diagStr({ProbeResult::kOk, true, 0, false, false, true, false, false, true, 0});
  CHECK(s == "aparato_en_pausa/warn,usa_siblock/ok");
  s = diagStr({ProbeResult::kOk, true, 0, false, false, true, false, false, false, kGroupAll | kGroupSocial});
  CHECK(s == "horario_todo/warn,usa_siblock/ok");
  s = diagStr({ProbeResult::kOk, true, 0, false, false, true, false, false, false, kGroupGames});
  CHECK(s == "horario_activo/info,usa_siblock/ok,todo_bien/ok");
  // Siempre ordenado bad > warn > info > ok, nunca vacío y sin códigos repetidos.
  for (int mask = 0; mask < 2048; mask++) {
    for (int p = 0; p < 3; p++) {
      for (int os = 0; os <= int(DeviceOs::kLinux); os++) {
        DiagInput in{ProbeResult(p), bool(mask & 1), uint8_t((mask >> 1) & 3), bool(mask & 8),
                     bool(mask & 16), bool(mask & 32), bool(mask & 64), bool(mask & 128),
                     bool(mask & 256), uint8_t((mask >> 9) & 3) == 0 ? uint8_t(0) : uint8_t(1u << (((mask >> 9) & 3) - 1)),
                     DeviceOs(os)};
        auto v = diagnose(in);
        bool ok = !v.empty();
        for (size_t i = 1; i < v.size(); i++) ok = ok && uint8_t(v[i - 1].level) >= uint8_t(v[i].level);
        for (size_t i = 0; i < v.size(); i++)
          for (size_t j = i + 1; j < v.size(); j++) ok = ok && std::string(v[i].code) != v[j].code;
        CHECK(ok);
      }
    }
  }

  // Pasos por sistema: solo cuando el aparato no usa SiBlock, todos "info", después de lo demás.
  auto withOs = [](ProbeResult p, bool seen, uint8_t by, DeviceOs os) {
    return diagStr({p, seen, by, false, false, true, false, false, false, 0, os});
  };
  CHECK(withOs(ProbeResult::kFail, false, 0, DeviceOs::kWindows) ==
        "no_usa_siblock/bad,ipv6/info,windows_dns_cifrado/info,antivirus_dns/info,vpn/info");
  CHECK(withOs(ProbeResult::kFail, true, 0, DeviceOs::kWindows) ==
        "navegador_dns_propio/warn,ipv6/info,windows_dns_cifrado/info,antivirus_dns/info,vpn/info");
  CHECK(withOs(ProbeResult::kSkip, false, 0, DeviceOs::kWindows) ==
        "sin_consultas/warn,windows_dns_cifrado/info,antivirus_dns/info,vpn/info");
  CHECK(withOs(ProbeResult::kFail, false, 0, DeviceOs::kMac) == "no_usa_siblock/bad,ipv6/info,apple_perfil_dns/info,vpn/info");
  CHECK(withOs(ProbeResult::kFail, false, 0, DeviceOs::kIos) == "no_usa_siblock/bad,ipv6/info,apple_perfil_dns/info,vpn/info");
  CHECK(withOs(ProbeResult::kFail, false, 0, DeviceOs::kAndroid) ==
        "no_usa_siblock/bad,ipv6/info,android_dns_privado/info,vpn/info");
  // Si ya salió dns_cifrado ("apaga DNS privado"), Android no lo repite.
  CHECK(withOs(ProbeResult::kFail, true, kBypassDoh, DeviceOs::kAndroid) ==
        "navegador_dns_propio/warn,dns_cifrado/warn,ipv6/info,vpn/info");
  for (DeviceOs os : {DeviceOs::kOther, DeviceOs::kChromeOs, DeviceOs::kLinux})
    CHECK(withOs(ProbeResult::kFail, false, 0, os) == "no_usa_siblock/bad,ipv6/info,revisar_aparato/info");
  CHECK(withOs(ProbeResult::kFail, false, 0, DeviceOs::kNone) == "no_usa_siblock/bad,ipv6/info");
  // Si usa SiBlock (o preguntó hace poco y no se probó) no hay pasos por sistema.
  for (int os = 0; os <= int(DeviceOs::kLinux); os++) {
    CHECK(withOs(ProbeResult::kOk, true, 0, DeviceOs(os)) == "usa_siblock/ok,todo_bien/ok");
    CHECK(withOs(ProbeResult::kOk, false, 0, DeviceOs(os)) == "usa_siblock/ok,todo_bien/ok");
    CHECK(withOs(ProbeResult::kSkip, true, 0, DeviceOs(os)) == "todo_bien/ok");
  }

  const uint32_t gw = 0x0101A8C0;
  CHECK(!routerLooksLikeDns(gw, 40, 49));
  CHECK(routerLooksLikeDns(gw, 40, 50));
  CHECK(!routerLooksLikeDns(gw, 39, 50));
  CHECK(!routerLooksLikeDns(0, 100, 100));
  CHECK(routerLooksLikeDns(gw, 0xFFFFFFF0u, 0xFFFFFFF0u));  // sin desborde

  for (uint32_t base : {0u, 0xFFFFFFFFu - 1500u}) {
    CHECK(!upstreamLooksDown(base + 2000, base + 500, base + 1000, 0));
    // Un solo timeout (o dos) sin respuesta en medio no basta: casa tranquila o dominio lento.
    CHECK(!upstreamLooksDown(base + 2000, base + 500, base + 1000, 1));
    CHECK(!upstreamLooksDown(base + 2000, base + 500, base + 1000, kUpstreamDownTimeouts - 1));
    CHECK(upstreamLooksDown(base + 2000, base + 500, base + 1000, kUpstreamDownTimeouts));
    CHECK(upstreamLooksDown(base + 2000, base + 500, base + 1000, 0xFFFFFFFFu));
    CHECK(!upstreamLooksDown(base + 1000 + 300000, base + 500, base + 1000, 3));
    CHECK(upstreamLooksDown(base + 1000 + 299999, base + 500, base + 1000, 3));
    CHECK(!upstreamLooksDown(base + 2000, base + 1500, base + 1000, 3));
  }
}

// Sistema por el User-Agent (User-Agents reales de navegadores).
static void testOsFromUserAgent() {
  using namespace siblock;
  const struct {
    const char* ua;
    DeviceOs os;
  } casos[] = {
      {"", DeviceOs::kNone},
      {"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
       DeviceOs::kWindows},
      {"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 "
       "Edg/131.0.0.0",
       DeviceOs::kWindows},
      {"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0", DeviceOs::kWindows},
      {"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 "
       "Safari/605.1.15",
       DeviceOs::kMac},  // también un iPad con Safari en modo escritorio
      {"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0", DeviceOs::kMac},
      {"Mozilla/5.0 (iPhone; CPU iPhone OS 18_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 "
       "Mobile/15E148 Safari/604.1",
       DeviceOs::kIos},
      {"Mozilla/5.0 (iPhone; CPU iPhone OS 17_7 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) "
       "CriOS/131.0.6778.73 Mobile/15E148 Safari/604.1",
       DeviceOs::kIos},
      {"Mozilla/5.0 (iPad; CPU OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 "
       "Mobile/15E148 Safari/604.1",
       DeviceOs::kIos},
      {"Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36",
       DeviceOs::kAndroid},
      {"Mozilla/5.0 (Linux; Android 14; SAMSUNG SM-S918B) AppleWebKit/537.36 (KHTML, like Gecko) SamsungBrowser/26.0 "
       "Chrome/122.0.0.0 Mobile Safari/537.36",
       DeviceOs::kAndroid},
      {"Mozilla/5.0 (Android 14; Mobile; rv:133.0) Gecko/133.0 Firefox/133.0", DeviceOs::kAndroid},
      {"Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
       DeviceOs::kChromeOs},
      {"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
       DeviceOs::kLinux},
      {"Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0", DeviceOs::kLinux},
      {"Python-urllib/3.12", DeviceOs::kOther},
      {"curl/8.5.0", DeviceOs::kOther},
      {"windows", DeviceOs::kOther},  // se respetan mayúsculas, como las mandan los navegadores
  };
  for (auto& c : casos) CHECK(osFromUserAgent(c.ua) == c.os);
  CHECK(osFromUserAgent(std::string(4000, 'x') + "Android") == DeviceOs::kAndroid);
}

// Fase 2, corrección: el nombre de prueba nunca contesta 0.0.0.0 (solo AP o sin IP todavía).
static void testProbeAnswerIp() {
  using namespace siblock;
  CHECK(probeAnswerIp(0x0A01A8C0, 0x0104A8C0) == 0x0A01A8C0);  // con red de la casa: esa
  CHECK(probeAnswerIp(0, 0x0104A8C0) == 0x0104A8C0);           // solo AP: la del AP
  CHECK(probeAnswerIp(0, 0) == 0);                             // sin ninguna: 0 (no se contesta)
}

// Fase 2, corrección: los contadores del aviso "sin internet" no pierden timeouts con varias tareas.
static void testUpstreamWatch() {
  using namespace siblock;
  {
    UpstreamWatch w;
    w.noteAnswered(1000);
    w.noteTimeout(1100);
    w.noteTimeout(1200);
    CHECK(!w.looksDown(1300));
    w.noteTimeout(1300);
    CHECK(w.looksDown(1400));
    w.noteAnswered(1500);
    CHECK(w.timeoutsSinceAnswer() == 0);
    CHECK(!w.looksDown(1600));
  }
  {
    UpstreamWatch w;
    constexpr int kThreads = 4, kEach = 20000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; t++)
      ts.emplace_back([&w] {
        for (int i = 0; i < kEach; i++) w.noteTimeout(uint32_t(i));
      });
    for (auto& t : ts) t.join();
    CHECK(w.timeoutsSinceAnswer() == uint32_t(kThreads * kEach));
  }
}

// ---------- Fase 3: nube (número de instalación, versiones, cuerpos, calendario) ----------

static void testCloud() {
  const std::string id = "000102030405060708090a0b0c0d0e0f";
  // validInstallId
  CHECK(validInstallId(id));
  CHECK(validInstallId("ffffffffffffffffffffffffffffffff"));
  CHECK(!validInstallId(id.substr(0, 31)));
  CHECK(!validInstallId(id + "0"));
  CHECK(!validInstallId("000102030405060708090A0B0C0D0E0F"));
  CHECK(!validInstallId("g00102030405060708090a0b0c0d0e0f"));
  CHECK(!validInstallId(""));
  CHECK(!validInstallId(std::string("0001020304050607\0" "8090a0b0c0d0e0f", 32)));
  // makeInstallId
  uint8_t rnd[16];
  for (int i = 0; i < 16; i++) rnd[i] = uint8_t(i);
  CHECK(makeInstallId(rnd) == id);
  // parseSemver
  uint32_t v[3];
  CHECK(parseSemver("v0.0.0", v) && v[0] == 0 && v[1] == 0 && v[2] == 0);
  CHECK(parseSemver("v1.20.3", v) && v[0] == 1 && v[1] == 20 && v[2] == 3);
  CHECK(parseSemver("v9999.0.1", v) && v[0] == 9999 && v[2] == 1);
  for (const char* bad : {"1.2.3", "v1.2", "v1.2.3.4", "v1.2.x", "v10000.0.0", "v1.2.3 ", "dev", "v01.2.3", "", "v",
                          "v.1.2", "v1..2", " v1.2.3", "v-1.2.3", "v1.2.3\n"})
    CHECK(!parseSemver(bad, v));
  CHECK(!parseSemver(std::string("v1.2.3\0", 7), v));
  // isNewer
  CHECK(isNewer("v1.2.10", "v1.2.9"));
  CHECK(isNewer("v2.0.0", "v1.99.99"));
  CHECK(!isNewer("v1.2.3", "v1.2.3"));
  CHECK(!isNewer("v1.2.2", "v1.2.3"));
  CHECK(!isNewer("v2.0.0", "dev"));
  CHECK(!isNewer("basura", "v1.0.0"));
  // parseVersionJson
  std::string ver;
  const char* j1 = "{\"version\":\"v1.2.3\",\"url\":\"x\"}";
  CHECK(parseVersionJson(j1, strlen(j1), ver) && ver == "v1.2.3");
  const char* j2 = "{\n  \"url\" : \"https://x/\",\n  \"version\" :\t \"v1.2.3\"\n}\n";
  ver.clear();
  CHECK(parseVersionJson(j2, strlen(j2), ver) && ver == "v1.2.3");
  const char* j3 = "{\"url\":\"version\"}";
  CHECK(!parseVersionJson(j3, strlen(j3), ver));
  const char* j4 = "{\"version\":1}";
  CHECK(!parseVersionJson(j4, strlen(j4), ver));
  const char* j5 = "{\"version\":\"v1.2.3";
  CHECK(!parseVersionJson(j5, strlen(j5), ver));
  const char* j6 = "{\"version\":\"dev\"}";
  CHECK(!parseVersionJson(j6, strlen(j6), ver));
  CHECK(!parseVersionJson(nullptr, 0, ver));
  std::string big = std::string("{\"version\":\"v1.2.3\",\"x\":\"") + std::string(1025, 'a') + "\"}";
  big.resize(1025);
  CHECK(!parseVersionJson(big.data(), big.size(), ver));
  std::string ok1024 = std::string("{\"version\":\"v1.2.3\"}") + std::string(1024 - 20, ' ');
  CHECK(ok1024.size() == 1024 && parseVersionJson(ok1024.data(), ok1024.size(), ver));
  // validReportDomain
  CHECK(validReportDomain("ads.ejemplo.com"));
  CHECK(validReportDomain("a-b.c0"));
  for (const char* bad : {"ejemplo", "-a.com", "a-.com", "a..com", "ADS.com", "a_b.com", "a.com.", ".a.com", "", "a.",
                          "a b.com", "a.c-"})
    CHECK(!validReportDomain(bad));
  std::string lab63(63, 'a'), lab64(64, 'a');
  CHECK(validReportDomain(lab63 + ".com"));
  CHECK(!validReportDomain(lab64 + ".com"));
  std::string d253 = lab63 + "." + lab63 + "." + lab63 + "." + std::string(61, 'b');  // 253
  CHECK(d253.size() == 253 && validReportDomain(d253));
  CHECK(!validReportDomain(d253 + "b"));
  // listas del contrato
  CHECK(kFabricantesCount == 10 && kMenusCount == 4 && kMotivosCount == 4);
  CHECK(inList(kFabricantes, kFabricantesCount, "huawei") && inList(kFabricantes, kFabricantesCount, "no-se"));
  CHECK(!inList(kFabricantes, kFabricantesCount, "linksys") && !inList(kFabricantes, kFabricantesCount, ""));
  CHECK(inList(kMenus, kMenusCount, "internet_wan") && !inList(kMenus, kMenusCount, "wan"));
  CHECK(inList(kMotivos, kMotivosCount, "otro") && !inList(kMotivos, kMotivosCount, "otra"));
  // cuerpos
  CHECK(pingJson(id, "v1.2.3", 7) == "{\"i\":\"" + id + "\",\"v\":\"v1.2.3\",\"b\":7}");
  CHECK(pingJson(id, "dev", 0) == "{\"i\":\"" + id + "\",\"v\":\"dev\",\"b\":0}");
  CHECK(pingJson(id, "v1", 7).empty());
  CHECK(pingJson(id.substr(1), "v1.2.3", 7).empty());
  CHECK(pingJson(id, "v1.2.3", 100000001).empty());
  CHECK(modemReportJson(id, "huawei", true, "internet_wan") ==
        "{\"i\":\"" + id + "\",\"origen\":\"panel\",\"fabricante\":\"huawei\",\"dns_ok\":true,\"menu\":\"internet_wan\"}");
  CHECK(modemReportJson(id, "zte", false, "otro") ==
        "{\"i\":\"" + id + "\",\"origen\":\"panel\",\"fabricante\":\"zte\",\"dns_ok\":false,\"menu\":\"otro\"}");
  CHECK(modemReportJson(id, "linksys", true, "internet_wan").empty());
  CHECK(modemReportJson(id, "huawei", true, "").empty());
  CHECK(modemReportJson("abc", "huawei", true, "internet_wan").empty());
  CHECK(blockedReportJson(id, "ads.ejemplo.com", "otro") ==
        "{\"i\":\"" + id + "\",\"origen\":\"panel\",\"dominio\":\"ads.ejemplo.com\",\"motivo\":\"otro\"}");
  CHECK(blockedReportJson(id, "ejemplo", "otro").empty());
  CHECK(blockedReportJson(id, "ads.ejemplo.com", "otra").empty());
  CHECK(blockedReportJson(id, "a\".com", "otro").empty());
  // blockedLastHours
  UsageHistory h;
  const uint32_t hNow = 500000;
  for (uint32_t x = hNow - 30; x <= hNow; x++) h.add(x, true);
  h.add(hNow, false);  // una consulta no bloqueada no cuenta
  CHECK(blockedLastHours(h, hNow, 24) == 24);
  CHECK(blockedLastHours(h, hNow, 168) == 31);
  CHECK(blockedLastHours(h, hNow, 500) == 31);
  CHECK(blockedLastHours(h, 0, 24) == 0);
  // CloudSchedule
  CloudSchedule s;
  CHECK(!s.due(0));  // sin start no vence nunca
  s.start(0);
  CHECK(!s.due(59999));
  CHECK(s.due(60000));
  s.done(true, 60000, 5000);
  CHECK(!s.due(60000 + 86400000 + 4999));
  CHECK(s.due(60000 + 86400000 + 5000));
  s.done(true, 0, 3600000 + 7);  // jitter % 1 h
  CHECK(!s.due(86400000 + 6) && s.due(86400000 + 7));
  CloudSchedule f;
  f.start(0);
  uint32_t t = 60000;
  f.done(false, t, 0);
  CHECK(!f.due(t + 3600000 - 1) && f.due(t + 3600000));
  f.done(false, t, 0);
  CHECK(!f.due(t + 7200000 - 1) && f.due(t + 7200000));
  f.done(false, t, 0);
  CHECK(!f.due(t + 14400000 - 1) && f.due(t + 14400000));
  for (int i = 0; i < 7; i++) f.done(false, t, 0);  // 10 fallos
  CHECK(!f.due(t + 86400000 - 1) && f.due(t + 86400000));
  for (int i = 0; i < 300; i++) f.done(false, t, 0);  // sin desbordar el contador
  CHECK(!f.due(t + 86400000 - 1) && f.due(t + 86400000));
  f.done(true, t, 0);  // un éxito vuelve a 24 h y borra los fallos
  f.done(false, t, 0);
  CHECK(!f.due(t + 3600000 - 1) && f.due(t + 3600000));
  CloudSchedule w;
  w.start(0xFFFFFFF0u);
  CHECK(!w.due(0xFFFFFFF0u));
  CHECK(!w.due(0xFFFFFFF0u + 59999u));
  CHECK(w.due(0xFFFFFFF0u + 60000u));
  w.done(true, 0xFFFFFFF0u + 60000u, 0);
  CHECK(!w.due(0xFFFFFFF0u + 60000u + 86399999u));
  CHECK(w.due(0xFFFFFFF0u + 60000u + 86400000u));
}

// ---------- Fase 5, Tarea 1: aparatos, nombres, horarios y grupos ----------

static DeviceId idOf(uint8_t last) {
  uint8_t salt[16], mac[6] = {2, 0, 0, 0, 0, last};
  for (int i = 0; i < 16; i++) salt[i] = uint8_t(i);
  return deviceIdFor(salt, mac);
}

static void testFamily() {
  CHECK(deviceIdHex(idOf(5)) == "9d2cbeaf31115773");
  DeviceId d{};
  CHECK(parseDeviceId("9D2CBEAF31115773", 16, d) && d == idOf(5));
  CHECK(parseDeviceId("9d2cbeaf31115773", 16, d) && deviceIdHex(d) == "9d2cbeaf31115773");
  CHECK(!parseDeviceId("9d2cbeaf3111577", 15, d));
  CHECK(!parseDeviceId("9d2cbeaf311157731", 17, d));
  CHECK(!parseDeviceId("9d2cbeaf3111577g", 16, d));
  CHECK(!(idOf(5) == idOf(6)));

  CHECK(validDeviceName("Tele sala"));
  CHECK(validDeviceName("Cel de mam\xc3\xa1"));
  CHECK(validDeviceName("\xf0\x9f\x93\xba"));
  CHECK(validDeviceName(std::string(32, 'x')));
  CHECK(!validDeviceName(""));
  CHECK(!validDeviceName(std::string(33, 'x')));
  CHECK(!validDeviceName(" a"));
  CHECK(!validDeviceName("a "));
  CHECK(!validDeviceName("a\tb"));
  CHECK(!validDeviceName("a\x7f"));
  CHECK(!validDeviceName("\xc3"));
  CHECK(!validDeviceName(std::string("a\0b", 3)));
  CHECK(!validDeviceName("\xed\xa0\x80"));  // sustituto
  CHECK(!validDeviceName("\xc0\xaf"));      // forma larga

  CHECK(localMinuteOfWeek(1795219200, 0) == 5 * 1440);
  CHECK(localMinuteOfWeek(1795419000, 0) == 0 * 1440 + 450);
  CHECK(localMinuteOfWeek(1795419000, -360) == 0 * 1440 + 90);
  CHECK(localMinuteOfWeek(1795219200, -360) == 4 * 1440 + 1080);
  CHECK(localMinuteOfWeek(1699999999, 0) == -1);
  CHECK(localMinuteOfWeek(1795219200, 840) == 5 * 1440 + 840);
  CHECK(validTzMinutes(-720) && validTzMinutes(840) && validTzMinutes(0));
  CHECK(!validTzMinutes(-721) && !validTzMinutes(841));

  Schedule school{1, 1, 0b0011111, 480, 840};
  CHECK(validSchedule(school));
  CHECK(scheduleActive(school, 0 * 1440 + 480));
  CHECK(!scheduleActive(school, 0 * 1440 + 840));
  CHECK(!scheduleActive(school, 0 * 1440 + 479));
  CHECK(!scheduleActive(school, 5 * 1440 + 600));
  CHECK(!scheduleActive(school, -1));
  Schedule night{1, 4, 0b0000001, 1260, 420};
  CHECK(scheduleActive(night, 0 * 1440 + 1320));
  CHECK(scheduleActive(night, 1 * 1440 + 419));
  CHECK(!scheduleActive(night, 1 * 1440 + 420));
  CHECK(!scheduleActive(night, 0 * 1440 + 360));
  Schedule sunday{1, 4, 0b1000000, 1260, 420};  // domingo noche -> lunes temprano
  CHECK(scheduleActive(sunday, 0 * 1440 + 100));
  CHECK(scheduleActive(sunday, 6 * 1440 + 1300));
  CHECK(!validSchedule({0, 1, 1, 0, 10}));
  CHECK(!validSchedule({1, 0, 1, 0, 10}));
  CHECK(!validSchedule({1, 8, 1, 0, 10}));
  CHECK(!validSchedule({1, 1, 0, 0, 10}));
  CHECK(!validSchedule({1, 1, 128, 0, 10}));
  CHECK(!validSchedule({1, 1, 1, 0, 1440}));
  CHECK(!validSchedule({1, 1, 1, 10, 10}));
  CHECK(validSchedule({0x80000000u, 7, 127, 1439, 0}));

  CHECK(inGroups("www.instagram.com", kGroupSocial));
  CHECK(inGroups("instagram.com", kGroupSocial));
  CHECK(!inGroups("instagram.com.evil.net", kGroupSocial));
  CHECK(!inGroups("roblox.com", kGroupSocial));
  CHECK(inGroups("roblox.com", kGroupGames));
  CHECK(inGroups("roblox.com", kGroupSocial | kGroupGames));
  CHECK(inGroups("wikipedia.org", kGroupAll));
  CHECK(!inGroups("wikipedia.org", kGroupSocial | kGroupGames));
  CHECK(!inGroups("com", kGroupSocial));
  CHECK(inGroups("cdn.discord.gg", kGroupSocial));
  for (const char* n : {"facebook.com", "fbcdn.net", "instagram.com", "cdninstagram.com", "tiktok.com",
                        "tiktokcdn.com", "tiktokv.com", "snapchat.com", "x.com", "twitter.com", "twimg.com",
                        "threads.net", "pinterest.com", "reddit.com", "discord.com", "discord.gg"})
    CHECK(inGroups(n, kGroupSocial) && !inGroups(n, kGroupGames));
  for (const char* n : {"roblox.com", "rbxcdn.com", "epicgames.com", "fortnite.com", "minecraft.net",
                        "xboxlive.com", "playstation.net", "steampowered.com", "steamcommunity.com", "supercell.com"})
    CHECK(inGroups(n, kGroupGames) && !inGroups(n, kGroupSocial));
  CHECK(!inGroups("ax.com", kGroupSocial) && !inGroups("tiktokc.com", kGroupSocial));

  // Listas completas (AdGuard HostlistsRegistry + agregados a mano, ver docs/grupos.md).
  {
    size_t nSocial = 0, nGames = 0;
    CHECK(groupListsSortedForTest(nSocial, nGames));  // la búsqueda binaria lo necesita
    CHECK(nSocial == 74 && nGames == 91);
  }
  for (const char* n : {"v16-webapp.tiktok.com", "bsky.app", "www.threads.com", "t.co", "i.redd.it",
                        "static-cdn.jtvnw.net", "gateway.discord.gg", "scontent.cdninstagram.com", "pin.it",
                        "pinterest.com.mx", "impala-media-production.s3.amazonaws.com"})
    CHECK(inGroups(n, kGroupSocial) && !inGroups(n, kGroupGames));
  for (const char* n : {"apis.roblox.com", "ctest.cdn.nintendo.net", "among.us", "s.team", "easy.ac",
                        "game.brawlstars.com", "sdk.hoyoverse.com", "xbox.com", "user.auth.xboxlive.com",
                        "pokemongolive.com", "kitkagames.com", "stumbleguys.com", "xboxservices.com"})
    CHECK(inGroups(n, kGroupGames) && !inGroups(n, kGroupSocial));
  // Lo que nunca se corta: cuentas, Google, mensajes, YouTube y CDNs compartidos.
  for (const char* n : {"login.live.com", "live.com", "microsoft.com", "www.google.com", "googleapis.com",
                        "gstatic.com", "apple.com", "web.whatsapp.com", "whatsapp.net", "messenger.com",
                        "www.youtube.com", "i.ytimg.com", "s3.amazonaws.com", "other.s3.amazonaws.com",
                        "cloudfront.net", "akamaihd.net", "co", "team", "nintendo.org", "xbox.co"})
    CHECK(!inGroups(n, kGroupSocial | kGroupGames));

  Family f;
  for (int i = 0; i < 32; i++) CHECK(f.setName(idOf(uint8_t(i)), "A" + std::to_string(i)));
  CHECK(!f.setName(idOf(200), "sobra"));
  CHECK(f.setName(idOf(3), "Tele sala"));  // renombrar no gasta casilla
  CHECK(f.slotOf(idOf(3)) == 3 && std::string(f.nameAt(3)) == "Tele sala");
  CHECK(!f.setName(idOf(3), " mal"));
  CHECK(std::string(f.nameAt(3)) == "Tele sala");
  CHECK(f.slotOf(idOf(200)) == -1);
  f.clear();
  CHECK(f.slotOf(idOf(3)) == -1 && f.nameAt(0) == nullptr && !f.used(0));
  CHECK(f.setName(idOf(10), "Tele"));
  CHECK(f.setName(idOf(11), "Cel"));
  CHECK(f.slotOf(idOf(10)) == 0 && f.slotOf(idOf(11)) == 1 && f.idAt(1) == idOf(11));
  CHECK(!f.setSchedule(-1, {0b100, 1, 127, 0, 10}));  // casilla 2 libre
  CHECK(f.setSchedule(-1, {0b11, 4, 127, 0, 10}));
  CHECK(f.setSchedule(-1, {0b01, 1, 127, 100, 200}));
  CHECK(f.scheduleCount() == 2);
  CHECK(!f.setSchedule(5, {0b01, 1, 127, 100, 200}));  // índice que no existe
  CHECK(!f.setSchedule(-1, {0b01, 1, 127, 100, 100}));  // inválido
  CHECK(f.setSchedule(1, {0b01, 2, 127, 100, 300}));
  CHECK(f.scheduleAt(1).groups == 2 && f.scheduleAt(1).end == 300);
  // blockedBySchedule: casilla, grupo y hora
  CHECK(f.blockedBySchedule(0, "example.com", 5));
  CHECK(f.blockedBySchedule(1, "example.com", 5));
  CHECK(!f.blockedBySchedule(1, "example.com", 10));
  CHECK(!f.blockedBySchedule(1, "example.com", -1));
  CHECK(f.blockedBySchedule(0, "roblox.com", 150));
  CHECK(!f.blockedBySchedule(0, "tiktok.com", 150));
  CHECK(!f.blockedBySchedule(1, "roblox.com", 150));
  CHECK(!f.blockedBySchedule(-1, "roblox.com", 150));
  CHECK(!f.blockedBySchedule(5, "roblox.com", 150));
  // activeGroups: grupos de los horarios activos de esa casilla ahora
  CHECK(f.activeGroups(0, 5) == 4);
  CHECK(f.activeGroups(0, 150) == 2);
  CHECK(f.activeGroups(1, 150) == 0);
  CHECK(f.activeGroups(0, -1) == 0 && f.activeGroups(-1, 5) == 0 && f.activeGroups(5, 5) == 0);
  // forget quita el bit y borra el horario que queda vacío
  CHECK(f.forget(idOf(10)));
  CHECK(!f.forget(idOf(10)));
  CHECK(f.scheduleCount() == 1 && f.scheduleAt(0).devices == 0b10);
  CHECK(!f.used(0) && f.used(1));
  CHECK(f.setName(idOf(12), "Nuevo") && f.slotOf(idOf(12)) == 0);  // reusa la casilla
  CHECK(!f.blockedBySchedule(0, "example.com", 5));  // el nuevo no hereda horarios
  CHECK(f.removeSchedule(0) && f.scheduleCount() == 0 && !f.removeSchedule(0));
  for (int i = 0; i < 8; i++) CHECK(f.setSchedule(-1, {0b01, 1, 127, uint16_t(i), 500}));
  CHECK(!f.setSchedule(-1, {0b01, 1, 127, 9, 500}));  // 9.º horario
  CHECK(f.removeSchedule(3) && f.scheduleCount() == 7 && f.scheduleAt(3).start == 4);
  CHECK(f.forget(idOf(12)) && f.scheduleCount() == 0);
  // setSlot (para leer el archivo)
  Family g;
  CHECK(g.setSlot(5, idOf(1), "Uno"));
  CHECK(!g.setSlot(5, idOf(2), "Dos"));    // casilla usada
  CHECK(!g.setSlot(6, idOf(1), "Otra"));   // huella repetida
  CHECK(!g.setSlot(32, idOf(2), "Dos"));
  CHECK(!g.setSlot(-1, idOf(2), "Dos"));
  CHECK(!g.setSlot(7, idOf(2), ""));
  CHECK(g.slotOf(idOf(1)) == 5 && g.usedMask() == (1u << 5));
}

// ---------- Fase 5, Tarea 2: respaldo y /familia.txt ----------

// Pasa el texto línea por línea (como lo hará la placa) y termina con finish().
static bool readText(BackupReader& r, const std::string& text) {
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    if (!r.line(text.data() + pos, end - pos)) return false;
    pos = end + 1;
  }
  return r.finish();
}

static std::string replaceLine(std::string text, const std::string& from, const std::string& to) {
  size_t at = text.find(from);
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

// El error esperado y en qué línea.
static void badBackup(const std::string& text, bool full, int line, const char* msgPart = nullptr) {
  BackupReader r(full);
  bool ok = readText(r, text);
  CHECK(!ok);
  if (r.errorLine() != line) std::printf("  linea %d (esperada %d): %s\n", r.errorLine(), line, r.error());
  CHECK(r.errorLine() == line);
  CHECK(r.error() && *r.error());
  if (msgPart) CHECK(std::string(r.error()).find(msgPart) != std::string::npos);
  CHECK(!r.line("fin", 3));  // después de un error todo es falso
}

static void testBackup() {
  Family f;
  for (int i = 0; i < 16; i++) f.salt[i] = uint8_t(i);
  f.hasSalt = true;
  f.tz = -360;
  CHECK(f.setName(idOf(2), "Tele"));
  CHECK(f.setName(idOf(7), "Cel de mam\xc3\xa1 \xf0\x9f\x93\xb1"));
  CHECK(f.setSchedule(-1, {0b11, kGroupSocial | kGroupGames, 0b0011111, 480, 840}));
  CHECK(f.setSchedule(-1, {0b10, kGroupAll, 127, 1260, 420}));
  std::vector<std::string> block = {"ads.example.com", "tracker.net"}, allow = {"ok.example.org"};
  std::string text = writeBackup(5, true, block, allow, f);
  std::string expected =
      "SIBLOCK-RESPALDO 1\nlistas 5\nrelay 1\nzona -360\nsal 000102030405060708090a0b0c0d0e0f\n"
      "bloquear ads.example.com\nbloquear tracker.net\npermitir ok.example.org\n"
      "aparato 0 " + deviceIdHex(idOf(2)) + " Tele\n"
      "aparato 1 " + deviceIdHex(idOf(7)) + " Cel de mam\xc3\xa1 \xf0\x9f\x93\xb1\n"
      "horario 00000003 3 31 480 840\nhorario 00000002 4 127 1260 420\nfin\n";
  CHECK(text == expected);
  {
    BackupReader r(true);
    CHECK(readText(r, text));
    const Backup& b = r.result();
    CHECK(b.full && b.lists == 5 && b.relay);
    CHECK(b.block == block && b.allow == allow);
    CHECK(b.family.tz == -360 && b.family.hasSalt && std::memcmp(b.family.salt, f.salt, 16) == 0);
    CHECK(b.family.slotOf(idOf(7)) == 1 && std::string(b.family.nameAt(1)) == "Cel de mam\xc3\xa1 \xf0\x9f\x93\xb1");
    CHECK(b.family.scheduleCount() == 2 && b.family.scheduleAt(1).start == 1260 && b.family.scheduleAt(0).days == 31);
    CHECK(writeBackup(b.lists, b.relay, b.block, b.allow, b.family) == text);
  }
  {  // "\r\n"
    std::string crlf;
    for (char c : text) {
      if (c == '\n') crlf += '\r';
      crlf += c;
    }
    BackupReader r(true);
    CHECK(readText(r, crlf));
    CHECK(r.result().family.scheduleCount() == 2);
  }
  // /familia.txt
  std::string fam = writeFamily(f);
  CHECK(fam.rfind("SIBLOCK-FAMILIA 1\nzona -360\nsal ", 0) == 0);
  CHECK(fam.find("listas") == std::string::npos && fam.find("relay") == std::string::npos);
  {
    BackupReader r(false);
    CHECK(readText(r, fam));
    CHECK(!r.result().full && r.result().family.slotOf(idOf(2)) == 0);
    CHECK(writeFamily(r.result().family) == fam);
  }
  {  // casillas con huecos se conservan
    Family g;
    g.hasSalt = true;
    CHECK(g.setSlot(9, idOf(1), "Nueve"));
    BackupReader r(false);
    CHECK(readText(r, writeFamily(g)));
    CHECK(r.result().family.slotOf(idOf(1)) == 9);
  }
  {  // sin aparatos ni reglas
    Family g;
    g.hasSalt = true;
    BackupReader r(true);
    CHECK(readText(r, writeBackup(1, false, {}, {}, g)));
    CHECK(r.result().lists == 1 && !r.result().relay && r.result().block.empty());
  }

  // Errores: línea donde falla. text: 1 cabecera, 2 listas, 3 relay, 4 zona, 5 sal, 6-7 bloquear,
  // 8 permitir, 9-10 aparato, 11-12 horario, 13 fin.
  std::string noEnd = text.substr(0, text.size() - 4);
  badBackup(noEnd, true, 0, "cortado");
  badBackup(replaceLine(text, "SIBLOCK-RESPALDO 1", "SIBLOCK-RESPALDO 2"), true, 1);
  badBackup(replaceLine(text, "SIBLOCK-RESPALDO 1", "SIBLOCK-FAMILIA 1"), true, 1);
  badBackup(fam, true, 1);
  badBackup(replaceLine(text, "bloquear tracker.net", "bloquear " + std::string(291, 'a') + ".net"), true, 7,
            "larga");
  {  // un dominio largo (253 bytes de línea) se acepta
    std::string l = "permitir " + std::string(240, 'a') + ".net";
    BackupReader r(true);
    CHECK(readText(r, replaceLine(text, "permitir ok.example.org", l)));
  }
  badBackup(replaceLine(text, "listas 5", "listas 2"), true, 2);
  badBackup(replaceLine(text, "listas 5", "listas 9"), true, 2);
  badBackup(replaceLine(text, "listas 5", "listas x"), true, 2);
  badBackup(replaceLine(text, "relay 1", "relay 2"), true, 3);
  badBackup(replaceLine(text, "zona -360", "zona 900"), true, 4);
  badBackup(replaceLine(text, "zona -360", "zona -"), true, 4);
  badBackup(replaceLine(text, "sal 000102030405060708090a0b0c0d0e0f", "sal 000102030405060708090a0b0c0d0e0"), true, 5);
  badBackup(replaceLine(text, "aparato 0 ", "aparato 32 "), true, 9);
  badBackup(replaceLine(text, "aparato 1 ", "aparato 0 "), true, 10);
  badBackup(replaceLine(text, "aparato 0 " + deviceIdHex(idOf(2)), "aparato 0 " + deviceIdHex(idOf(7))), true, 10);
  badBackup(replaceLine(text, "aparato 0 " + deviceIdHex(idOf(2)), "aparato 0 zz"), true, 9);
  badBackup(replaceLine(text, "horario 00000002", "horario 00000004"), true, 12, "aparato");
  badBackup(replaceLine(text, "horario 00000002 4 127 1260 420", "horario 00000002 4 127 420 420"), true, 12);
  badBackup(replaceLine(text, "horario 00000002 4 127", "horario 00000002 8 127"), true, 12);
  badBackup(replaceLine(text, "horario 00000002 4 127", "horario 00000002 4 128"), true, 12);
  badBackup(replaceLine(text, "1260 420", "1260 1440"), true, 12);
  badBackup(replaceLine(text, "1260 420", "1260 420 9"), true, 12);
  badBackup(replaceLine(text, "bloquear tracker.net", "bloquear no_es_dominio"), true, 7);
  badBackup(replaceLine(text, "bloquear tracker.net", "bloquear ads.example.com"), true, 7);
  badBackup(replaceLine(text, " Tele\n", std::string(" Te\0le\n", 7)), true, 9);
  badBackup(replaceLine(text, " Tele\n", " Te\x01le\n"), true, 9);
  badBackup(replaceLine(text, " Tele\n", " \n"), true, 9);
  badBackup(replaceLine(text, "relay 1\n", "relay 1\n\n"), true, 4);
  badBackup(replaceLine(text, "relay 1\n", "relay 1\nrelay 0\n"), true, 4);
  badBackup(replaceLine(text, "relay 1\n", "relay 1\nhola 1\n"), true, 4, "no se entiende");
  // Telegram (token y chat) nunca va en el respaldo ni se acepta al subirlo.
  CHECK(text.find("telegram") == std::string::npos && text.find("token") == std::string::npos &&
        text.find("chat") == std::string::npos);
  badBackup(replaceLine(text, "relay 1\n", "relay 1\ntelegram 123456:" + std::string(35, 'A') + "\n"), true, 4,
            "no se entiende");
  badBackup(replaceLine(text, "relay 1\n", "relay 1\ntelegram_chat 123456789\n"), true, 4, "no se entiende");
  badBackup(text + "zona 0\n", true, 14);
  badBackup("zona 0\n" + text, true, 1);
  badBackup(replaceLine(text, "listas 5\n", ""), true, 0);
  badBackup(replaceLine(text, "sal 000102030405060708090a0b0c0d0e0f\n", ""), true, 0);
  badBackup(replaceLine(fam, "zona -360\n", "zona -360\nlistas 1\n"), false, 3);
  badBackup(replaceLine(fam, "zona -360\n", "zona -360\nbloquear a.com\n"), false, 3);
  badBackup(replaceLine(fam, "zona -360\n", "zona -360\nrelay 0\n"), false, 3);
  badBackup("", true, 0);
  {  // 201 reglas
    std::string many = "SIBLOCK-RESPALDO 1\nlistas 1\n";
    for (int i = 0; i < 201; i++) many += "bloquear d" + std::to_string(i) + ".com\n";
    many += "sal 000102030405060708090a0b0c0d0e0f\nfin\n";
    badBackup(many, true, 203, "demasiadas reglas");
    std::string ok200 = "SIBLOCK-RESPALDO 1\nlistas 1\n";
    for (int i = 0; i < 200; i++) ok200 += (i % 2 ? "bloquear d" : "permitir d") + std::to_string(i) + ".com\n";
    ok200 += "sal 000102030405060708090a0b0c0d0e0f\nfin\n";
    BackupReader r(true);
    CHECK(readText(r, ok200));
    CHECK(r.result().block.size() + r.result().allow.size() == 200);
  }
  {  // 9 horarios
    Family g;
    g.hasSalt = true;
    CHECK(g.setName(idOf(1), "Uno"));
    std::string t = writeFamily(g);
    std::string add;
    for (int i = 0; i < 9; i++) add += "horario 00000001 1 1 " + std::to_string(i) + " 600\n";
    badBackup(replaceLine(t, "fin\n", add + "fin\n"), false, 4 + 9, "horarios");
  }
  {  // demasiado grande
    std::string big = "SIBLOCK-RESPALDO 1\nlistas 1\n";
    for (int i = 0; big.size() <= kBackupMaxBytes; i++) big += "bloquear " + std::string(150, 'b') + std::to_string(i) + ".com\n";
    BackupReader r(true);
    CHECK(!readText(r, big));
    CHECK(std::string(r.error()).find("grande") != std::string::npos);
  }
  {  // demasiado grande visto antes de leer (familia.txt o restore.tmp): sin "línea N"
    BackupReader r(true);
    CHECK(!r.failTooBig());
    CHECK(r.errorLine() == 0);
    CHECK(!strcmp(r.error(), "el archivo es demasiado grande"));
    CHECK(!r.line("SIBLOCK-RESPALDO 1", 18));  // y queda en error
    CHECK(!r.finish());
  }
  {  // 200 reglas largas pasan el tope: el panel no debe entregar un respaldo que no se puede restaurar
    std::vector<std::string> block;
    for (int i = 0; i < 200; i++) block.push_back(std::string(200, 'a') + std::to_string(i) + ".com");
    Family g;
    g.hasSalt = true;
    CHECK(writeBackup(1, false, block, {}, g).size() > kBackupMaxBytes);
  }
}

// ---------- Fase 5, Tarea 3: listas, LED de estado y ahorro ----------

static void testLists() {
  const char* u[8];
  const char* g1 = "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts";
  const char* g2 = "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/light-onlydomains.txt";
  CHECK(listUrls(1, u, 8) == 2 && !strcmp(u[0], g1) && !strcmp(u[1], g2));
  size_t n = listUrls(7, u, 8);
  CHECK(n == 4);
  // Estafas, español y al final la general: si no cabe todo, se cortan anuncios de la general.
  CHECK(n == 4 && strstr(u[0], "hagezi") && strstr(u[1], "easylistspanish") && !strcmp(u[2], g1) && !strcmp(u[3], g2));
  CHECK(listUrls(3, u, 8) == 3 && strcmp(u[0], g1) && !strcmp(u[1], g1));
  CHECK(listUrls(5, u, 8) == 3 && strstr(u[0], "easylistspanish") && !strcmp(u[1], g1) && !strcmp(u[2], g2));
  CHECK(listUrls(2, u, 8) == 2 && !strcmp(u[0], g1));
  CHECK(listUrls(0, u, 8) == 2 && listUrls(9, u, 8) == 2);
  CHECK(listUrls(7, u, 2) == 2);  // nunca más de max
  for (size_t i = 0; i < listUrls(7, u, 8); i++) CHECK(strncmp(u[i], "https://", 8) == 0);
  CHECK(!validListMask(0) && !validListMask(2) && !validListMask(8) && !validListMask(9) && !validListMask(4));
  CHECK(validListMask(1) && validListMask(3) && validListMask(5) && validListMask(7));
  CHECK(listsOffered() == 7);
  CHECK(kListsDefault == kListGeneral);

  CHECK(acceptListSize(150000, 160000, false, 0, false));
  CHECK(!acceptListSize(70000, 160000, false, 0, false));
  CHECK(acceptListSize(70000, 160000, false, 0, true));
  // Sin tamaño se pide el 90 % (144 000 de 160 000). El plan decía 145 000 -> no, pero 145 000 es
  // más del 90 %; la regla se movió sin cambiarla.
  CHECK(!acceptListSize(143999, 160000, true, 0, false));
  CHECK(acceptListSize(144000, 160000, true, 0, false));
  CHECK(acceptListSize(145000, 160000, true, 0, false));
  CHECK(acceptListSize(80000, 160000, false, 0, false));
  CHECK(!acceptListSize(79999, 160000, false, 0, false));
  CHECK(acceptListSize(70000, 160000, false, 70500, false));
  CHECK(!acceptListSize(70000, 160000, false, 80000, false));
  CHECK(acceptListSize(0, 1000, false, 0, false));
  CHECK(acceptListSize(10, 500, true, 0, false));
  CHECK(!acceptListSize(10, 1001, false, 0, false));
}

static void testStatusLed() {
  CHECK(ledStateFor(true, false, false, 160000, false) == LedState::kOk);
  CHECK(ledStateFor(true, false, true, 160000, false) == LedState::kUpdating);
  CHECK(ledStateFor(false, true, true, 0, true) == LedState::kUpdating);
  CHECK(ledStateFor(false, false, false, 160000, false) == LedState::kProblem);
  CHECK(ledStateFor(true, true, false, 160000, false) == LedState::kProblem);
  CHECK(ledStateFor(true, false, false, 0, false) == LedState::kProblem);
  CHECK(ledStateFor(true, false, false, 160000, true) == LedState::kProblem);
  for (uint32_t t : {0u, 199u, 200u, 999u, 1000u, 0xFFFFFFFFu}) CHECK(ledOn(LedState::kOk, t));
  CHECK(ledOn(LedState::kUpdating, 0) && ledOn(LedState::kUpdating, 199) && ledOn(LedState::kUpdating, 999));
  CHECK(!ledOn(LedState::kUpdating, 1000) && !ledOn(LedState::kUpdating, 1999) && ledOn(LedState::kUpdating, 2000));
  CHECK(ledOn(LedState::kProblem, 0) && ledOn(LedState::kProblem, 199));
  CHECK(!ledOn(LedState::kProblem, 200) && !ledOn(LedState::kProblem, 399) && ledOn(LedState::kProblem, 400));
  CHECK(ledOn(LedState::kProblem, 800) && !ledOn(LedState::kProblem, 1000));
}

static void testSavings() {
  UsageHistory::Slot slots[UsageHistory::kHours];
  for (auto& sl : slots) sl = {0, 0};
  CHECK(savedBytes(slots, UsageHistory::kHours) == 0);
  CHECK(savedBytes(slots, 0) == 0);
  for (auto& sl : slots) sl = {100, 10};
  CHECK(savedBytes(slots, UsageHistory::kHours) == uint64_t(168) * 10 * 30720);
  for (auto& sl : slots) sl = {0xFFFFFFFFu, 0xFFFFFFFFu};  // sin desbordar
  CHECK(savedBytes(slots, UsageHistory::kHours) == uint64_t(168) * 0xFFFFFFFFu * 30720);
  CHECK(kSavedBytesPerBlocked == 30720);
}

// ---------- Permitidos de la comunidad ----------

static bool parseCommunity(const std::string& body, CommunityList& out, size_t chunk = 0) {
  CommunityParser p;
  if (!chunk) chunk = body.size() ? body.size() : 1;
  for (size_t i = 0; i < body.size(); i += chunk) p.feed(body.data() + i, std::min(chunk, body.size() - i));
  return p.finish(out);
}

static bool parses(const std::string& body) {
  CommunityList l;
  return parseCommunity(body, l);
}

static void testCommunity() {
  // FNV-1a de 64 bits (valores conocidos).
  CHECK(communityHash("", 0) == 0xcbf29ce484222325ULL);
  CHECK(communityHash(std::string("a")) == 0xaf63dc4c8601ec8cULL);

  CommunityList l;
  const std::string ok = "{\"version\": 7, \"dominios\": [\"b.ejemplo.com\", \"a.ejemplo.com\", \"b.ejemplo.com\"]}";
  CHECK(parseCommunity(ok, l));
  CHECK(l.version == 7 && l.updated == 0);
  CHECK(l.keys.size() == 2);  // sin repetir
  CHECK(std::is_sorted(l.keys.begin(), l.keys.end()));
  // De a un byte (como llega por la red) da lo mismo.
  CommunityList l1;
  CHECK(parseCommunity(ok, l1, 1) && l1.keys == l.keys && l1.version == 7);
  CommunityList l3;
  CHECK(parseCommunity(ok, l3, 3) && l3.keys == l.keys);

  // Dominio y subdominios, como «Permitir»; el padre y los parecidos no.
  CHECK(communityAllows(l.keys, "a.ejemplo.com"));
  CHECK(communityAllows(l.keys, "x.y.a.ejemplo.com"));
  CHECK(!communityAllows(l.keys, "ejemplo.com"));
  CHECK(!communityAllows(l.keys, "aa.ejemplo.com"));
  CHECK(!communityAllows(l.keys, "a.ejemplo.com.mx"));
  CHECK(!communityAllows(l.keys, ""));
  CHECK(!communityAllows(std::vector<uint64_t>(), "a.ejemplo.com"));

  // Vacía, con espacios y saltos, con claves de más (se saltan) y en otro orden.
  CHECK(parseCommunity("{\"version\":0,\"dominios\":[]}", l) && l.version == 0 && l.keys.empty());
  CHECK(parses(" \r\n{ \"dominios\" : [ \"a.com\" ] ,\n \"version\" : 4294967295 } \n"));
  CHECK(parses("{\"version\":1,\"nota\":\"hola\",\"n\":-1.5e3,\"x\":true,\"y\":null,\"z\":false,\"dominios\":[]}"));

  // Malformados: todo o nada.
  const char* malos[] = {
      "",
      "[]",
      "{}",
      "{\"version\":1}",
      "{\"dominios\":[]}",
      "{\"version\":1,\"dominios\":[]",                 // sin cerrar
      "{\"version\":1,\"dominios\":[]}x",               // basura después
      "{\"version\":1,\"dominios\":[]}{}",
      "{\"version\":1,\"dominios\":[],}",                // coma de más
      "{\"version\":1,\"dominios\":[\"a.com\",]}",
      "{\"version\":1,\"dominios\":[,\"a.com\"]}",
      "{\"version\":1 \"dominios\":[]}",
      "{\"version\":\"1\",\"dominios\":[]}",            // versión como texto
      "{\"version\":-1,\"dominios\":[]}",
      "{\"version\":01,\"dominios\":[]}",
      "{\"version\":1.5,\"dominios\":[]}",
      "{\"version\":4294967296,\"dominios\":[]}",
      "{\"version\":1,\"version\":2,\"dominios\":[]}",  // clave repetida
      "{\"version\":1,\"dominios\":[],\"dominios\":[]}",
      "{\"version\":1,\"dominios\":\"a.com\"}",
      "{\"version\":1,\"dominios\":[1]}",
      "{\"version\":1,\"dominios\":[[\"a.com\"]]}",
      "{\"version\":1,\"dominios\":[\"A.com\"]}",       // mayúsculas
      "{\"version\":1,\"dominios\":[\"com\"]}",
      "{\"version\":1,\"dominios\":[\"a..com\"]}",
      "{\"version\":1,\"dominios\":[\"-a.com\"]}",
      "{\"version\":1,\"dominios\":[\"a.com \"]}",
      "{\"version\":1,\"dominios\":[\"a\\u002ecom\"]}",  // sin escapes
      "{\"version\":1,\"dominios\":[\"a.com\"],\"x\":{}}",  // otras claves: solo valores sencillos
      "{\"version\":1,\"dominios\":[],\"x\":[]}",
      "{\"version\":1,\"dominios\":[],\"x\":tru}",
      "{\"version\":1,\"dominios\":[],\"x\":abc}",
      "{\"version\":1,\"dominios\":[],\"x\":\"a\nb\"}",  // control dentro de un texto
      "{version:1,\"dominios\":[]}",
  };
  for (const char* m : malos) {
    bool r = parses(m);
    if (r) std::printf("  aceptó: %s\n", m);
    CHECK(!r);
  }
  // Un texto de más de 253 en una clave que se salta vale; como dominio, no.
  CHECK(parses("{\"version\":1,\"dominios\":[],\"x\":\"" + std::string(400, 'a') + "\"}"));
  CHECK(!parses("{\"version\":1,\"dominios\":[\"" + std::string(250, 'a') + ".com\"]}"));

  // Tope de 500 sitios y de 32 KB.
  auto lista = [](size_t n) {
    std::string b = "{\"version\":3,\"dominios\":[";
    for (size_t i = 0; i < n; i++) b += (i ? ",\"s" : "\"s") + std::to_string(i) + ".ejemplo.com\"";
    return b + "]}";
  };
  CHECK(parseCommunity(lista(kCommunityMax), l) && l.keys.size() == kCommunityMax);
  CHECK(!parses(lista(kCommunityMax + 1)));
  std::string relleno = "{\"version\":1,\"dominios\":[],\"x\":\"";
  std::string grande = relleno + std::string(kCommunityMaxBody - relleno.size() - 2, 'a') + "\"}";
  CHECK(grande.size() == kCommunityMaxBody && parses(grande));
  CHECK(!parses(" " + grande));  // un byte más del tope
  // Lo que se lee después de un error no lo arregla.
  CommunityParser p;
  CHECK(!p.feed("{x", 2));
  CHECK(!p.feed("\"version\":1,\"dominios\":[]}", 26));
  CHECK(!p.finish(l));

  // Archivo: ida y vuelta, y roto → se ignora.
  CommunityList a;
  CHECK(parseCommunity(lista(3), a));
  a.updated = 1764000000u;
  std::string f = encodeCommunity(a);
  CHECK(f.size() == 16 + 3 * 8 && f.compare(0, 4, "SBC1") == 0);
  CommunityList b;
  CHECK(decodeCommunity(reinterpret_cast<const uint8_t*>(f.data()), f.size(), b));
  CHECK(b.version == 3 && b.updated == 1764000000u && b.keys == a.keys);
  CommunityList vacia;
  std::string fv = encodeCommunity(vacia);
  CHECK(fv.size() == 16 && decodeCommunity(reinterpret_cast<const uint8_t*>(fv.data()), fv.size(), b) && b.keys.empty());
  auto roto = [&](std::string g) { CommunityList x; return !decodeCommunity(reinterpret_cast<const uint8_t*>(g.data()), g.size(), x); };
  CHECK(roto(f.substr(0, f.size() - 1)));       // cortado
  CHECK(roto(f + "x"));                          // de más
  { std::string g = f; g[3] = '2'; CHECK(roto(g)); }  // otra cabecera
  { std::string g = f; std::swap_ranges(g.begin() + 16, g.begin() + 24, g.begin() + 24); CHECK(roto(g)); }  // desordenado
  { std::string g = f; g.replace(24, 8, g.substr(16, 8)); CHECK(roto(g)); }  // repetido
  { CommunityList big; big.keys.resize(kCommunityMax + 1); for (size_t i = 0; i < big.keys.size(); i++) big.keys[i] = i;
    CHECK(roto(encodeCommunity(big))); }
  CHECK(!decodeCommunity(nullptr, 0, b));
}

// ---------- Avisos por Telegram ----------
static std::string decodeForm(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      o += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

static void testNotify() {
  const std::string tail(35, 'a');
  CHECK(validTelegramToken("12345:" + std::string(30, 'A')));
  CHECK(validTelegramToken("123456789012:" + std::string(64, 'z')));
  CHECK(validTelegramToken("110201543:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw"));
  CHECK(validTelegramToken("123456:aZ09_-" + std::string(30, 'x')));
  CHECK(!validTelegramToken(""));
  CHECK(!validTelegramToken("1234:" + tail));                       // 4 dígitos
  CHECK(!validTelegramToken("1234567890123:" + tail));              // 13 dígitos
  CHECK(!validTelegramToken("12345:" + std::string(29, 'a')));      // corto
  CHECK(!validTelegramToken("12345:" + std::string(65, 'a')));      // largo
  CHECK(!validTelegramToken("12a45:" + tail));
  CHECK(!validTelegramToken("12345" + tail));                       // sin ':'
  CHECK(!validTelegramToken("12345::" + tail));
  CHECK(!validTelegramToken("12345:" + tail + "/"));                // nada de rutas
  CHECK(!validTelegramToken("12345:" + tail + "?"));
  CHECK(!validTelegramToken("12345:" + tail + " "));
  CHECK(!validTelegramToken(" 12345:" + tail));
  CHECK(!validTelegramToken("12345:" + tail + "\n"));
  CHECK(!validTelegramToken("12345:" + tail + "\xc3\xb1"));
  CHECK(!validTelegramToken(std::string("12345:") + std::string(34, 'a') + '\0'));

  CHECK(validTelegramChatId("1"));
  CHECK(validTelegramChatId("123456789"));
  CHECK(validTelegramChatId("-1001234567890"));
  CHECK(validTelegramChatId(std::string(20, '9')));
  CHECK(validTelegramChatId("-" + std::string(20, '9')));
  CHECK(!validTelegramChatId(""));
  CHECK(!validTelegramChatId("-"));
  CHECK(!validTelegramChatId(std::string(21, '9')));
  CHECK(!validTelegramChatId("+123"));
  CHECK(!validTelegramChatId("12 3"));
  CHECK(!validTelegramChatId("--1"));
  CHECK(!validTelegramChatId("@canal"));
  CHECK(!validTelegramChatId("1&text=x"));

  const std::string sp = "\xC2\xA0";
  CHECK(groupThousands(0) == "0");
  CHECK(groupThousands(999) == "999");
  CHECK(groupThousands(1000) == "1" + sp + "000");
  CHECK(groupThousands(152340) == "152" + sp + "340");
  CHECK(groupThousands(4294967295u) == "4" + sp + "294" + sp + "967" + sp + "295");

  CHECK(bootMessage(BootCause::kPower, 152340) ==
        "SiBlock se reinició (corte de luz o la desconectaron). Lista: 152" + sp + "340 sitios.");
  CHECK(bootMessage(BootCause::kNormal, 1) == "SiBlock se reinició (reinicio normal). Lista: 1 sitio.");
  CHECK(bootMessage(BootCause::kCrash, 5).find("falla y se recuperó sola") != std::string::npos);
  CHECK(bootMessage(BootCause::kBrownout, 5).find("bajó la corriente") != std::string::npos);
  CHECK(bootMessage(BootCause::kUpdate, 0) == "SiBlock se reinició (actualización). Todavía no tiene su lista.");
  CHECK(listFailMessage(0, 100) == "SiBlock no pudo actualizar la lista. Sigue usando la que tiene (100 sitios).");
  CHECK(listFailMessage(23, 100).find("desde hace") == std::string::npos);
  CHECK(listFailMessage(24, 100).find("desde hace 1 día.") != std::string::npos);
  CHECK(listFailMessage(49, 2000) ==
        "SiBlock no pudo actualizar la lista desde hace 2 días. Sigue usando la que tiene (2" + sp + "000 sitios).");
  CHECK(listFailMessage(0, 0) == "SiBlock no pudo actualizar la lista y todavía no tiene ninguna: no bloquea nada.");
  CHECK(listOkMessage(152340) == "SiBlock ya pudo actualizar la lista: 152" + sp + "340 sitios.");
  CHECK(weeklyMessage(12345) == "Resumen de la semana: 12" + sp + "345 anuncios y rastreadores bloqueados.");
  CHECK(weeklyMessage(1) == "Resumen de la semana: 1 anuncio o rastreador bloqueado.");
  CHECK(weeklyMessage(0).find("no hubo") != std::string::npos);
  CHECK(testMessage().find("prueba") != std::string::npos);
  // Ningún mensaje lleva más que números y estados: ni '.' de una IP, ni ':' de una MAC.
  for (const std::string& m : {bootMessage(BootCause::kPower, 152340), listFailMessage(50, 1), weeklyMessage(7),
                               listOkMessage(3), testMessage()}) {
    CHECK(m.size() < 200 && m.find("://") == std::string::npos);
    bool ipLike = false;
    for (size_t i = 1; i + 1 < m.size(); i++)
      if (m[i] == '.' && isdigit(uint8_t(m[i - 1])) && isdigit(uint8_t(m[i + 1]))) ipLike = true;
    CHECK(!ipLike);
  }

  CHECK(formEncode("aZ09-_.~") == "aZ09-_.~");
  CHECK(formEncode("a b&c=d+e%f/\n") == "a%20b%26c%3Dd%2Be%25f%2F%0A");
  CHECK(formEncode("\xC3\xB1") == "%C3%B1");
  std::string form = telegramForm("-100123", "Hola & adiós = 100%");
  CHECK(form == "chat_id=-100123&text=Hola%20%26%20adi%C3%B3s%20%3D%20100%25");
  CHECK(decodeForm(form.substr(form.find("text=") + 5)) == "Hola & adiós = 100%");
  CHECK(form.find('&', form.find("text=")) == std::string::npos);  // el texto no puede meter otro campo
  CHECK(telegramForm("12&x=1", "hola").empty());
  CHECK(telegramForm("12", "").empty());

  // Domingo 23 de noviembre de 2025, 20:00 UTC = 1763928000.
  const int64_t sun20 = 1763928000;
  CHECK(weeklySummaryDay(sun20, 0) == int32_t(sun20 / 86400));
  CHECK(weeklySummaryDay(sun20 - 60, 0) == -1);                       // 19:59
  CHECK(weeklySummaryDay(sun20 + 4 * 3600 - 60, 0) == int32_t(sun20 / 86400));  // 23:59
  CHECK(weeklySummaryDay(sun20 + 4 * 3600, 0) == -1);                 // lunes 00:00
  CHECK(weeklySummaryDay(sun20 - 24 * 3600, 0) == -1);                // sábado 20:00
  CHECK(weeklySummaryDay(sun20, -360) == -1);                         // 14:00 en la casa (UTC-6)
  CHECK(weeklySummaryDay(sun20 + 6 * 3600, -360) == int32_t(sun20 / 86400));  // 20:00 en la casa
  CHECK(weeklySummaryDay(sun20 + 6 * 3600, 0) == -1);                 // en UTC ya es lunes
  CHECK(weeklySummaryDay(sun20 - 3 * 3600, 180) > 0);                 // 20:00 en UTC+3
  CHECK(weeklySummaryDay(sun20 + 7 * 86400, 0) == int32_t(sun20 / 86400) + 7);  // la otra semana, otro día
  CHECK(weeklySummaryDay(0, 0) == -1);                                // sin hora
  CHECK(weeklySummaryDay(1699999999, 0) == -1);

  CHECK(telegramResultFor(200) == TelegramResult::kOk);
  CHECK(telegramResultFor(-1) == TelegramResult::kNoNet && telegramResultFor(0) == TelegramResult::kNoNet);
  CHECK(telegramResultFor(401) == TelegramResult::kToken && telegramResultFor(404) == TelegramResult::kToken);
  CHECK(telegramResultFor(400) == TelegramResult::kChat);
  CHECK(telegramResultFor(403) == TelegramResult::kBlocked);
  CHECK(telegramResultFor(429) == TelegramResult::kLimit);
  CHECK(telegramResultFor(500) == TelegramResult::kRejected && telegramResultFor(302) == TelegramResult::kRejected);
  CHECK(std::string(telegramResultName(TelegramResult::kNever)) == "never");
  CHECK(std::string(telegramResultName(TelegramResult::kNoNet)) == "sin_internet");
  CHECK(std::string(telegramResultName(TelegramResult::kBlocked)) == "bloqueado");

  // Tope de 20 por hora, también cuando millis() da la vuelta.
  for (uint32_t base : {0u, 0xFFFFFFFFu - 1800000u}) {
    SendLimiter l;
    for (uint32_t i = 0; i < SendLimiter::kMax; i++) CHECK(l.allow(base + i * 1000));
    CHECK(!l.allow(base + 20000));
    CHECK(!l.allow(base + SendLimiter::kWindowMs - 1));          // el primero todavía cuenta
    CHECK(l.allow(base + SendLimiter::kWindowMs));               // salió de la ventana: hay lugar para uno
    CHECK(!l.allow(base + SendLimiter::kWindowMs + 500));
    CHECK(l.allow(base + SendLimiter::kWindowMs + 1000));        // salió el segundo
  }
  {
    Cooldown c;
    CHECK(c.take(0, 30000));
    CHECK(!c.take(29999, 30000));
    CHECK(c.take(30000, 30000));
    Cooldown d;
    CHECK(d.take(0xFFFFFFFFu - 10, 30000));
    CHECK(!d.take(100, 30000));
    CHECK(d.take(30000, 30000));
  }

  {  // Lista que no se actualiza.
    ListFailWatch w;
    CHECK(w.onResult(false, 0, true) == ListFailWatch::kNone);
    CHECK(w.onResult(false, 60000, true) == ListFailWatch::kNone);
    CHECK(w.onResult(false, 360000, true) == ListFailWatch::kFail);    // tercera seguida
    CHECK(w.failingHours(360000) == 0 && w.failingHours(3 * 3600000) == 3);
    CHECK(w.onResult(false, 2160000, true) == ListFailWatch::kNone);    // a los 30 min: ya se avisó
    CHECK(w.onResult(false, 360000 + ListFailWatch::kRepeatMs - 1, true) == ListFailWatch::kNone);
    CHECK(w.onResult(false, 360000 + ListFailWatch::kRepeatMs, true) == ListFailWatch::kFail);  // 24 h después
    CHECK(w.failingHours(360000 + ListFailWatch::kRepeatMs) == 24);
    CHECK(w.onResult(true, 90000000, true) == ListFailWatch::kRecovered);
    CHECK(w.failingHours(90000000) == 0);
    CHECK(w.onResult(true, 90000001, true) == ListFailWatch::kNone);
    // Una falla suelta que se arregla no avisa nada.
    CHECK(w.onResult(false, 1, true) == ListFailWatch::kNone);
    CHECK(w.onResult(true, 2, true) == ListFailWatch::kNone);
  }
  {  // Apagado: no avisa ni cuenta como avisado; al encenderlo con la racha en curso, avisa.
    ListFailWatch w;
    for (int i = 0; i < 5; i++) CHECK(w.onResult(false, uint32_t(i) * 60000, false) == ListFailWatch::kNone);
    CHECK(w.onResult(true, 400000, false) == ListFailWatch::kNone);
    for (int i = 0; i < 3; i++) w.onResult(false, uint32_t(i), false);
    CHECK(w.onResult(false, 10, true) == ListFailWatch::kFail);
    CHECK(w.onResult(true, 20, false) == ListFailWatch::kNone);  // se apagó: no dice «ya se pudo»
  }
  {  // millis() da la vuelta en medio de la racha.
    ListFailWatch w;
    uint32_t t0 = 0xFFFFFFFFu - 1000;
    w.onResult(false, t0, true);
    w.onResult(false, t0 + 500, true);
    CHECK(w.onResult(false, 5000, true) == ListFailWatch::kFail);
    CHECK(w.onResult(false, 6000, true) == ListFailWatch::kNone);
    CHECK(w.failingHours(7200000) == 2);
  }
}

// Prefijos reales de la lista MA-L del IEEE (bajada el 2026-11-22).
static bool brandIs(std::initializer_list<uint8_t> b, const char* want) {
  uint8_t mac[6] = {0, 0, 0, 0x12, 0x34, 0x56};
  size_t i = 0;
  for (uint8_t x : b)
    if (i < 6) mac[i++] = x;
  const char* got = ouiBrand(mac);
  if (!want) return got == nullptr;
  return got && std::strcmp(got, want) == 0;
}

static void testOui() {
  CHECK(brandIs({0x00, 0x00, 0xF0}, "Samsung"));  // Samsung Electronics, primero de su lista
  CHECK(brandIs({0xFC, 0xF1, 0x36}, "Samsung"));
  CHECK(brandIs({0x00, 0x04, 0x1F}, "Consola PlayStation"));  // primero de la tabla entera
  CHECK(brandIs({0xFC, 0xCA, 0x40}, "Consola PlayStation"));  // último de su bloque
  CHECK(brandIs({0x00, 0x03, 0x93}, "Apple"));                // primero del bloque Apple
  CHECK(brandIs({0xFC, 0xFC, 0x48}, "Apple"));                // último del bloque Apple
  CHECK(brandIs({0x00, 0x09, 0xBF}, "Nintendo"));
  CHECK(brandIs({0x00, 0x4B, 0x12}, "Aparato inteligente"));  // Espressif
  CHECK(brandIs({0x00, 0x1B, 0xA9}, "Impresora Brother"));
  CHECK(brandIs({0x00, 0x01, 0xE6}, "HP"));                   // "Hewlett Packard"
  CHECK(brandIs({0x00, 0x02, 0xB3}, "Computadora"));          // Intel, primero del último bloque
  CHECK(brandIs({0xFC, 0xF8, 0xAE}, "Computadora"));          // último de la tabla entera
  // Sin sugerencia: chips y armadoras, HP Enterprise, prefijos que no están.
  CHECK(brandIs({0x00, 0xE0, 0x4C}, nullptr));  // Realtek
  CHECK(brandIs({0x00, 0x14, 0xA4}, nullptr));  // Hon Hai (Foxconn)
  CHECK(brandIs({0x00, 0x17, 0x87}, nullptr));  // "Brother, Brother & Sons ApS" (no es la de impresoras)
  CHECK(brandIs({0x00, 0x0B, 0x86}, nullptr));  // Hewlett Packard Enterprise
  CHECK(brandIs({0x00, 0x00, 0x00}, nullptr));
  CHECK(brandIs({0xFC, 0xFF, 0xFC}, nullptr));  // múltiplo de 4: no es privada ni de grupo

  // Dirección privada (bit 0x02) o de grupo (bit 0x01): nunca dice marca.
  uint8_t priv[6] = {0x02, 0, 0, 0, 0, 5}, apple[6] = {0x00, 0x03, 0x93, 1, 2, 3};
  CHECK(macIsPrivate(priv) && ouiCode(priv) == kOuiPrivate && ouiBrand(priv) == nullptr);
  CHECK(!macIsPrivate(apple) && ouiCode(apple) == 2 && std::strcmp(ouiLabel(ouiCode(apple)), "Apple") == 0);
  uint8_t applePriv[6] = {0x02, 0x03, 0x93, 1, 2, 3}, appleGroup[6] = {0x01, 0x03, 0x93, 1, 2, 3};
  CHECK(macIsPrivate(applePriv) && ouiBrand(applePriv) == nullptr);  // el prefijo de Apple con el bit privado
  CHECK(macIsPrivate(appleGroup) && ouiCode(appleGroup) == kOuiPrivate);
  uint8_t da[6] = {0xDA, 0xA1, 0x19, 0, 0, 1};  // así se ven las de un iPhone (x2, x6, xA, xE)
  CHECK(macIsPrivate(da));
  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  CHECK(macIsPrivate(bcast) && ouiBrand(bcast) == nullptr);
  CHECK(ouiLabel(kOuiNone) == nullptr && ouiLabel(kOuiPrivate) == nullptr && ouiLabel(200) == nullptr);
  CHECK(ouiLabel(1) && std::strcmp(ouiLabel(1), "Consola PlayStation") == 0);
  // Los dos últimos bytes de la MAC no cambian la marca.
  CHECK(brandIs({0x00, 0x03, 0x93, 0xFF, 0xFF, 0xFF}, "Apple"));
}

int main() {
  testSha256();
  testPbkdf2();
  testHex();
  testBase64();
  testValidPanelPassword();
  testValidWifi();
  testApPassword();
  testApSsid();
  testImprovParser();
  testImprovRpc();
  testImprovBuild();
  testImprovTry();
  testModes();
  testApAddress();
  testDnsNames();
  testUsageHistory();
  testTopBlocked();
  testBypassTableAt(0);
  testBypassTableAt(0xFFFFFFFFu - 60000);
  testDiagnose();
  testOsFromUserAgent();
  testProbeAnswerIp();
  testUpstreamWatch();
  testCloud();
  testFamily();
  testBackup();
  testLists();
  testStatusLed();
  testSavings();
  testCommunity();
  testNotify();
  testOui();
  if (fails) {
    std::printf("%d fallas de %d pruebas\n", fails, checks);
    return 1;
  }
  std::printf("OK: %d pruebas, 0 fallas\n", checks);
  return 0;
}

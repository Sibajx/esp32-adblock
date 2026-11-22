# SiBlock fase 2 ("Acompañamiento"): plan de implementación

> **Para agentes:** SUB-SKILL OBLIGATORIA: usa superpowers:subagent-driven-development (recomendado) o
> superpowers:executing-plans para hacer este plan tarea por tarea. Los pasos usan casillas (`- [ ]`).

**Meta:** que cada persona sepa si su aparato usa SiBlock y, si algo falla, qué hacer: comprobación
con un nombre de prueba, botón "algo no funciona", canario de Firefox, opción de iCloud Private
Relay, aviso de aparatos que se saltan el bloqueo, gráficas de 24 h / 7 días y top 10 en el panel,
panel cómodo en el celular, y en la página las guías del router por marca y proveedor, la guía del
FortiGate y la página de obstáculos.

**Arquitectura:** la lógica nueva sin Arduino va en `lib/siblock_core/` (nombres especiales, historia
de uso, top de bloqueados, tabla de aparatos que se saltan, diagnóstico) y en `lib/adblock_core/`
(respuesta NXDOMAIN), probada en `test_host`. `src/dns_server.cpp` la usa al contestar;
`src/web.cpp` suma rutas; `src/web_page.h` dibuja todo con DOM y SVG a mano. `site/` suma tres
páginas estáticas y un módulo JS con pruebas `node --test`.

**Tecnologías:** ESP32 clásico, Arduino core 2.0.17, PlatformIO, C++17, Python 3 + dnspython
(simulador), HTML/CSS/JS sin framework, `node --test`.

**Diseño:** [`docs/superpowers/specs/2026-11-21-siblock-design.md`](../specs/2026-11-21-siblock-design.md),
sección "Fase 2 en detalle" (2.1 a 2.7). Fuente: [`docs/ideas-pendientes.md`](../../ideas-pendientes.md)
§2, §2.1, §2.3, §2.5, §3, §3.2, §3.3, §3.5.

## Restricciones globales

- Rama `siblock-fases`. Prohibido: push, merge a `main`, cambiar la visibilidad del repo,
  `wrangler deploy` / `wrangler pages deploy` reales, tocar otros equipos de la red, mandar
  correos, leer o copiar archivos personales con
  tokens o claves, poner correos personales (solo
  `contacto@s-dev.win`). No leer ni mostrar `src/secrets.h`.
- No cambiar `partitions.csv`. Flash del programa < 92 % en `pio run`.
- Deben pasar siempre: `make -C test_host test`; `make -C sim`, luego
  `cd sim && SIM_FLASH=./sim-flash ./esp32-sim` en segundo plano y `python3 sim/test_sim.py`;
  apagar con `pkill -x esp32-sim` (nunca `pkill -f`); `pio run`; `cd site && npm test`.
- Texto que ve la persona: español, palabras simples, sin tecnicismos. Marca **SiBlock**.
- Privacidad: sin cookies; IPs solo en RAM (como hoy), nunca en flash ni fuera de la placa; nada
  nuevo sale de la placa a internet.
- Seguridad: rutas nuevas pensadas contra un atacante en la red local. Header anti-CSRF sigue
  `X-Requested-With: adblock`. Cuerpo de POST con el tope de 512 B de hoy.
- No inventar datos de routers ni proveedores: sin fuente → guía general + "falta verificar".
- Commit al final de cada tarea solo si el orquestador lo pide (pie
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`); nunca push.

## Contratos entre carriles

| Contrato | Valor |
|---|---|
| Nombre de prueba | `<n>.prueba.siblock.internal` (`n` = 12 caracteres `[a-z0-9]` al azar); `A` → IP de la placa, TTL 0; `AAAA` y otros → NOERROR vacío |
| Imagen de prueba | `GET http://<n>.prueba.siblock.internal[:puerto]/siblock-prueba.gif` → 200 `image/gif`, GIF 1×1 transparente de 43 B, `Cache-Control: no-store` |
| CSP del panel | la de hoy + `img-src *.prueba.siblock.internal:*` |
| Canario Firefox | `use-application-dns.net` (exacto) → NXDOMAIN siempre |
| Private Relay | `mask.icloud.com`, `mask-h2.icloud.com` (exactos) → NXDOMAIN solo con `relay_block`=1 (NVS, `putUInt`, falta = 0) |
| API nueva | `GET /api/diagnose?probe=ok\|fail` (pública, pide anti-CSRF); `GET /api/history` 🔒; `GET /api/options` 🔒; `POST /api/options` 🔒 cuerpo `relayBlock=0\|1` |
| `/api/diagnose` | `{"findings":[{"code":"<código>","level":"ok\|info\|warn\|bad"}]}` (códigos en la tarea 4) |
| `/api/history` | `{"clock":true,"hour":<hora UTC = epoch/3600>,"h":[[consultas,bloqueadas] ×168, la más vieja primero],"top":[{"d":"dominio","n":12}] (≤10)}`; sin hora: `clock:false`, `hour:0`, `h` en ceros |
| `/api/options` | `{"relayBlock":false}` |
| `/api/detail` (campo nuevo) | `"bypass":[{"ip":"192.168.1.20","k":["doh","relay"],"ago":120}]` |
| `/api/stats` (campos nuevos) | `"canary":<n>`, `"relay":<n>` (contadores de NXDOMAIN dados) |
| Archivo nuevo en LittleFS | `/history.bin` (1352 B, se escribe vía `/history.tmp` + rename) |
| Páginas nuevas | `site/guia-router.html`, `site/fortigate.html`, `site/obstaculos.html` (con `id="no-puede"` para "Lo que un DNS no puede bloquear") |
| Enlaces del panel a la página | `https://siblock.s-dev.win/guia-router.html`, `https://siblock.s-dev.win/obstaculos.html` |

## Enfoque de revisión (lo que más puede morder)

1. **Casa con un solo aparato o sin puerta de enlace**: "router que hace de DNS" no debe salir con
   pocas consultas ni con gateway 0. `routerLooksLikeDns` exige ≥ 50 consultas y ≥ 80 % de la
   puerta de enlace, y gateway 0 → false. Prueba en la tarea 4 y en el simulador (tarea 7).
2. **Hora que llega tarde, salta adelante días o va hacia atrás** (NTP tarde, corte de luz): la
   historia no debe romperse ni mezclar horas. Salto de ≥ 168 h limpia todo; hora vieja dentro de
   la ventana suma en su casilla; más vieja se ignora; hora 0 no cuenta. Prueba en la tarea 3.
3. **`Host` falsificado para la imagen de prueba**: `prueba.siblock.internal.evil.com`,
   `xprueba.siblock.internal`, `a.prueba.siblock.internal.` con puerto raro o mayúsculas. Solo
   `<etiquetas>.prueba.siblock.internal[:puerto]` vale, y solo para el GIF; el resto de rutas con
   ese `Host` → 403. Prueba en la tarea 2 y en el simulador (tarea 7).
4. **Dominios raros en el top 10** (253 caracteres, bytes UTF-8 inválidos, comillas): se cortan a
   63 bytes y salen escapados en JSON. Prueba en la tarea 3 y en el simulador (tarea 7).
5. **Opción de Private Relay con valores malos o sin permiso**: `relayBlock=2`, vacío, sin sesión,
   sin anti-CSRF → no cambia nada; "borrar la configuración" la apaga. Prueba en la tarea 6.

---

## Carril firmware (`src/`, `lib/`, `sim/`, `test_host/`, `platformio.ini`)

Un solo agente, tareas 1 → 9 en orden. Skills: `esp32`, `superpowers:test-driven-development`;
`dataviz` para las gráficas (tarea 8); `security-suite` (auditor de endpoints) en la tarea 9.

### Tarea 1: `adblock_core` — respuesta NXDOMAIN

**Archivos:** Modificar `lib/adblock_core/adblock_core.{h,cpp}`, `test_host/test_core.cpp`.

**Interfaces (produce):**
```cpp
namespace adblock {
// NXDOMAIN: mismo ID y pregunta, QR=1, RA=1, RD copiado, RCODE=3, sin respuestas ni EDNS.
size_t buildNxdomain(const uint8_t* query, const DnsQuestion& q, uint8_t* out);
}
```

- [ ] **Paso 1: prueba que falla** en `test_core.cpp` (mismo estilo que la de `buildServfail`):
  consulta `use-application-dns.net` A con ID 0xBEEF y RD=1 → largo = `q.questionEnd`,
  `out[0..1]` = BE EF, `out[3] & 0x0F == 3`, `out[2] & 0x80` (QR), `out[2] & 0x01` (RD),
  QDCOUNT 1, ANCOUNT/NSCOUNT/ARCOUNT 0, la pregunta igual byte a byte; con EDNS en la consulta
  ARCOUNT sigue en 0.
- [ ] **Paso 2:** `make -C test_host test` → falla al compilar.
- [ ] **Paso 3:** implementar `buildNxdomain` reusando lo que ya hace `buildServfail`.
- [ ] **Paso 4:** `make -C test_host test` → "OK: todas las pruebas pasan" y 0 fallas en `test_siblock`.
- [ ] **Paso 5:** commit `adblock_core: respuesta NXDOMAIN`.

### Tarea 2: `siblock_core` — nombres especiales

**Archivos:** Crear `lib/siblock_core/dns_names.{h,cpp}`; modificar `test_host/test_siblock.cpp`.

**Interfaces (produce):**
```cpp
namespace siblock {
constexpr const char kProbeSuffix[] = "prueba.siblock.internal";
enum class SpecialName : uint8_t { kNone, kProbe, kCanary, kRelay };
// name: ya en minúsculas, sin punto final (como DnsQuestion::name).
// kProbe: igual a kProbeSuffix o termina en "." + kProbeSuffix. kCanary: "use-application-dns.net".
// kRelay: "mask.icloud.com" o "mask-h2.icloud.com". Todo exacto (sin subdominios) salvo kProbe.
SpecialName classifySpecial(const std::string& name);
enum BypassFlag : uint8_t { kBypassDoh = 1, kBypassRelay = 2 };
// kBypassDoh si name es o cuelga de un nombre de kDohNames; kBypassRelay si classifySpecial == kRelay.
uint8_t bypassFlagsFor(const std::string& name);
extern const char* const kDohNames[];
extern const size_t kDohNamesCount;
// Header Host: "<etiquetas>.prueba.siblock.internal" con ":puerto" opcional (solo dígitos),
// sin distinguir mayúsculas. Cada etiqueta 1..63 de [a-z0-9-]. Nada más vale.
bool isProbeHost(const std::string& host);
}
```
`kDohNames` (fija, se compara por sufijo de etiqueta): `dns.google`, `cloudflare-dns.com`,
`one.one.one.one`, `dns.quad9.net`, `dns9.quad9.net`, `dns10.quad9.net`, `dns11.quad9.net`,
`doh.opendns.com`, `doh.familyshield.opendns.com`, `dns.nextdns.io`, `dns.adguard-dns.com`,
`dns.adguard.com`, `doh.cleanbrowsing.org`, `dns.mullvad.net`, `doh.mullvad.net`.

- [ ] **Paso 1: pruebas que fallan** (`testDnsNames`):
  - `classifySpecial`: `"ab12.prueba.siblock.internal"` y `"prueba.siblock.internal"` → kProbe;
    `"xprueba.siblock.internal"`, `"prueba.siblock.internal.com"` → kNone;
    `"use-application-dns.net"` → kCanary, `"a.use-application-dns.net"` → kNone;
    `"mask.icloud.com"`, `"mask-h2.icloud.com"` → kRelay, `"icloud.com"`, `"x.mask.icloud.com"` → kNone.
  - `bypassFlagsFor`: `"dns.google"`, `"chrome.cloudflare-dns.com"`, `"abc123.dns.nextdns.io"` →
    kBypassDoh; `"notdns.google"`, `"google.com"`, `"cloudflare-dns.com.evil.net"` → 0;
    `"mask.icloud.com"` → kBypassRelay.
  - `isProbeHost`: sí `"ab12.prueba.siblock.internal"`, `"AB12.Prueba.Siblock.Internal:8080"`,
    `"a.b.prueba.siblock.internal"`; no `"prueba.siblock.internal"` (sin etiqueta),
    `"x.prueba.siblock.internal.evil.com"`, `"xprueba.siblock.internal"`,
    `"a.prueba.siblock.internal:80x"`, `"a.prueba.siblock.internal."`, `"a_b.prueba.siblock.internal"`,
    `""`, `"..prueba.siblock.internal"`.
- [ ] **Paso 2:** `make -C test_host test` → falla al compilar.
- [ ] **Paso 3:** implementar en `dns_names.cpp` (sin `std::regex`: ocupa flash).
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: nombres especiales (prueba, canario, Private Relay, DoH)`.

### Tarea 3: `siblock_core` — historia de 168 h y top de bloqueados

**Archivos:** Crear `lib/siblock_core/usage.{h,cpp}`; modificar `test_host/test_siblock.cpp`.

**Interfaces (produce):**
```cpp
namespace siblock {
// Hora UTC (epoch/3600) o 0 si la hora no es creíble (epoch < 1700000000).
uint32_t epochHour(int64_t epochSeconds);

class UsageHistory {
 public:
  static constexpr size_t kHours = 168;
  static constexpr size_t kBytes = 8 + kHours * 8;  // 1352: "SBH1" + u32 última hora + 168 × (u32 consultas, u32 bloqueadas), little-endian
  struct Slot { uint32_t queries, blocked; };
  // hour 0 no cuenta. Hora nueva: limpia las casillas entre la última y esta (≥ 168 → todas).
  // Hora vieja dentro de las últimas 168: suma en su casilla. Más vieja: se ignora.
  void add(uint32_t hour, bool blocked);
  // Las 168 horas que terminan en nowHour, la más vieja primero; sin datos = {0,0}.
  void snapshot(uint32_t nowHour, Slot out[kHours]) const;
  size_t serialize(uint8_t* out, size_t max) const;  // kBytes, o 0 si max < kBytes
  bool deserialize(const uint8_t* in, size_t len);  // false (y sin cambios) si len o cabecera no cuadran
 private:
  Slot slots_[kHours] = {};
  uint32_t last_ = 0;
};

class TopBlocked {  // Space-Saving: 32 casillas fijas, ~2,2 KB
 public:
  static constexpr size_t kSlots = 32, kNameMax = 63;
  struct Entry { char name[kNameMax + 1]; uint32_t count; };
  void add(const char* name);                         // corta a 63 bytes
  size_t top(Entry* out, size_t max) const;           // mayor a menor; empate por nombre
  void halve();                                       // cuenta / 2; las que quedan en 0 se sueltan
 private:
  Entry e_[kSlots] = {};
};
}
```

- [ ] **Paso 1: pruebas que fallan** (`testUsageHistory`, `testTopBlocked`):
  - `epochHour(1699999999) == 0`; `epochHour(1700000000) == 472222`; `epochHour(-5) == 0`.
  - `add(h,false)` ×3 y `add(h,true)` ×1 → `snapshot(h)[167] == {4,1}`; `snapshot(h+1)[166] == {4,1}`, `[167] == {0,0}`.
  - Hueco: datos en `h`, luego `add(h+5,...)` → las horas `h+1..h+4` en cero, `h` intacta.
  - Salto: `add(h+168,...)` → la hora `h` ya no aparece (todo en cero salvo `h+168`).
  - Hora vieja: tras `add(h+10)`, `add(h+3,true)` suma en `h+3`; `add(h+10-168,true)` se ignora.
  - `add(0,true)` no cambia nada.
  - `serialize` → 1352 B que empiezan con `"SBH1"`; `deserialize` de eso da el mismo `snapshot`;
    `deserialize` con 1351 B o cabecera `"XXXX"` → false y la historia no cambia.
  - Top: `"a"` ×5, `"b"` ×3, `"c"` ×1 → top(…,10) = a5, b3, c1. Con 32 nombres únicos llenos y
    luego `"pesado"` ×100 intercalado con 200 nombres únicos → `"pesado"` es el primero.
  - Nombre de 253 `x` → guardado con 63 bytes y `'\0'`. `halve()` sobre a5,b3,c1 → a2,b1 y `c`
    desaparece.
- [ ] **Paso 2:** `make -C test_host test` → falla al compilar.
- [ ] **Paso 3:** implementar. Casilla de la hora `h` = `h % 168`.
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: historia de 168 h y top de bloqueados`.

### Tarea 4: `siblock_core` — aparatos que se saltan y diagnóstico

**Archivos:** Crear `lib/siblock_core/diagnose.{h,cpp}`; modificar `test_host/test_siblock.cpp`.

**Interfaces (produce):**
```cpp
namespace siblock {
class BypassTable {  // solo RAM
 public:
  static constexpr size_t kSlots = 16;
  static constexpr uint32_t kForgetMs = 24UL * 3600 * 1000;
  struct Entry { uint32_t ip; uint8_t flags; uint32_t lastMs; };
  void note(uint32_t ip, uint8_t flags, uint32_t nowMs);   // flags 0: nada. Llena: pisa la más vieja. Suma flags (OR)
  uint8_t flagsFor(uint32_t ip, uint32_t nowMs) const;      // 0 si no está o venció
  size_t list(Entry* out, size_t max, uint32_t nowMs) const;  // sin vencidas, más reciente primero
 private:
  Entry e_[kSlots] = {};
};

enum class ProbeResult : uint8_t { kSkip, kOk, kFail };
ProbeResult parseProbe(const std::string& s);              // "ok" / "fail"; lo demás kSkip
enum class DiagLevel : uint8_t { kOk, kInfo, kWarn, kBad };
const char* levelName(DiagLevel l);                         // "ok" "info" "warn" "bad"
struct DiagFinding { const char* code; DiagLevel level; };
struct DiagInput {
  ProbeResult probe; bool clientSeen; uint8_t bypassFlags; bool relayBlockOn;
  bool paused; bool listLoaded; bool upstreamDown; bool viaRouter;
};
// Orden: bad, warn, info, ok (estable dentro de cada nivel). Nunca vacía.
std::vector<DiagFinding> diagnose(const DiagInput& in);
bool routerLooksLikeDns(uint32_t gatewayIp, uint32_t gatewayQueries, uint32_t totalQueries); // gatewayIp != 0, total >= 50, gw*10 >= total*8
// true si hubo timeout, el último timeout es más nuevo que la última respuesta y fue hace < 5 min.
bool upstreamLooksDown(uint32_t nowMs, uint32_t lastAnsweredMs, uint32_t lastTimeoutMs, bool anyTimeout);
}
```

Códigos de `diagnose` (tabla cerrada; los textos están en la tarea 8):

| Condición | code | level |
|---|---|---|
| probe kOk | `usa_siblock` | ok |
| probe kFail y clientSeen | `navegador_dns_propio` | warn |
| probe kFail y !clientSeen | `no_usa_siblock` (bad) + `ipv6` (info) | |
| probe kSkip y !clientSeen | `sin_consultas` | warn |
| bypassFlags & kBypassDoh | `dns_cifrado` | warn |
| bypassFlags & kBypassRelay y !relayBlockOn | `private_relay` | warn |
| paused | `en_pausa` | warn |
| !listLoaded | `sin_lista` | bad |
| upstreamDown | `sin_internet` | bad |
| viaRouter | `router_dns` | warn |
| ningún bad ni warn | `todo_bien` | ok |

- [ ] **Paso 1: pruebas que fallan** (`testBypassTable`, `testDiagnose`):
  - `note(ip1,kBypassDoh,0)`, `note(ip1,kBypassRelay,10)` → `flagsFor(ip1,20) == 3`; a `kForgetMs+11` → 0.
  - 17 IPs distintas → la primera (más vieja) se pierde; `list` da 16, la más reciente primero.
  - `diagnose({kOk,true,0,false,false,true,false,false})` → `[usa_siblock/ok, todo_bien/ok]`.
  - `{kFail,false,...listLoaded=true}` → primero `no_usa_siblock/bad`, luego `ipv6/info`, sin `todo_bien`.
  - `{kFail,true,...}` → contiene `navegador_dns_propio/warn`.
  - `{kOk, bypass=kBypassRelay, relayBlockOn=false}` → `private_relay`; con `relayBlockOn=true` no.
  - `{kSkip, clientSeen=true, listLoaded=false, upstreamDown=true, paused=true}` → `sin_lista` y
    `sin_internet` antes que `en_pausa`.
  - `routerLooksLikeDns`: (gw,40,49) false; (gw,40,50) true; (gw,39,50) false; (0,100,100) false.
  - `upstreamLooksDown`: sin timeout → false; timeout a 1000, respuesta a 500, ahora 2000 → true;
    ahora 1000+300000 → false; respuesta 1500 > timeout 1000 → false; con `now` cerca de
    `0xFFFFFFFF` y valores que dieron la vuelta, mismas respuestas (restas sin signo).
  - `parseProbe("ok")==kOk`, `("fail")==kFail`, `("OK")`, `("")`, `("okk")` → kSkip.
- [ ] **Paso 2:** `make -C test_host test` → falla al compilar.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: aparatos que se saltan y diagnóstico`.

### Tarea 5: shim del simulador — puerta de enlace

**Archivos:** Modificar `sim/shim/WiFi.h` (y `shim.cpp` si hace falta).

**Interfaces (produce):** `IPAddress WiFiClass::gatewayIP()` → la IP de `$SIM_GATEWAY` si existe
y es válida; si no, `127.0.0.254` (así el `127.0.0.1` de las pruebas no es la puerta de enlace).

- [ ] **Paso 1:** agregar `gatewayIP()`; `make -C sim` compila.
- [ ] **Paso 2:** arrancar el simulador principal y `python3 sim/test_sim.py` → "TODO BIEN".
- [ ] **Paso 3:** commit `sim: puerta de enlace simulada`.

### Tarea 6: DNS — canario, prueba, Private Relay, detección, historia; opción en NVS y `/api/options`

**Archivos:** Modificar `src/dns_server.{h,cpp}`, `src/settings.{h,cpp}`, `src/config.h`,
`src/web.cpp`, `src/main.cpp`, `sim/test_sim.py`.

**Interfaces:**
- Consume: tareas 1–4.
- Produce:
```cpp
// config.h
#define NVS_RELAY_BLOCK "relay_block"   // u32 0/1; falta = 0
// settings.h
bool relayBlock();
bool setRelayBlock(bool on);
// dns_server.h
struct DnsStats { /* … lo de hoy … */ uint32_t canary = 0; uint32_t relay = 0; };
class DnsServer {
 public:
  void setRelayBlock(bool on);            // volatile, lo lee la tarea DNS
  bool relayBlock() const;
  uint8_t bypassFlagsFor(uint32_t ip);
  size_t bypassList(siblock::BypassTable::Entry* out, size_t max);
  bool clientSeenWithin(uint32_t ip, uint32_t ms);   // de la tabla de aparatos
  uint32_t clientQueries(uint32_t ip);
  uint32_t totalClientQueries();
  bool upstreamDown();                    // upstreamLooksDown con lastAnsweredMs_/lastTimeoutMs_
  void historySnapshot(uint32_t nowHour, siblock::UsageHistory::Slot* out);
  size_t topBlocked(siblock::TopBlocked::Entry* out, size_t max);
  size_t saveHistory(uint8_t* out, size_t max);
  bool loadHistory(const uint8_t* in, size_t len);
  void halveTop();
};
```
Orden en `answerLocally` (después del portal cautivo, antes de la lista):
1. `classifySpecial`: kProbe → `buildAddressResponse(…, uint32_t(WiFi.localIP()), …)` (no cuenta
   como bloqueada ni va a "últimas consultas"); kCanary → `buildNxdomain`, `stats_.canary++`;
   kRelay con `relayBlock` → `buildNxdomain`, `stats_.relay++`. Vale también en pausa.
2. `bypassFlagsFor(name)` (con kRelay solo si la opción está apagada) → `BypassTable::note`.
3. Lo de hoy (lista, caché, reenvío). Cada consulta que llega aquí suma a la historia con
   `epochHour(time(nullptr))`; cada bloqueada suma al top.
Historia, top y tabla protegidos con un `portMUX` propio cada uno (copias fuera del lock al leer).
`main.cpp`: al arrancar `dns.setRelayBlock(settings::relayBlock())`. `web.cpp`: `GET/POST
/api/options` (privadas); POST acepta solo `relayBlock` = `"0"` o `"1"` (otro valor o falta → 400
`"valor inválido"`), guarda en NVS y luego `gDns->setRelayBlock`. `/api/stats` suma `canary` y `relay`.

- [ ] **Paso 1: pruebas que fallan** en `sim/test_sim.py`, sección nueva `"\n5b. Nombres especiales y Private Relay"` en `main()`:
  - `use-application-dns.net` A y AAAA → rcode 3 sin respuestas; `stats()["canary"]` sube 2.
  - `ab12cd34ef56.prueba.siblock.internal` A → `["127.0.0.1"]` con TTL 0; AAAA → NOERROR vacío.
  - `mask.icloud.com` A → **no** es NXDOMAIN (rcode 0) con la opción apagada.
  - `GET /api/options` → `{"relayBlock": false}`; sin sesión → 401; `POST` sin anti-CSRF → 403;
    `relayBlock=2` y cuerpo vacío → 400 y sigue false.
  - `POST relayBlock=1` → 200; `mask.icloud.com` y `mask-h2.icloud.com` → rcode 3;
    `stats()["relay"]` sube; con pausa de 5 min sigue rcode 3; reanudar; `POST relayBlock=0` → rcode 0 otra vez.
  - Placa nueva (`Fresh(nvs=SEED_WIFI)`, sección nueva en `nuevas()`): `relay_block` falta → `mask.icloud.com`
    rcode 0; reiniciar con `{"siblock": {…SEED_WIFI…, "relay_block": "1"}}` → rcode 3; borrar
    configuración con BOOT 10 s (igual que en `boton_boot()`) → `relay_block` ya no está en `nvs()`.
- [ ] **Paso 2:** `make -C sim` y `python3 sim/test_sim.py` → las checks nuevas FALLAN.
- [ ] **Paso 3:** implementar según la lista de arriba.
- [ ] **Paso 4:** `make -C test_host test`, `make -C sim`, simulador + `python3 sim/test_sim.py` → "TODO BIEN"; `pio run` compila.
- [ ] **Paso 5:** commit `DNS: canario de Firefox, nombre de prueba, opción de Private Relay y detección`.

### Tarea 7: rutas del panel — imagen de prueba, `/api/diagnose`, `/api/history`, aparatos que se saltan; historia guardada

**Archivos:** Modificar `src/web.cpp`, `src/main.cpp`, `src/dns_server.{h,cpp}` si falta algo, `sim/test_sim.py`.

**Interfaces:**
- Consume: tarea 6 (`DnsServer`), tarea 4 (`diagnose`), tarea 2 (`isProbeHost`).
- Produce: las rutas del contrato.
  - `GET /siblock-prueba.gif`: registrada sin `guarded`. Si `isProbeHost(host)` o `hostOk()` → 200 GIF
    (bytes fijos `GIF89a…;`, 43 B), `Content-Type: image/gif`, `Cache-Control: no-store`. Si no → 403.
    Cualquier otra ruta con `Host` de prueba sigue en 403 (por `hostOk`).
  - `GET /api/diagnose` (`guarded<false,…>` y además exige anti-CSRF, si falta → 403): arma
    `DiagInput` con `parseProbe(server.arg("probe"))`, la IP del que pregunta,
    `clientSeenWithin(ip, 600000)`, `bypassFlagsFor(ip)`, `relayBlock()`, pausa,
    `gList->count() > 0`, `upstreamDown()`, `routerLooksLikeDns(uint32_t(WiFi.gatewayIP()),
    clientQueries(gw), totalClientQueries())`.
  - `GET /api/history` (`guarded<true,…>`): `epochHour(time(nullptr))`; `clock:false` si da 0.
    Top 10 con `appendJsonString`.
  - `/api/detail` suma `bypass` (`k`: `"doh"`, `"relay"` según flags).
  - `main.cpp`: al arrancar, tras montar LittleFS, lee `/history.bin` → `dns.loadHistory`. Cada
    60 min de `millis()` guarda en `/history.tmp` y renombra a `/history.bin`. Cada 24 h de
    `millis()` → `dns.halveTop()`. Todo con restas sin signo.
  - CSP de `handlePage`: la de hoy + `img-src *.prueba.siblock.internal:*` (si la de hoy no tiene `img-src`, se agrega esa directiva sola).

- [ ] **Paso 1: pruebas que fallan** en `sim/test_sim.py`, sección `"\n5c. Comprobación, diagnóstico e historia"`:
  - `GET /siblock-prueba.gif` con `Host: ab12cd34ef56.prueba.siblock.internal:8080` → 200, cuerpo
    empieza con `b"GIF89a"`, 43 B, `no-store`. Con `Host: x.prueba.siblock.internal.evil.com` → 403.
    `GET /api/stats` con `Host: ab12.prueba.siblock.internal` → 403.
  - CSP de `/` contiene `img-src *.prueba.siblock.internal:*`.
  - Tras una consulta DNS del simulador (127.0.0.1): `GET /api/diagnose?probe=ok` sin sesión → 200,
    contiene `usa_siblock` y ningún hallazgo `bad`; sin anti-CSRF → 403. `probe=fail` → contiene
    `navegador_dns_propio`. Con `X-Sim-Client: 192.168.1.77` y `probe=fail` → contiene
    `no_usa_siblock` (bad) y `ipv6` (info), y los `bad` van antes que los `info`. Ningún valor de
    la respuesta contiene un punto (no trae dominios ni IPs).
  - `query("dns.google")` y luego `/api/detail` → `bypass` trae `{"ip":"127.0.0.1","k":["doh"]}`;
    `/api/diagnose?probe=ok` trae `dns_cifrado`.
  - `GET /api/history` sin sesión → 401; con sesión → `len(h) == 168`, `clock` true, y tras
    3 consultas a `doubleclick.net` la última hora sube ≥ 3 en ambas columnas; `top[0]["d"] == "doubleclick.net"`.
    Un dominio de 70 caracteres bloqueado a mano y consultado aparece en `top` cortado a 63.
  - Placa nueva con `env={"SIM_GATEWAY": "127.0.0.1"}`: 60 consultas → `/api/diagnose` trae
    `router_dns`; con 10 consultas no.
  - Historia que sobrevive: placa nueva, crea contraseña con `/api/setup`, 5 consultas bloqueadas,
    `set_clock(+61 min)`, esperar 1 s, cerrar, relanzar con `dir=` la misma carpeta, entrar con
    Basic → suma de bloqueadas de `h` ≥ 5.
- [ ] **Paso 2:** `make -C sim` + simulador + `python3 sim/test_sim.py` → las nuevas FALLAN.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** `make -C test_host test`; `make -C sim`; simulador + `python3 sim/test_sim.py` → "TODO BIEN"; `pio run` compila.
- [ ] **Paso 5:** commit `Panel: imagen de prueba, diagnóstico, historia y aparatos que se saltan`.

### Tarea 8: panel — comprobación, "algo no funciona", gráficas, top 10, opción de Private Relay, celular

**Archivos:** Modificar `src/web_page.h`, `sim/test_sim.py`.

**Interfaces:** consume las rutas del contrato. Todo lo que viene de la red con `textContent`;
SVG con `document.createElementNS('http://www.w3.org/2000/svg', …)`; sin librerías, sin
`innerHTML`, sin `style=` desde JS que la CSP bloquee (la CSP permite `'unsafe-inline'` en estilos,
igual se prefieren clases).

Qué agrega (ids fijos, los usa la prueba):
- `#comprobar` (público, arriba): al cargar y con el botón "Comprobar otra vez" carga
  `http://<12 al azar>.prueba.siblock.internal${location.port?':'+location.port:''}/siblock-prueba.gif`
  con `new Image()` y espera 4 s. Textos: ok → "✅ Este aparato está usando SiBlock."; falla →
  "❌ Este aparato todavía no usa SiBlock." + botón "Algo no funciona".
- `#algo` botón "Algo no funciona" (público) → corre la comprobación y `GET /api/diagnose?probe=…`
  y pinta `#hallazgos` (lista, bad primero, con un ícono por nivel). Textos por código:

| code | Texto |
|---|---|
| `usa_siblock` | Este aparato usa SiBlock. |
| `navegador_dns_propio` | Este aparato pregunta a SiBlock, pero este navegador usa su propio DNS. Apaga "DNS seguro" en los ajustes del navegador. |
| `no_usa_siblock` | Este aparato no pregunta a SiBlock. Revisa que tu router dé la IP de SiBlock como DNS y desconecta y vuelve a conectar el WiFi del aparato. |
| `ipv6` | Si tu router tiene IPv6, apaga su DNS por IPv6: los aparatos lo usan para saltarse SiBlock. |
| `sin_consultas` | Este aparato no ha preguntado nada a SiBlock en los últimos 10 minutos. |
| `dns_cifrado` | Este aparato usa un DNS cifrado y se salta SiBlock. Apaga "DNS privado" o "DNS seguro" en sus ajustes. |
| `private_relay` | Este aparato usa iCloud Private Relay y se salta SiBlock. Puedes activar la opción de abajo. |
| `en_pausa` | SiBlock está en pausa: no bloquea nada hasta que termine. |
| `sin_lista` | SiBlock todavía no tiene su lista de anuncios. Revisa que tenga internet y espera unos minutos. |
| `sin_internet` | SiBlock no logra hablar con internet. Revisa el módem. |
| `router_dns` | Todo llega desde tu router: así SiBlock no ve cada aparato. Lo mejor es que el router reparta la IP de SiBlock directo. |
| `todo_bien` | No encontré nada raro. |

  Cada hallazgo de router o IPv6 lleva un enlace "Ver la guía" a `https://siblock.s-dev.win/guia-router.html`;
  `dns_cifrado` y `private_relay` a `https://siblock.s-dev.win/obstaculos.html`.
- `#uso` (con sesión): botones `#ver24` "24 horas" / `#ver7` "7 días" (`aria-pressed`); `<svg id="grafica" viewBox>` de barras
  (consultas en gris, bloqueadas en el color de acento encima), `<title>` por barra con
  "hora: N consultas, M bloqueadas"; 24 h = las últimas 24 de `h`; 7 días = `h` agrupada por día
  local (`new Date(hora*3600000)`). Etiquetas del eje: cada 6 h / cada día. Sin hora o todo en
  cero: "Todavía no hay datos. Aparecen cuando SiBlock tenga la hora de internet." Un resumen en
  texto para lectores de pantalla (`aria-label` con totales).
- `#top` (con sesión): "Más bloqueados" — lista ordenada de hasta 10 (`d` y `n`); vacía → "Todavía nada bloqueado."
- `#saltan` (con sesión): "Aparatos que se saltan SiBlock" con IP, qué usa (DNS cifrado / Private Relay) y hace cuánto; vacía → oculta.
- `#relay` (con sesión, en "Bloqueo"): casilla "Apagar iCloud Private Relay en esta casa" +
  aviso "Safari en iPhone y Mac dejará de usar Private Relay en tu WiFi: SiBlock podrá bloquear sus
  anuncios, pero pierdes la privacidad extra de Private Relay." → `POST /api/options`.
- Celular: letra base 16 px, `input` 16 px, botones con `min-height:44px`, tablas dentro de un
  contenedor con `overflow-x:auto`, nada más ancho que 360 px se sale; modo oscuro con las
  variables de hoy.

- [ ] **Paso 1: prueba que falla** en `sim/test_sim.py` (sección 4): `GET /` trae los ids
  `comprobar`, `algo`, `hallazgos`, `grafica`, `ver24`, `ver7`, `top`, `saltan`, `relay`, el texto
  `prueba.siblock.internal`, y no trae `innerHTML`.
- [ ] **Paso 2:** `make -C sim` + simulador + test → FALLA.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** simulador + `python3 sim/test_sim.py` → "TODO BIEN". Abrir `http://127.0.0.1:8080/`
  en un navegador (herramienta de navegador si la hay) a 375 px y a escritorio, claro y oscuro:
  ninguna barra ni texto se sale; la consola no muestra errores de CSP; "Algo no funciona"
  muestra hallazgos. (En el simulador la imagen de prueba falla porque la PC no usa SiBlock como
  DNS: es lo esperado.) Revisión de detalle con `emil-design-eng` o `break-ui` si están.
- [ ] **Paso 5:** commit `Panel: comprobación, algo no funciona, gráficas y celular`.

### Tarea 9: cierre del carril firmware

**Archivos:** ninguno nuevo (arreglos que salgan).

- [ ] **Paso 1:** `security-suite` (auditor de endpoints/auth) sobre los cambios de `src/web.cpp` y
  `src/dns_server.cpp` de esta fase. Revisar en especial: `/api/diagnose` pública (¿filtra algo de
  otro aparato?), excepción de `Host` del GIF, `POST /api/options`, rutas con el `Host` de prueba.
  Una CRÍTICA se arregla aquí con su prueba.
- [ ] **Paso 2:** `make -C test_host test` → 0 fallas; `make -C sim`, simulador, `python3 sim/test_sim.py` → "TODO BIEN"; `pkill -x esp32-sim`.
- [ ] **Paso 3:** `pio run` → anotar RAM y flash; flash < 92 %.
- [ ] **Paso 4:** commit `Fase 2: cierre del carril firmware` (si hubo cambios). Reportar números en el resumen.

---

## Carril site (`site/`)

Un agente, tareas 10 → 13. Skills: `frontend-design`, `web-design-guidelines`, `break-ui`.
Mismo estilo que `site/index.html` y `site/estilos.css`; CSP de `site/_headers` sin cambios (solo
`<script type="module" src="js/…">`, nada en línea, sin `on…=`).

### Tarea 10: datos y lógica del selector de guías

**Archivos:** Crear `site/js/guias-datos.js`, `site/js/guia-router.js`, `site/tests/guia-router.test.mjs`.

**Interfaces (produce):**
```js
// guias-datos.js
export const MARCAS;   // [{id, nombre}] en este orden: tp-link TP-Link, huawei Huawei, zte ZTE,
                       // technicolor Technicolor, arris Arris, netgear Netgear, asus ASUS, mikrotik MikroTik,
                       // proveedor "El módem que me dio mi proveedor", no-se "No sé / otra"
export const PAISES;   // [{id, nombre, proveedores:[{id, nombre}]}]:
                       // MX México: telmex "Telmex (Infinitum)", totalplay Totalplay, izzi izzi, megacable Megacable
                       // CO Colombia: claro Claro, movistar Movistar, tigo Tigo, etb ETB
                       // ES España: movistar Movistar, vodafone Vodafone, orange Orange, digi Digi
                       // US Estados Unidos: xfinity "Xfinity (Comcast)", spectrum Spectrum, att AT&T, verizon Verizon
                       // otro "Otro país": []
export const FICHAS;   // {} vacío: aquí entran fichas verificadas {marca|proveedor: {ips, menu, fuente}} cuando haya fuente
// guia-router.js
export function paisDesdeIdioma(idioma);   // 'es-MX'→'MX', 'es-CO'→'CO', 'es-ES'→'ES', 'en-US'→'US',
                                           // sin región, desconocido, null → 'otro'
export function proveedoresDe(paisId);     // proveedores del país + {id:'otro', nombre:'Otro proveedor'}
export function guiaPara({ marca, pais, proveedor }); // → {titulo, ips:['192.168.1.1','192.168.0.1'],
                                           //    pasos:[…], verificada:boolean, fuente:string|null}
export function pintarGuia(doc, guia);     // solo textContent / createElement
export function iniciar(doc, nav);         // llena selects, elige país por nav.language, pinta al cambiar
```
`guiaPara` sin ficha en `FICHAS` → pasos generales (abajo), `verificada:false`, `fuente:null`; la
página muestra entonces "Falta verificar: esta guía es general para tu marca".
Pasos generales (texto fijo):
1. "Mira la etiqueta de abajo de tu módem: ahí vienen la dirección para entrar (casi siempre 192.168.1.1 o 192.168.0.1), el usuario y la contraseña."
2. "Abre esa dirección en el navegador de un aparato conectado a tu WiFi y entra."
3. "Busca la parte que diga DHCP, LAN o DNS."
4. "Donde dice DNS primario, pon la dirección de tu SiBlock. Deja vacío el DNS secundario: si pones otro, los aparatos lo usan a ratos y se saltan SiBlock."
5. "Si ves algo de DNS por IPv6, apágalo o pon ahí también tu SiBlock."
6. "Guarda, y en cada aparato apaga y prende el WiFi."
7. "Abre tu panel y pulsa Comprobar: debe decir que este aparato usa SiBlock."
Bloque fijo "¿Tu módem no deja cambiar el DNS?": plan A, cambiar el DNS en cada aparato (enlace a
`#por-aparato`); "Algunos proveedores lo abren si llamas a soporte y lo pides". El plan B (que
SiBlock reparta las direcciones) se menciona como "más adelante".

- [ ] **Paso 1: pruebas que fallan** (`node --test`): `paisDesdeIdioma` con los 4 casos, `'es'`, `'fr-FR'`,
  `undefined` → `'otro'`; `proveedoresDe('MX')` termina en `otro` y trae 5; `proveedoresDe('zz')`
  → solo `otro`; `guiaPara({marca:'huawei'})` → `verificada:false`, 7 pasos, `ips` como arriba;
  `pintarGuia` con un `doc` falso y una guía cuyo título es `'<img src=x onerror=alert(1)>'` deja
  ese texto en `textContent` sin crear elementos `img`; todos los `id` de `MARCAS` y proveedores
  son únicos dentro de su lista.
- [ ] **Paso 2:** `cd site && npm test` → FALLA.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** `cd site && npm test` → todo pasa.
- [ ] **Paso 5:** commit `Página: datos y lógica de las guías del router`.

### Tarea 11: página `guia-router.html` y prueba de páginas

**Archivos:** Crear `site/guia-router.html`, `site/tests/paginas.test.mjs`; modificar `site/estilos.css`.

Contenido: encabezado y pie iguales a `index.html` (con enlace de vuelta); `<select id="marca">`,
`<select id="pais">`, `<select id="proveedor">` con `<label>`; `<section id="guia" aria-live="polite">`;
`<section id="por-aparato">` con plan A por sistema: Windows 11, Mac, iPhone (rutas de menú de cada
sistema en palabras simples) y Android con la marca "Falta verificar: el menú cambia según la marca
del teléfono", y el aviso de apagar "DNS privado" en Android; aviso fijo de IPv6; enlace a
`obstaculos.html` y a `fortigate.html` ("¿Tienes un FortiGate?"). Sin capturas.
`paginas.test.mjs` recorre **todos** los `site/*.html` (así cubre las páginas de la tarea 12):
- `<html lang="es">`, `<meta name="viewport"`, `<title>` que contiene "SiBlock";
- ningún `<script>` sin `src`, ningún atributo `on…=`, ningún `javascript:`;
- cada `href="x.html"` o `href="x.html#id"` o `href="#id"` interno existe (archivo e `id`);
- el único correo es `contacto@s-dev.win`;
- enlaces externos con `rel` que incluye `noopener`.

- [ ] **Paso 1:** escribir `paginas.test.mjs` y la página vacía con solo el `<html>` → `npm test` FALLA.
- [ ] **Paso 2:** completar la página y los estilos (selects de 44 px, legible a 360 px, modo oscuro con las variables existentes).
- [ ] **Paso 3:** `cd site && npm test` → todo pasa. Abrir la página (servidor estático local, p. ej. `python3 -m http.server -d site 8000`) a 375 px y escritorio: nada se sale.
- [ ] **Paso 4:** commit `Página: guía del router por marca y proveedor`.

### Tarea 12: páginas `fortigate.html` y `obstaculos.html`

**Archivos:** Crear `site/fortigate.html`, `site/obstaculos.html`; modificar `site/estilos.css` si hace falta.

- `fortigate.html`: los 9 pasos de §2.3 de `docs/ideas-pendientes.md` en orden, con el bloque de
  comandos tal cual en `<pre><code>`, y "Deshacer" (`set dns-service default`). Sin notas sobre
  otros equipos de la red. Aviso arriba: "Para quien administra un
  FortiGate (FortiOS 7.x)".
- `obstaculos.html`: tres secciones con las tablas de §2.5 reescritas en palabras simples
  ("En la red", "En los aparatos", y `id="no-puede"` "Lo que un DNS no puede bloquear":
  anuncios de YouTube, Twitch, Instagram, Facebook y TikTok, porque vienen del mismo lugar que el
  video o las fotos; bloquearlos rompería la app). Columna "Qué hace SiBlock" con "Lo arregla
  solo" (canario de Firefox), "Lo puedes activar" (Private Relay, explicando que es opción),
  "Lo detecta" (DNS cifrado: Chrome, Edge, Android, Windows, iPhone, antivirus) y "Solo con guía".
  "Modo estricto" se describe como "más adelante". Tablas dentro de un contenedor con scroll lateral.
- [ ] **Paso 1:** crear las dos páginas; `cd site && npm test` (la prueba de la tarea 11 las cubre) → pasa.
- [ ] **Paso 2:** revisar a 375 px y escritorio.
- [ ] **Paso 3:** commit `Página: FortiGate y obstáculos`.

### Tarea 13: enlaces desde `index.html`

**Archivos:** Modificar `site/index.html`.

- Paso 4 "Dile a tu router que use SiBlock": botón a `guia-router.html`; enlace a `fortigate.html`.
- Bloque "Qué no puede quitar": enlace a `obstaculos.html#no-puede`.
- Paso nuevo o párrafo "¿Ya funciona?": "Abre tu panel desde el aparato que quieras revisar y
  mira el aviso de arriba; si algo falla, pulsa «Algo no funciona»." (la página no puede
  comprobarlo sola).
- Menú superior: "Guías" → `guia-router.html`.
- [ ] **Paso 1:** cambiar; `cd site && npm test` → pasa (los enlaces existen).
- [ ] **Paso 2:** commit `Página: enlaces a las guías`.

---

## Carril docs (`docs/`, `README.md`)

Un agente, tarea 14. Se escribe a partir de los contratos de este plan (no espera al firmware).
Skill: `engineering:documentation`.

### Tarea 14: documentación de la fase 2

**Archivos:** Modificar `docs/api.md`, `docs/dns.md`, `docs/operacion.md`, `docs/seguridad.md`,
`docs/pruebas.md`, `docs/arquitectura.md`, `docs/ideas-pendientes.md`, `README.md`.

- `api.md`: `/siblock-prueba.gif`, `GET /api/diagnose` (tabla de códigos de la tarea 4),
  `GET /api/history`, `GET/POST /api/options`, campos nuevos de `/api/detail` y `/api/stats`.
- `dns.md`: nombres especiales y en qué orden se revisan; NXDOMAIN; lista de nombres de DNS cifrado
  que se detectan (no se bloquean).
- `operacion.md`: comprobación, "algo no funciona", opción de Private Relay (apagada de fábrica,
  "borrar la configuración" la apaga), gráficas (168 h, se guardan cada hora en `/history.bin`).
- `seguridad.md`: excepción de `Host` del GIF y por qué es segura; `/api/diagnose` pública y qué
  no revela; `/api/history` con sesión; IPs de aparatos que se saltan solo en RAM; riesgo bajo
  aceptado: una página web HTTP podría usar la imagen de prueba para saber si el visitante está
  detrás de una SiBlock.
- `pruebas.md`: pruebas nuevas (host, simulador, página) y lista para la placa: comprobación desde
  un celular real, Firefox con DoH automático, iPhone con Private Relay (opción apagada y encendida),
  Android con DNS privado, gráficas tras un reinicio.
- `arquitectura.md`: módulos nuevos `dns_names`, `usage`, `diagnose` y `/history.bin`.
- `ideas-pendientes.md`: marcar ✅ (fase 2) §3, §3.2-6, §3.2-7, la parte del panel de §3.3,
  §3.5-7, Firefox y Private Relay de §2.5, y las guías de §2/§2.1/§2.3; dejar pendiente lo que
  falta verificar (fichas por marca y proveedor, capturas).
- `README.md`: una línea sobre las guías y el botón "algo no funciona".
- [ ] **Paso 1:** escribir; revisar que no haya correos fuera de `contacto@s-dev.win` (`git grep -n "@" docs README.md`).
- [ ] **Paso 2:** commit `Docs: fase 2`.

---

## Notas de ejecución

- Carriles en paralelo: firmware (1–9, uno a la vez), site (10–13), docs (14). No comparten archivos.
- CI y cloud no tienen trabajo en esta fase: `ci.yml` ya corre `npm test` de `site/` con
  `tests/*.test.mjs` y `test_sim.py` completo.

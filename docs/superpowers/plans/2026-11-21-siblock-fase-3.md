# SiBlock fase 3 ("Cloudflare"): plan de implementación

> **Para agentes:** SUB-SKILL OBLIGATORIA: usa superpowers:subagent-driven-development (recomendado) o
> superpowers:executing-plans para hacer este plan tarea por tarea. Los pasos usan casillas (`- [ ]`).

**Meta:** que la comunidad enseñe a SiBlock qué módem usa cada proveedor y qué no debió bloquearse,
sin recoger nada que identifique a una casa: un Worker con D1 para reportes, avisos de placa,
estadísticas públicas y del dueño, y Contáctanos; en la placa, un número de instalación al azar y
dos opciones apagadas de fábrica (compartir datos anónimos, avisar de versión nueva); en la página,
formularios, aviso de privacidad, estadísticas y analítica sin cookies preparada.

**Arquitectura:** `cloud/` es un Worker TypeScript (`siblock-api`, ruta `siblock.s-dev.win/api/*`)
con D1 (`migrations/`), probado con `vitest` + `@cloudflare/vitest-pool-workers`; toda llamada de
salida (Turnstile, llaves de Access) y el reloj entran por un objeto `Deps` para poder probarlos.
En la placa, la lógica sin Arduino va a `lib/siblock_core/cloud.{h,cpp}` (probada en `test_host`)
y la parte HTTPS a `src/cloud.{h,cpp}`; el simulador habla con un Worker de mentira en Python.
`site/` suma páginas estáticas con módulos JS probados con `node --test`.

**Tecnologías:** ESP32 clásico, Arduino core 2.0.17, PlatformIO, C++17; TypeScript 5.9.3,
wrangler 4.156.0, vitest 4.1.11, @cloudflare/vitest-pool-workers 0.23.0, Node 22; HTML/CSS/JS
sin framework; Python 3 (simulador y scripts de CI).

**Diseño:** [`docs/superpowers/specs/2026-11-21-siblock-design.md`](../specs/2026-11-21-siblock-design.md),
sección "Fase 3 en detalle" (3.1 a 3.3). Fuente: [`docs/ideas-pendientes.md`](../../ideas-pendientes.md)
§2.2, §3.2 (8 y 10), §3.3, §3.4.

## Restricciones globales

- Rama `siblock-fases`. Prohibido: push, merge a `main`, cambiar la visibilidad del repo,
  `wrangler deploy` / `wrangler pages deploy` / `wrangler d1 ... --remote` reales, tocar otros equipos de la red, mandar correos, leer o copiar archivos personales con
  tokens o claves, poner
  correos personales (solo `contacto@s-dev.win`). No leer ni mostrar `src/secrets.h`.
- No cambiar `partitions.csv`. Flash del programa < 92 % en `pio run`.
- Deben pasar siempre: `make -C test_host test`; `make -C sim`, luego
  `cd sim && SIM_FLASH=./sim-flash ./esp32-sim` en segundo plano y `python3 sim/test_sim.py`
  (la FALLA vieja de la sección 8, "menos de 1% sin respuesta", ya existía en la fase 2: no debe
  empeorar ni sumar otras); apagar con `pkill -x esp32-sim` (nunca `pkill -f`); `pio run`;
  `cd site && npm test`; `cd cloud && npm test && npm run tipos`.
- Versiones npm fijas (exactas, sin `^`): las de "Tecnologías". `npm install` solo dentro de
  `cloud/`; se sube `package-lock.json`, no `node_modules/` (agregar a `.gitignore` de `cloud/`).
- Texto que ve la persona: español, palabras simples, sin tecnicismos. Marca **SiBlock**.
- Privacidad: sin cookies; ninguna IP ni MAC se guarda en D1 ni en la placa fuera de RAM; todo lo
  que la placa manda a internet está **apagado de fábrica**; fechas en D1 solo como día.
- Seguridad: cada ruta pensada contra un atacante en la red local (panel) o en internet (Worker).
  Panel: header anti-CSRF `X-Requested-With: adblock`, cuerpo de POST con el tope de 512 B.
- No inventar datos de routers ni proveedores: sin fuente → guía general + "falta verificar".
- Commit al final de cada tarea solo si el orquestador lo pide (pie
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`); nunca push.

## Contratos entre carriles

| Contrato | Valor |
|---|---|
| Base | Worker en `https://siblock.s-dev.win/api/v1/…`; página en la misma dirección |
| Número de instalación (`i`) | 32 caracteres `[0-9a-f]` |
| Fabricantes | `tp-link` `huawei` `zte` `technicolor` `arris` `netgear` `asus` `mikrotik` `proveedor` `no-se` (mismos `id` que `MARCAS` de `site/js/guias-datos.js`) |
| Menús | `internet_wan` `red_local` `avanzado` `otro` |
| Motivos "no debió bloquearse" | `pagina_no_abre` `app_no_funciona` `juego_no_funciona` `otro` |
| Temas de Contáctanos | `no_instala` `no_bloquea` `bloqueo_de_mas` `mi_modem` `otra` |
| Pasos del embudo | `entro` `conecto` `instalo` `wifi` `panel` |
| `POST /api/v1/reporte/modem` | `{"i","origen":"pagina"\|"panel","fabricante","dns_ok":bool,"menu","turnstile"?}` (`turnstile` obligatorio si `origen=pagina`) → 200 `{"ok":true}` |
| `POST /api/v1/reporte/bloqueo` | `{"i","origen","dominio","motivo","turnstile"?}` → 200 `{"ok":true}` |
| `POST /api/v1/placa` | `{"i","v":"vX.Y.Z"\|"dev","b":<entero 0..100000000>}` → 200 `{"ok":true}` |
| `POST /api/v1/paso` | `{"p":"<paso>"}` → 204 |
| `POST /api/v1/contacto` | `{"nombre"?,"correo","tema","mensaje","diagnostico"?,"turnstile"}` → 200 `{"ok":true}` |
| `GET /api/v1/pais` | `{"pais":"MX"\|"","proveedor":"<asOrganization>"\|""}`, `Cache-Control: no-store` |
| `GET /api/v1/estadisticas` | `{"placas7d":n,"versiones":[{"v","n"}],"paises":[{"pais","n"}] (n≥5; "otros" solo si suma≥5),"bloqueadasSemanas":[{"desde":"AAAA-MM-DD","n"}] ×12 la más vieja primero,"modems":[{"pais","proveedor","fabricante","menu","n"}] (dns_ok=1, n≥3, sin "otro"/"no-se")}`, `Cache-Control: public, max-age=300` |
| `GET /api/v1/dueno/resumen` | (Access) `{"embudo":[{"paso","n"}] (30 días, orden del contrato),"temas":[{"tema","n"}],"motivos":[{"motivo","n"}],"bloqueos":[{"dominio","n"}] (top 50),"guiasFaltan":[{"fabricante","n"}],"placasTotal":n}` |
| Errores del Worker | 400 `{"error":"datos"}`, 403 `{"error":"origen"\|"turnstile"\|"acceso"}`, 404 `{"error":"no_existe"}`, 405 `{"error":"metodo"}`, 413 `{"error":"grande"}`, 429 `{"error":"limite"}`, 500 `{"error":"interno"}` |
| `version.json` (estático) | `https://siblock.s-dev.win/version.json` → `{"version":"vX.Y.Z","url":"https://siblock.s-dev.win/#paso-instalar"}`; en el repo `site/version.json` con `v0.0.0`; CI lo reescribe en cada tag |
| `site/js/config.js` | `export const CONFIG = Object.freeze({ api: '/api/v1', analiticaToken: '', turnstileClave: '' });` (CI reescribe los dos últimos) |
| Panel → página | `https://siblock.s-dev.win/reporte.html#i=<32hex>`; `https://siblock.s-dev.win/contacto.html#diag=<base64url de JSON>` (≤ 600 caracteres; JSON `{"v","heap","listErr","codes":[...]}`) |
| API nueva de la placa | `GET /api/cloud` 🔒; `POST /api/cloud` 🔒 `share=0\|1` y/o `versionCheck=0\|1`; `POST /api/report/modem` 🔒 `fabricante&dnsOk=0\|1&menu`; `POST /api/report/blocked` 🔒 `domain&motivo` |
| `/api/cloud` | `{"share":false,"versionCheck":false,"installId":"<32hex>","current":"<SIBLOCK_VERSION>","latest":"","update":false,"lastPing":"never"\|"ok"\|"fail"}` |
| Reportes de la placa | 200 `{"ok":true}`; 409 `{"error":"apagado"}`; 400 `{"error":"datos"}`; 502 `{"error":"sin_internet"\|"rechazado"\|"limite"}` |
| NVS nuevas | `install_id` (string 32 hex), `cloud_share` (u32 0/1), `ver_check` (u32 0/1); falta = 0 |

## Enfoque de revisión (lo que más puede morder)

1. **La placa habla con internet sin permiso** (configuración de fábrica, opción guardada con un
   valor raro, NVS que falla al leer): con `cloud_share`/`ver_check` ausentes, `2` o ilegibles no
   sale **ninguna** petición. Prueba en el simulador (tarea 3: 25 h de reloj sin peticiones al
   Worker de mentira) y en `/api/cloud` con `share=2` (tarea 3).
2. **Respuesta automática usada para mandar correo a terceros** (spam con la dirección de otra
   persona): la respuesta es texto fijo, sin nombre ni mensaje de quien escribe, y solo sale tras
   Turnstile, límite por IP y tope diario. Prueba en la tarea 18.
3. **Cabeceras de correo inyectadas** (`correo` con `\r\nBcc:`, `nombre` con saltos, mensaje con
   `<script>`): correo inválido → 400; asunto fijo; HTML quitado. Prueba en la tarea 18.
4. **Umbrales que se saltan** (un país con 4 placas, un mismo número que reporta 5 veces, "otro"):
   el país no sale, cuenta 1, "otro" nunca sale. Prueba en las tareas 14 y 16.
5. **`#diag=` o `#i=` malos en la página** (base64 roto, JSON enorme, HTML dentro, `i` que no es
   hex): se ignora sin romper la página y nunca se pinta como HTML. Prueba en las tareas 9 y 10.

---

## Carril firmware (`src/`, `lib/`, `sim/`, `test_host/`, `platformio.ini`)

Un solo agente, tareas 1 → 6 en orden. Skills: `esp32`, `superpowers:test-driven-development`;
`security-suite` (auditor de endpoints) en la tarea 6.

### Tarea 1: `siblock_core` — lógica de la nube sin Arduino

**Archivos:** Crear `lib/siblock_core/cloud.{h,cpp}`; modificar `test_host/test_siblock.cpp`.

**Interfaces (produce):**
```cpp
namespace siblock {
bool validInstallId(const std::string& s);                 // 32 de [0-9a-f]
std::string makeInstallId(const uint8_t rnd[16]);           // hex en minúsculas
bool parseSemver(const std::string& s, uint32_t out[3]);    // "vX.Y.Z", cada parte 0..9999, sin ceros raros ni espacios
bool isNewer(const std::string& latest, const std::string& current);  // false si alguna no es vX.Y.Z ("dev" → false)
// Busca la clave "version" y su valor string; false si len > 1024 o el valor no es vX.Y.Z.
bool parseVersionJson(const char* body, size_t len, std::string& version);
bool validReportDomain(const std::string& d);  // 3..253 B, ≥ 2 etiquetas de 1..63 [a-z0-9-], sin '-' al inicio/fin de etiqueta
extern const char* const kFabricantes[]; extern const size_t kFabricantesCount;   // contrato
extern const char* const kMenus[];       extern const size_t kMenusCount;
extern const char* const kMotivos[];     extern const size_t kMotivosCount;
bool inList(const char* const* list, size_t n, const std::string& v);
// Cuerpos JSON para el Worker; devuelven "" si algún campo no es válido (por eso no hace falta escapar).
std::string pingJson(const std::string& id, const std::string& version, uint32_t blocked);
std::string modemReportJson(const std::string& id, const std::string& fabricante, bool dnsOk, const std::string& menu);
std::string blockedReportJson(const std::string& id, const std::string& domain, const std::string& motivo);
// Suma de bloqueadas de las últimas `hours` horas que terminan en nowHour (usa UsageHistory::snapshot).
uint32_t blockedLastHours(const UsageHistory& h, uint32_t nowHour, size_t hours);

class CloudSchedule {
 public:
  static constexpr uint32_t kFirstMs = 60000, kEveryMs = 86400000, kRetryMinMs = 3600000;
  void start(uint32_t nowMs);                             // próximo en kFirstMs
  bool due(uint32_t nowMs) const;                         // resta sin signo (millis da la vuelta)
  void done(bool ok, uint32_t nowMs, uint32_t jitterMs);  // ok: kEveryMs + jitter%3600000; fallo: min(kRetryMinMs << fallos, kEveryMs)
 private:
  uint32_t next_ = 0, base_ = 0; uint8_t fails_ = 0;
};
}
```
`pingJson("ab…", "v1.2.3", 7)` → `{"i":"ab…","v":"v1.2.3","b":7}` exacto; `"dev"` vale como versión;
`modemReportJson` → `{"i":"…","origen":"panel","fabricante":"huawei","dns_ok":true,"menu":"internet_wan"}`;
`blockedReportJson` → `{"i":"…","origen":"panel","dominio":"ads.ejemplo.com","motivo":"otro"}`.

- [ ] **Paso 1: pruebas que fallan** (`testCloud`):
  - `validInstallId`: sí 32 hex minúsculas; no 31/33, mayúsculas, `g`, vacío, con `\0` dentro.
  - `makeInstallId` de 16 bytes 0x00..0x0f → `"000102030405060708090a0b0c0d0e0f"`.
  - `parseSemver`: sí `v0.0.0`, `v1.20.3`, `v9999.0.1`; no `1.2.3`, `v1.2`, `v1.2.3.4`, `v1.2.x`,
    `v10000.0.0`, `v1.2.3 `, `dev`, `v01.2.3`.
  - `isNewer("v1.2.10","v1.2.9")` sí; `("v1.2.3","v1.2.3")` no; `("v2.0.0","dev")` no; `("basura","v1.0.0")` no.
  - `parseVersionJson`: `{"version":"v1.2.3","url":"x"}` y con espacios/saltos → `v1.2.3`;
    `{"url":"version"}` → false; `{"version":1}` → false; 1025 bytes → false; `{"version":"v1.2.3` (cortado) → false.
  - `validReportDomain`: sí `ads.ejemplo.com`, `a-b.c0`; no `ejemplo`, `-a.com`, `a-.com`, `a..com`,
    `ADS.com`, `a_b.com`, 254 B, etiqueta de 64, `a.com.` .
  - Cuerpos: los tres exactos de arriba; cada uno con un campo malo (id corto, `fabricante=linksys`,
    `menu=""`, dominio malo, motivo malo, versión `v1`) → `""`.
  - `blockedLastHours`: historia con 1 bloqueada por hora en `h-30..h` → 24 h = 24, 168 h = 31.
  - `CloudSchedule`: `start(0)` → `due(59999)` no, `due(60000)` sí; `done(true, 60000, 5000)` →
    `due(60000+86400000+4999)` no, `+5000` sí; tres fallos seguidos → esperas 1 h, 2 h, 4 h; 10 fallos
    → tope 24 h; con `start(0xFFFFFFF0)` la vuelta de millis no rompe `due`.
- [ ] **Paso 2:** `make -C test_host test` → falla al compilar.
- [ ] **Paso 3:** implementar `cloud.cpp` (sin `std::regex`, sin parser JSON general).
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: número de instalación, versiones y cuerpos para la nube`.

### Tarea 2: ajustes en NVS, raíces y cliente HTTPS (+ simulador)

**Archivos:** Modificar `src/config.h`, `src/settings.{h,cpp}`, `src/certs.h`; crear
`src/cloud.{h,cpp}`; modificar `sim/shim/HTTPClient.h`, `sim/shim/shim.cpp`, `sim/test_sim.py`.

**Interfaces (produce):**
```cpp
// config.h
#define SIBLOCK_CLOUD_BASE "https://siblock.s-dev.win"
#define NVS_INSTALL_ID "install_id"
#define NVS_CLOUD_SHARE "cloud_share"
#define NVS_VER_CHECK "ver_check"
// settings
String installId();                // la crea (esp_random, makeInstallId) si falta o no es válida
bool cloudShare();  bool setCloudShare(bool on);     // solo el valor 1 cuenta como encendido
bool versionCheck(); bool setVersionCheck(bool on);
// src/cloud.h
namespace cloud {
enum class Result : uint8_t { kOk, kNoNet, kRejected, kLimit };   // kRejected: 4xx≠429 o 5xx; kLimit: 429
Result postJson(const char* path, const std::string& body);       // SIBLOCK_CLOUD_BASE + path, 8 s, User-Agent "SiBlock/<ver>"
bool fetchLatestVersion(std::string& out);                        // GET /version.json, lee ≤ 1024 B
}
```
- `certs.h`: sumar `GTS_Root_R1`, `GTS_Root_R4`, `SSL.com_TLS_RSA_Root_CA_2022`,
  `SSL.com_TLS_ECC_Root_CA_2022` desde `/etc/ssl/certs/*.pem`, con el mismo formato y comentario.
- Simulador: `HTTPClient` suma `addHeader(name, value)` y `int POST(const String&)` (curl con
  `--data-binary @archivo`, `-H` por cada header, `--max-time 8`); `begin()` acepta también
  `http://127.0.0.1:`; si existe `SIM_CLOUD_URL`, una URL que empieza con `https://siblock.s-dev.win`
  se reescribe a ese prefijo (así el firmware no sabe del simulador). Cada petición a la nube se
  anota en el registro del simulador.
- `test_sim.py`: clase `NubeFalsa` (un `http.server` en un hilo, puerto libre) que guarda cada
  petición (método, ruta, headers, cuerpo) y contesta según `respuestas[ruta] = (código, cuerpo)`;
  la placa nueva de la sección 18 arranca con `SIM_CLOUD_URL=http://127.0.0.1:<puerto>`.

- [ ] **Paso 1: prueba que falla** — sección `18. Nube apagada de fábrica` (placa propia, `FRESH`):
  arranca con NVS vacía y `NubeFalsa`; adelanta el reloj 25 h (`.clock_ms`) en pasos de 1 h;
  `check(len(nube.peticiones) == 0, "sin permiso no sale nada")`. Con `cloud_share=2` y
  `ver_check=2` escritos a mano en `nvs.json` y otras 25 h → sigue en 0 peticiones.
- [ ] **Paso 2:** correr el simulador y `python3 sim/test_sim.py --solo-nuevas` → falla (no existe la sección/rutas).
- [ ] **Paso 3:** implementar ajustes, `cloud.cpp` (copiar el patrón de `blocklist.cpp:242-260`:
  `WiFiClientSecure` + `ROOT_CAS`, `useHTTP10`, sin seguir redirecciones) y el simulador.
- [ ] **Paso 4:** `make -C sim`, simulador, `python3 sim/test_sim.py` → la sección 18 en ok; `pio run` compila.
- [ ] **Paso 5:** commit `SiBlock: número de instalación, opciones de nube y cliente HTTPS`.

### Tarea 3: aviso diario, consulta de versión y `/api/cloud`

**Archivos:** Modificar `src/main.cpp`, `src/web.cpp`, `sim/test_sim.py`.

**Interfaces:**
- Consume: tarea 1 (`CloudSchedule`, `pingJson`, `blockedLastHours`, `isNewer`), tarea 2.
- Produce: `GET/POST /api/cloud` según el contrato; dos `CloudSchedule` en `main.cpp` (`pingSched`,
  `verSched`) que arrancan al encender una opción o al arrancar con ella encendida.
- Reglas: el aviso diario sale solo con `cloudShare()` y hora válida (`epochHour != 0`), con
  `b = blockedLastHours(historia, ahora, 24)`; la consulta de versión solo con `versionCheck()`.
  Ninguna corre mientras se baja la lista. `latest`/`lastPing` viven en RAM. `jitter = esp_random() % 3600000`.
  `POST /api/cloud`: cada campo presente debe ser `0` o `1`, si no 400 y no cambia nada; si la NVS
  no guarda, 500 `{"error":"guardar"}`.

- [ ] **Paso 1: pruebas que fallan** (sección 18 sigue):
  - `GET /api/cloud` sin sesión → 401; sin anti-CSRF → 403; con sesión → JSON del contrato con
    `share:false`, `versionCheck:false`, `installId` 32 hex, `lastPing:"never"`.
  - `POST /api/cloud share=2` → 400 y sigue apagado; `share=1` → 200 `share:true`.
  - Adelantar 61 s → la nube recibió `POST /api/v1/placa` con cuerpo `{"i":<installId>,"v":"dev","b":<n>}`
    (solo esas tres claves, `Content-Type: application/json`, `User-Agent` empieza con `SiBlock/`);
    `lastPing:"ok"`. Con la nube contestando 500 → `lastPing:"fail"` y reintento a la hora.
  - `versionCheck=1`, nube con `/version.json` = `{"version":"v9.0.0","url":"…"}` → tras 61 s
    `GET /version.json` **sin cuerpo ni número**; `/api/cloud` da `latest:"v9.0.0"`, `update:false`
    (versión `dev`). `share=0` y `versionCheck=0` → 25 h más sin peticiones nuevas.
- [ ] **Paso 2:** simulador + `test_sim.py --solo-nuevas` → fallan.
- [ ] **Paso 3:** implementar en `main.cpp` (loop) y `web.cpp` (`guarded<true, …>`).
- [ ] **Paso 4:** `make -C sim`, `python3 sim/test_sim.py` → sección 18 ok; `pio run`.
- [ ] **Paso 5:** commit `SiBlock: aviso diario y consulta de versión, apagados de fábrica`.

### Tarea 4: reportes desde el panel

**Archivos:** Modificar `src/web.cpp`, `sim/test_sim.py`.

**Interfaces:** Consume `modemReportJson`, `blockedReportJson`, `cloud::postJson`. Produce
`POST /api/report/modem` y `POST /api/report/blocked` (contrato). Con `cloudShare()` apagado → 409
sin llamar a la nube. Dominio: `trim` + minúsculas antes de `validReportDomain`. `Result` → 200 /
502 `sin_internet` / 502 `rechazado` / 502 `limite`. Un reporte a la vez (si hay uno en curso, 429 local `{"error":"ocupado"}`).

- [ ] **Paso 1: pruebas que fallan** (sección `18b. Reportes desde el panel`):
  - apagado: `fabricante=huawei&dnsOk=1&menu=internet_wan` → 409 y la nube no recibió nada.
  - encendido: lo mismo → 200 y la nube recibió `/api/v1/reporte/modem` con el cuerpo exacto de la tarea 1.
  - `fabricante=linksys`, `dnsOk=2`, `menu=` vacío, `domain=ejemplo`, `domain=a.com&motivo=otra`,
    cuerpo de 600 B → 400 (o 413 por tamaño) y nada sale.
  - `domain=Ads.Ejemplo.com &motivo=pagina_no_abre` → 200, la nube recibió `ads.ejemplo.com`.
  - nube con 429 → 502 `limite`; nube caída → 502 `sin_internet`; sin sesión → 401.
- [ ] **Paso 2–4:** fallan → implementar → `make -C sim`, `test_sim.py` y `pio run` en ok.
- [ ] **Paso 5:** commit `SiBlock: reportes de módem y de bloqueos desde el panel`.

### Tarea 5: panel — "Compartir con SiBlock", aviso de versión y Contáctanos

**Archivos:** Modificar `src/web_page.h`, `sim/test_sim.py`.

**Interfaces:** Consume `/api/cloud`, `/api/report/*`, `/api/diagnose`, `/api/stats`. Sin
`<script>` nuevo en línea fuera del que ya existe ni cambios a la CSP. DOM con `textContent`.

Textos fijos (copiar tal cual):
- Título: **Compartir con SiBlock** (apagado de fábrica).
- Aviso de "Compartir datos anónimos": «Si lo activas, tu SiBlock manda a siblock.s-dev.win una vez
  al día un número al azar de esta instalación (no dice quién eres), la versión de SiBlock y
  cuántos anuncios quitó ese día. Y, solo cuando tú lo mandes, la marca de tu módem, si pudiste
  cambiar el DNS y en qué menú, o el sitio que no debió bloquearse. Nunca manda tu IP, la MAC,
  nombres de aparatos ni qué sitios visitas. El servidor ve tu país y tu compañía de internet por la
  conexión, pero no guarda tu IP. Con esta opción apagada, SiBlock solo se conecta a internet para
  bajar la lista.»
- Aviso de "Avisarme de versiones nuevas": «Si lo activas, SiBlock revisa una vez al día si hay una
  versión nueva. Solo pide un archivo público; no manda nada tuyo.»
- Banner con `update:true`: «Hay una versión nueva de SiBlock (vX.Y.Z). Se instala desde la página
  con el cable USB.» + enlace `https://siblock.s-dev.win/#paso-instalar`.
- Formulario de módem: selects con los fabricantes, "¿Pudiste cambiar el DNS?" (Sí/No) y el menú
  (Internet o WAN / Red local, LAN o DHCP / Avanzado / Otro o no lo encontré). Botón «Mandar».
- En la lista de bloqueados del panel, botón «No debió bloquearse» → elige motivo (Una página no
  abre / Una app no funciona / Un juego no funciona / Otro) → `POST /api/report/blocked`. Si
  compartir está apagado, el botón explica que hay que activarlo antes.
- Enlace «Reportar desde la página» → `reporte.html#i=<installId>`; «Escríbenos» →
  `contacto.html#diag=<base64url>` con `{"v","heap","listErr","codes"}` (de `/api/stats` y
  `/api/diagnose`; ≤ 600 caracteres, si no cabe se quitan `codes`).
- Mensajes del 502: `sin_internet` «SiBlock no pudo llegar a internet. Prueba más tarde.»,
  `limite` «Mandaste muchos seguidos. Espera un minuto.», `rechazado` «No se pudo guardar. Prueba más tarde.»

- [ ] **Paso 1: pruebas que fallan** (`18c. Panel`): la página del panel contiene «Compartir con
  SiBlock», el aviso completo, «No debió bloquearse», `reporte.html#i=`, `contacto.html#diag=`; la
  CSP no cambió; `grep -c '<script' ` sigue igual.
- [ ] **Paso 2–4:** fallan → implementar → `make -C sim`, `test_sim.py`, `pio run` (flash < 92 %).
- [ ] **Paso 5:** revisión con `break-ui` del formulario (anchos de celular, textos largos) y commit
  `SiBlock: panel para compartir, aviso de versión y Contáctanos`.

### Tarea 6: "borrar la configuración", auditoría y números finales

**Archivos:** Modificar `sim/test_sim.py` (y lo que la auditoría pida en `src/`).

- [ ] **Paso 1: prueba** (`18d. Borrar la configuración`): con `share=1`, `versionCheck=1`, BOOT 10 s
  (botón de mentira) → `/api/cloud` da `share:false`, `versionCheck:false` e `installId` **distinto**;
  25 h de reloj sin peticiones.
- [ ] **Paso 2:** si falla, corregir (`factoryReset` ya borra el espacio; revisar RAM: `latest`, schedules).
- [ ] **Paso 3:** `security-suite`, auditor de endpoints sobre `/api/cloud` y `/api/report/*`
  (atacante en la red local: sin sesión, sin anti-CSRF, cuerpos grandes, valores raros). Arreglar
  lo CRÍTICO/ALTO con su prueba.
- [ ] **Paso 4:** `make -C test_host test`, simulador + `test_sim.py` completo, `pio run`; anotar RAM,
  flash y conteos.
- [ ] **Paso 5:** commit `SiBlock: nube apagada tras borrar la configuración`.

---

## Carril site (`site/`)

Un agente, tareas 7 → 11 en orden. Skills: `frontend-design`, `break-ui`, `web-design-guidelines`;
`dataviz` en la tarea 10. Todo módulo nuevo tiene prueba en `site/tests/*.test.mjs`; sin
`<script>` en línea; sin cookies ni `localStorage`.

### Tarea 7: configuración, analítica sin cookies y CSP

**Archivos:** Crear `site/js/config.js` (contrato), `site/js/nube.js`, `site/version.json`
(`{"version":"v0.0.0","url":"https://siblock.s-dev.win/#paso-instalar"}`),
`site/tests/nube.test.mjs`; modificar `site/_headers` y el `<head>` de todas las `site/*.html`.

**Interfaces (produce, `site/js/nube.js`):**
```js
export function cargarAnalitica(doc, config)        // token vacío → no hace nada; si no, agrega <script defer src="https://static.cloudflareinsights.com/beacon.min.js" data-cf-beacon='{"token":"…"}'> una sola vez
export function cargarTurnstile(doc, config)        // clave vacía → false; si no, carga https://challenges.cloudflare.com/turnstile/v0/api.js una vez → true
export async function enviar(ruta, datos, fetchFn = fetch)  // POST JSON a CONFIG.api + ruta → {ok:true} | {ok:false, error:'<código del contrato>'|'sin_internet'}
export function mensajeDeError(error)               // texto simple en español para cada código
export function contarPaso(paso, nav = navigator)   // sendBeacon(CONFIG.api + '/paso', JSON) solo si paso está en la lista del contrato
```
- `_headers`: sumar a la CSP `script-src https://static.cloudflareinsights.com https://challenges.cloudflare.com`,
  `connect-src https://cloudflareinsights.com`, `frame-src https://challenges.cloudflare.com`;
  `form-action 'none'` se queda (los formularios mandan con `fetch`).
- Cada página carga `js/inicio.js` (nuevo, `type="module"`) que llama `cargarAnalitica(document, CONFIG)`.

- [ ] **Paso 1: pruebas que fallan**: token vacío → ningún `<script>` agregado; con token → uno solo
  aunque se llame dos veces, con el token en `data-cf-beacon`; `enviar` con un `fetch` falso: 200 →
  `{ok:true}`, 429 → `{ok:false,error:'limite'}`, red caída → `sin_internet`; `contarPaso('raro')` no
  llama a `sendBeacon`; `paginas.test.mjs`: todas las páginas incluyen `js/inicio.js` y la CSP tiene los tres orígenes nuevos.
- [ ] **Paso 2–4:** `cd site && npm test` falla → implementar → pasa.
- [ ] **Paso 5:** commit `site: analítica sin cookies preparada y ayudas para la nube`.

### Tarea 8: aviso de privacidad

**Archivos:** Crear `site/privacidad.html`; modificar el pie de todas las páginas (enlace
«Privacidad») y `site/tests/paginas.test.mjs`.

Contenido (palabras simples): qué cuenta la página (visitas con Cloudflare Web Analytics, sin
cookies; pasos de la instalación como contadores sin número ni cookie); qué manda la placa y solo
si se activa (el texto de la tarea 5); qué se guarda de los reportes (país, compañía de internet,
marca, si funcionó, menú, sitio reportado, día; nunca IP, MAC, nombre ni ubicación); Contáctanos
(el correo y el mensaje solo los lee el dueño, se borran cuando ya no hacen falta); que se puede no
mandar nada; contacto `contacto@s-dev.win`.

- [ ] **Paso 1:** prueba: existe `privacidad.html` con `id="placa"`, `id="reportes"`, `id="contacto"`,
  sin la palabra "cookie" salvo en "sin cookies"; todas las páginas enlazan a `privacidad.html`.
- [ ] **Paso 2–4:** falla → escribir → pasa.
- [ ] **Paso 5:** commit `site: aviso de privacidad`.

### Tarea 9: formularios de reporte (módem y "no debió bloquearse")

**Archivos:** Crear `site/reporte.html`, `site/js/reporte.js`, `site/tests/reporte.test.mjs`.

**Interfaces (produce, `site/js/reporte.js`):**
```js
export function idDesdeFragmento(hash)     // "#i=<32hex>" → id; lo demás → null
export function idNuevo(cryptoObj = crypto) // 32 hex con getRandomValues
export function cuerpoModem(id, campos)    // → objeto del contrato con origen 'pagina' o null si algo no está en las listas
export function cuerpoBloqueo(id, campos)  // dominio: trim + minúsculas + misma regla que validReportDomain; o null
```
- La página: aviso corto arriba (qué se manda / qué no / enlace a `privacidad.html#reportes`),
  «Tu internet parece ser de: <proveedor>» desde `GET /api/v1/pais` (si falla, no se muestra), dos
  formularios con selects del contrato (nombres de `MARCAS`), Turnstile (si no hay clave: «El
  formulario todavía no está listo» y el botón desactivado), mensajes con `mensajeDeError`.
- [ ] **Paso 1: pruebas que fallan**: `idDesdeFragmento("#i=" + 32hex)` → id; `"#i=ABC"`, `"#i=<b>"`,
  `""`, `"#i=" + 33 hex` → null; `cuerpoModem` con `fabricante:'linksys'` → null; `cuerpoBloqueo`
  con `" Ads.Ejemplo.COM "` → `dominio:'ads.ejemplo.com'`, con `'ejemplo'` o `'<img>'` → null;
  `paginas.test.mjs` encuentra `reporte.html` sin `<script>` en línea.
- [ ] **Paso 2–4:** falla → implementar → `npm test` pasa; `break-ui` en 360 px.
- [ ] **Paso 5:** commit `site: reportes de módem y de bloqueos`.

### Tarea 10: Contáctanos

**Archivos:** Crear `site/contacto.html`, `site/js/contacto.js`, `site/tests/contacto.test.mjs`;
modificar el pie (enlace «Contáctanos» junto al `mailto:` actual).

**Interfaces (produce):**
```js
export function diagDesdeFragmento(hash)  // "#diag=<base64url>" → {v,heap,listErr,codes} solo con esas claves, strings ≤ 100, codes ≤ 20 de [a-z_]; base64/JSON roto o > 600 → null
export function textoDiag(diag)           // líneas "Versión: …" etc. para mostrar con textContent y adjuntar
export function validarContacto(campos)   // {ok:true, cuerpo} | {ok:false, campo:'correo'|'tema'|'mensaje'|'nombre'}
```
Reglas (iguales al Worker): `nombre` opcional ≤ 80; `correo` ≤ 254, un `@`, sin espacios ni saltos;
`tema` del contrato; `mensaje` 10..2000 caracteres; `diagnostico` solo si la casilla «Adjuntar el
diagnóstico de mi SiBlock» está marcada. Tras enviar: «Recibimos tu mensaje. Te contestamos pronto.»
- [ ] **Paso 1: pruebas que fallan**: `diagDesdeFragmento` con un base64url bueno → objeto; con
  `"%%%"`, JSON de 700, claves extra (`{"v":"x","ip":"1.2.3.4"}` → sin `ip`), `codes:["<b>"]` → sin ese
  código; `validarContacto` con `correo:"a@b.c\r\nBcc: x@y.z"` → `campo:'correo'`; mensaje de 9 → `mensaje`.
- [ ] **Paso 2–4:** falla → implementar → pasa; `break-ui` (mensaje largo, emoji, nombre de 80).
- [ ] **Paso 5:** commit `site: Contáctanos con diagnóstico opcional`.

### Tarea 11: estadísticas públicas, panel del dueño y embudo

**Archivos:** Crear `site/estadisticas.html`, `site/dueno.html`, `site/js/graficas.js`,
`site/js/estadisticas.js`, `site/js/dueno.js`, `site/tests/graficas.test.mjs`; modificar
`site/js/instalar.js` (pasos), `site/js/guia-router.js` (país desde `/api/v1/pais`),
`site/index.html` (enlace a estadísticas).

**Interfaces (produce):**
```js
// graficas.js — SVG a mano con createElementNS, sin librerías.
export function barras(doc, filas, { etiqueta, valor, titulo })  // <svg role="img"> con <title>; filas vacías → <p>Todavía no hay datos.</p>
export function semanaCorta(desde)                               // "2026-11-16" → "16 nov"
```
- `estadisticas.html`: placas activas (7 días), versiones, países (tabla con barras), anuncios
  bloqueados por semana, tabla «proveedor → módem → menú». Nota: «Un país aparece cuando tiene 5
  placas o más.»
- `dueno.html`: embudo, temas, motivos, dominios más reportados, guías que faltan. Si la respuesta
  es 403: «Entra con tu correo de dueño.» `<meta name="robots" content="noindex">`.
- Embudo en `instalar.js`: `contarPaso('entro')` al cargar `index.html`, `conecto` al abrir el
  puerto, `instalo` al terminar, `wifi` al guardar el WiFi, `panel` al tocar «Abrir mi panel».
- `guia-router.js`: si `/api/v1/pais` da un país de `PAISES`, ese es el inicial; si no, el del
  idioma como hoy.
- [ ] **Paso 1: pruebas que fallan**: `barras` con 3 filas → 3 `<rect>` y `<title>`; con `[]` → el
  texto; un nombre de dominio con `<script>` sale como texto; `semanaCorta`; con un `fetch` falso,
  `guia-router` elige `CO` si el Worker dice `CO` y el idioma `es-MX` si falla.
- [ ] **Paso 2–4:** falla → implementar → pasa; `break-ui` con 0 placas y con 200 países.
- [ ] **Paso 5:** commit `site: estadísticas, panel del dueño y embudo sin cookies`.

---

## Carril cloud (`cloud/`)

Un agente, tareas 12 → 19 en orden. Skills: `cloudflare:workers-best-practices`, `cloudflare:wrangler`,
`superpowers:test-driven-development`; `cloudflare:turnstile-spin` (tarea 13),
`cloudflare:cloudflare-email-service` (tarea 18), `security-suite` (tarea 19). Nada se despliega.

### Tarea 12: armazón, D1 y enrutador

**Archivos:** Crear `cloud/package.json`, `cloud/package-lock.json`, `cloud/.gitignore`
(`node_modules/`, `.wrangler/`), `cloud/tsconfig.json`, `cloud/wrangler.toml`,
`cloud/vitest.config.ts`, `cloud/test/setup.ts`, `cloud/migrations/0001_inicial.sql`,
`cloud/src/index.ts`, `cloud/src/tipos.ts`, `cloud/src/http.ts`, `cloud/test/http.test.ts`.

- `package.json` scripts: `"test": "vitest run"`, `"tipos": "tsc --noEmit"`; devDependencies exactas.
- `wrangler.toml`: `name = "siblock-api"`, `main = "src/index.ts"`, `compatibility_date = "2026-11-01"`,
  `routes = [{ pattern = "siblock.s-dev.win/api/*", zone_name = "s-dev.win" }]`,
  `[[d1_databases]] binding="DB" database_name="siblock" database_id="REEMPLAZAR-CON-EL-ID-DE-D1" migrations_dir="migrations"`,
  `[[send_email]] name="CORREO"`, `[[ratelimits]] name="LIMITE" namespace_id="1001" simple={limit=5, period=60}`,
  `[[ratelimits]] name="LIMITE_CONTACTO" namespace_id="1002" simple={limit=2, period=60}`,
  `[triggers] crons=["17 3 * * *"]`, `[vars]` `ORIGEN="https://siblock.s-dev.win"`,
  `HOST_PAGINA="siblock.s-dev.win"`, `CONTACTO_DESTINO="contacto@s-dev.win"`,
  `CONTACTO_REMITENTE="contacto@s-dev.win"`, `RESPUESTA_AUTOMATICA="1"`, `ACCESS_TEAM_DOMAIN=""`,
  `ACCESS_AUD=""`. Secretos (no en el archivo): `ID_PEPPER`, `TURNSTILE_SECRET`.
- Esquema (`0001_inicial.sql`):
```sql
CREATE TABLE reportes_modem (instalacion TEXT PRIMARY KEY, origen TEXT NOT NULL CHECK (origen IN ('pagina','panel')),
  pais TEXT NOT NULL, asn INTEGER, proveedor TEXT NOT NULL, fabricante TEXT NOT NULL,
  dns_ok INTEGER NOT NULL CHECK (dns_ok IN (0,1)), menu TEXT NOT NULL, dia INTEGER NOT NULL);
CREATE TABLE reportes_bloqueo (instalacion TEXT NOT NULL, dominio TEXT NOT NULL, motivo TEXT NOT NULL,
  origen TEXT NOT NULL, dia INTEGER NOT NULL, PRIMARY KEY (instalacion, dominio));
CREATE INDEX reportes_bloqueo_dominio ON reportes_bloqueo (dominio);
CREATE TABLE placas (instalacion TEXT PRIMARY KEY, dia INTEGER NOT NULL, version TEXT NOT NULL, pais TEXT NOT NULL);
CREATE INDEX placas_dia ON placas (dia);
CREATE TABLE bloqueadas (dia INTEGER NOT NULL, instalacion TEXT NOT NULL, total INTEGER NOT NULL, PRIMARY KEY (dia, instalacion));
CREATE TABLE pasos (dia INTEGER NOT NULL, paso TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (dia, paso));
CREATE TABLE temas (dia INTEGER NOT NULL, tema TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (dia, tema));
CREATE TABLE contacto_dia (dia INTEGER PRIMARY KEY, n INTEGER NOT NULL);
```
  (`dia` = días desde 1970 en UTC.)

**Interfaces (produce):**
```ts
// tipos.ts
export interface Env { DB: D1Database; CORREO: SendEmail; LIMITE: RateLimit; LIMITE_CONTACTO: RateLimit;
  ID_PEPPER: string; TURNSTILE_SECRET: string; ORIGEN: string; HOST_PAGINA: string; CONTACTO_DESTINO: string;
  CONTACTO_REMITENTE: string; RESPUESTA_AUTOMATICA: string; ACCESS_TEAM_DOMAIN: string; ACCESS_AUD: string }
export interface Deps { fetch: typeof fetch; ahora: () => number }   // ms
// http.ts
export function json(status: number, cuerpo: unknown, cache = "no-store"): Response  // + nosniff, application/json; charset=utf-8
export function error(status: number, codigo: string): Response
export async function leerJson(req: Request, max: number): Promise<unknown | Response>  // >max → 413, no JSON/no objeto → 400
export function origenValido(req: Request, env: Env): boolean   // sin Origin → true; con Origin → igual a env.ORIGEN
export function dia(ms: number): number
export async function claveIp(req: Request, env: Env, ruta: string): Promise<string>  // `${ruta}:` + SHA-256(ID_PEPPER‖CF-Connecting-IP) en hex, 32; nunca se guarda
// index.ts
export async function manejar(req: Request, env: Env, ctx: ExecutionContext, deps: Deps): Promise<Response>
export default { fetch: (r, e, c) => manejar(r, e, c, { fetch, ahora: Date.now }), scheduled: … }
```
- `vitest.config.ts`: pool de Workers con `wrangler.configPath`, `ID_PEPPER="pimienta-de-prueba"`,
  `TURNSTILE_SECRET="secreto-de-prueba"`; `test/setup.ts` aplica las migraciones con
  `applyD1Migrations(env.DB, env.TEST_MIGRATIONS)` (`readD1Migrations` en la config). Seguir la
  forma de configurar de la versión fijada (README de `@cloudflare/vitest-pool-workers@0.23.0`).
  Las pruebas llaman a `manejar(req, envDePrueba, ctx, deps)` con `LIMITE`/`CORREO` falsos.

- [ ] **Paso 1: pruebas que fallan** (`http.test.ts`): ruta desconocida → 404 `no_existe`; `GET
  /api/v1/contacto` → 405; cuerpo de 9000 B a una ruta con tope 1024 → 413; `"[1]"` → 400;
  `Origin: https://malo.example` en POST → 403 `origen`; sin `Origin` → pasa a la ruta; las tablas existen.
- [ ] **Paso 2:** `cd cloud && npm install && npm test` → falla.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** `npm test` y `npm run tipos` en verde.
- [ ] **Paso 5:** commit `cloud: Worker siblock-api con D1 y pruebas locales`.

### Tarea 13: validación, número de instalación, Turnstile y límites

**Archivos:** Crear `cloud/src/opciones.ts`, `cloud/src/validar.ts`, `cloud/src/turnstile.ts`,
`cloud/test/validar.test.ts`, `cloud/test/turnstile.test.ts`.

**Interfaces (produce):**
```ts
export const FABRICANTES, MENUS, MOTIVOS, TEMAS, PASOS: readonly string[]   // contrato
export function esId(s: unknown): s is string                               // 32 [0-9a-f]
export async function hashId(id: string, pepper: string): Promise<string>  // SHA-256(pepper‖id) hex, primeros 32
export function esDominio(s: unknown): s is string                          // misma regla que validReportDomain
export function esVersion(s: unknown): s is string                          // ^v\d{1,4}\.\d{1,4}\.\d{1,4}$ o "dev"
export function esCorreo(s: unknown): s is string                           // ≤254, un @, sin espacios ni controles
export async function verificarTurnstile(token: unknown, env: Env, deps: Deps): Promise<boolean>
  // POST https://challenges.cloudflare.com/turnstile/v0/siteverify (form: secret, response); ok solo si success===true y hostname===env.HOST_PAGINA; token no string o > 2048 → false sin llamar; error de red → false
export async function dentroDelLimite(lim: RateLimit, clave: string): Promise<boolean>
```
- [ ] **Paso 1: pruebas que fallan**: cada lista igual al contrato; `esId`/`esDominio`/`esVersion`/
  `esCorreo` con los mismos casos buenos y malos que la tarea 1 y la 10 (`"a@b.c\r\nBcc: x"` → false);
  `hashId` estable y ≠ id; `verificarTurnstile` con `deps.fetch` falso: `success:true, hostname` bien → true;
  hostname `otro.com` → false; `success:false` → false; fetch que lanza → false; token de 3000 → false sin llamar.
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** commit `cloud: validación, Turnstile y límites`.

### Tarea 14: reportes de módem, `/pais`

**Archivos:** Crear `cloud/src/reportes.ts`, `cloud/test/reportes.test.ts`; modificar `cloud/src/index.ts`.

**Interfaces:** `POST /api/v1/reporte/modem` (tope 1024 B): límite `LIMITE` (`claveIp(…,"reporte")`)
→ 429; validar; `origen=pagina` exige Turnstile → 403 `turnstile`; `pais` = `request.cf.country`
o `""`, `asn` = `cf.asn`, `proveedor` = `cf.asOrganization` cortado a 100 o `""`;
`INSERT … ON CONFLICT(instalacion) DO UPDATE` con `hashId`. `GET /api/v1/pais` → contrato, sin tocar D1.
- [ ] **Paso 1: pruebas que fallan**: reporte de panel válido → 200 y una fila con `instalacion` =
  `hashId` (no el id), `dia` correcto; mismo `i` dos veces con otro menú → sigue 1 fila con el
  menú nuevo; desde página sin token → 403, con token bueno → 200; `fabricante:"linksys"`,
  `dns_ok:"sí"`, `i` corto, clave extra `ip` (se ignora y no se guarda) → 400/200 según el caso;
  `LIMITE` falso que dice no → 429; ninguna columna guarda la IP (revisar `PRAGMA table_info`);
  `/pais` con `cf` de prueba `{country:"MX", asOrganization:"Ejemplo"}` → `{"pais":"MX","proveedor":"Ejemplo"}`.
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** commit `cloud: reportes de módem (uno por instalación)`.

### Tarea 15: "no debió bloquearse", aviso de placa y embudo

**Archivos:** Modificar `cloud/src/reportes.ts`; crear `cloud/src/placas.ts`, `cloud/test/placas.test.ts`;
modificar `cloud/src/index.ts`.

**Interfaces:** `POST /api/v1/reporte/bloqueo` (igual que módem; PK `(instalacion, dominio)`);
`POST /api/v1/placa` (tope 256 B, `LIMITE` con clave `placa`; sin Turnstile; `b` entero 0..100000000):
upsert `placas` (`dia`, `version`, `pais`) y `bloqueadas(dia, instalacion)` = `b` (el último gana);
`POST /api/v1/paso` (tope 128 B, `LIMITE` con clave `paso`): `INSERT … ON CONFLICT DO UPDATE SET n = n + 1` → 204.
- [ ] **Paso 1: pruebas que fallan**: bloqueo `dominio:"ADS.ejemplo.com"` → 400 (la placa y la página
  ya lo mandan en minúsculas); mismo `i`+dominio dos veces → 1 fila; dos `i` distintos → 2; `placa` con
  `v:"v1.2.3", b:10` y luego `b:12` el mismo día → `bloqueadas.total = 12`, 1 fila en `placas`;
  `b:-1`, `b:1.5`, `b:100000001`, `v:"1.2.3"` → 400; `paso:"entro"` ×3 → `n = 3`; `paso:"x"` → 400.
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** commit `cloud: bloqueos reportados, aviso diario de placa y embudo`.

### Tarea 16: estadísticas públicas

**Archivos:** Crear `cloud/src/estadisticas.ts`, `cloud/test/estadisticas.test.ts`; modificar `cloud/src/index.ts`.

**Interfaces:** `GET /api/v1/estadisticas` → contrato. `placas7d` = placas con `dia ≥ hoy − 6`;
`paises` agrupa esas placas, solo `n ≥ 5`, y suma las demás en `{"pais":"otros"}` si esa suma ≥ 5;
`bloqueadasSemanas`: 12 semanas que empiezan en lunes (UTC), la más vieja primero, con 0 si no hay;
`modems`: grupos `(pais, proveedor, fabricante, menu)` con `dns_ok = 1`, `n ≥ 3`, sin `menu='otro'`
ni `fabricante IN ('no-se')`. `Cache-Control: public, max-age=300`.
- [ ] **Paso 1: pruebas que fallan** (con `deps.ahora` fijo): 5 placas en MX y 4 en CO → `MX:5`, sin
  `CO`, sin `otros`; 4 en CO + 1 en PE → `otros:5`; placa de hace 7 días no cuenta; 3 reportes iguales
  → sale el grupo, 2 → no; 3 con `menu:'otro'` → no; `dns_ok:0` → no; 12 semanas siempre.
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** commit `cloud: estadísticas públicas con umbrales`.

### Tarea 17: panel del dueño con Cloudflare Access

**Archivos:** Crear `cloud/src/access.ts`, `cloud/src/dueno.ts`, `cloud/test/access.test.ts`,
`cloud/test/dueno.test.ts`; modificar `cloud/src/index.ts`.

**Interfaces (produce):**
```ts
export async function verificarAccess(req: Request, env: Env, deps: Deps): Promise<boolean>
  // header Cf-Access-Jwt-Assertion; RS256; llaves de `${ACCESS_TEAM_DOMAIN}/cdn-cgi/access/certs` (con deps.fetch);
  // aud incluye ACCESS_AUD, iss === ACCESS_TEAM_DOMAIN, exp > ahora, nbf ≤ ahora (60 s de holgura). Vars vacías → false.
```
`GET /api/v1/dueno/resumen` → 403 `acceso` si no; si sí, el contrato (30 días para embudo, temas y motivos).
- [ ] **Paso 1: pruebas que fallan**: en la prueba se crea un par RSA con WebCrypto y un JWKS falso
  servido por `deps.fetch`: JWT bueno → true; vencido, otra `aud`, otro `iss`, firma de otra llave,
  `alg:"none"`, sin header, vars vacías → false; `resumen` sin JWT → 403; con JWT → embudo en el orden
  del contrato y `bloqueos` ordenados por `n` (máximo 50).
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** commit `cloud: resumen del dueño detrás de Access`.

### Tarea 18: Contáctanos

**Archivos:** Crear `cloud/src/contacto.ts`, `cloud/src/correo.ts`, `cloud/test/contacto.test.ts`;
modificar `cloud/src/index.ts`.

**Interfaces (produce):**
```ts
export function sinHtml(s: string): string   // quita <…>, decodifica &lt; &gt; &amp; &quot; &#39;, quita controles salvo \n, CRLF → \n
export function correoAlDueno(d: Contacto, env: Env): { from, to, replyTo, subject, text }
  // subject fijo: "[SiBlock] " + etiqueta del tema; to = env.CONTACTO_DESTINO; replyTo = d.correo; from = {email: CONTACTO_REMITENTE, name: "SiBlock"}
export function respuestaAutomatica(correo: string, env: Env): { from, to, subject, text }
  // subject "Recibimos tu mensaje"; texto fijo (abajo) — nunca incluye nombre ni mensaje
```
`POST /api/v1/contacto` (tope 8192 B): `LIMITE_CONTACTO` (`claveIp(…,"contacto")`) → 429; validar como
la tarea 10; Turnstile obligatorio; `contacto_dia` del día ≥ 200 → 429; manda al dueño con
`env.CORREO.send(…)`; si falla → 500 `interno` (no cuenta); si sale, suma `contacto_dia` y `temas`, y
con `RESPUESTA_AUTOMATICA="1"` manda la respuesta (si esta falla, igual 200).
Texto de la respuesta automática:
«Hola. Recibimos tu mensaje y te contestamos pronto. Mientras tanto, esto ayuda en los casos más
comunes: Guía del router: https://siblock.s-dev.win/guia-router.html · Problemas comunes:
https://siblock.s-dev.win/obstaculos.html · Si un sitio no abre, en tu panel puedes ponerlo en
"Permitir". Gracias por usar SiBlock.»
- [ ] **Paso 1: pruebas que fallan** (con `CORREO` falso que anota lo mandado): mensaje válido →
  200, 2 correos; el del dueño con `replyTo` = correo, `subject` sin texto de la persona, `text` sin
  `<script>`; la respuesta automática sin el nombre ni el mensaje; `correo` con `\r\n` → 400 y 0
  correos; mensaje de 9 o de 2001 → 400; tema raro → 400; sin Turnstile → 403; tope de 200 → 429;
  `RESPUESTA_AUTOMATICA="0"` → 1 correo; `CORREO.send` que lanza → 500 y `temas` sin cambio;
  `diagnostico` > 1500 → 400; `sinHtml("<b>hola</b> &lt;3")` → `"hola <3"`.
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** commit `cloud: Contáctanos con respuesta automática`.

### Tarea 19: limpieza diaria, DEPLOY.md y auditoría

**Archivos:** Crear `cloud/src/limpieza.ts`, `cloud/test/limpieza.test.ts`, `cloud/DEPLOY.md`.

- `scheduled`: borra filas con `dia < hoy − 400` de `bloqueadas`, `pasos`, `temas`, `contacto_dia`,
  `placas`. Prueba: filas de hace 401 y 399 días → solo queda la de 399.
- `cloud/DEPLOY.md` (pasos a mano del autor, en orden, con el comando o el menú exacto y cómo
  comprobar cada uno): 1) `npx wrangler login`; 2) `npx wrangler d1 create siblock` y pegar el id en
  `wrangler.toml`; `npx wrangler d1 migrations apply siblock --remote`; 3) Email Routing en
  `s-dev.win`: dirección `contacto@s-dev.win` → correo personal verificado (nunca en el repo);
  4) Email Sending: incorporar `s-dev.win` (agrega SPF, DKIM y DMARC en `cf-bounce`); revisar que
  el SPF y el DMARC de `s-dev.win` (`v=DMARC1; p=quarantine; rua=mailto:contacto@s-dev.win`) no
  choquen con los que ya haya; si la cuenta no tiene el plan de pago, `RESPUESTA_AUTOMATICA="0"`;
  si enviar a `contacto@s-dev.win` da `E_RECIPIENT_NOT_ALLOWED`, `npx wrangler secret put
  CONTACTO_DESTINO` con el correo verificado (falta verificar con la cuenta real); 5) Turnstile:
  widget para `siblock.s-dev.win`, `npx wrangler secret put TURNSTILE_SECRET`, clave pública como
  variable `TURNSTILE_SITEKEY` del repo; 6) `npx wrangler secret put ID_PEPPER` (32 bytes al azar:
  `openssl rand -hex 32`; si se pierde, la deduplicación empieza de cero); 7) Access: aplicación
  self-hosted para `siblock.s-dev.win/dueno*` y `siblock.s-dev.win/api/v1/dueno/*`, política solo
  con el correo del dueño (código por correo), sesión de **30 días**; copiar el AUD y el dominio
  del equipo a `ACCESS_AUD` y `ACCESS_TEAM_DOMAIN`; 8) Web Analytics: sitio `siblock.s-dev.win`,
  token como variable `CF_WA_TOKEN` del repo; 9) `npx wrangler deploy` (lo hace el autor) y
  comprobar `curl https://siblock.s-dev.win/api/v1/pais`; si la ruta del Worker no gana a Pages
  (falta verificar), usar Pages Functions como plan B; 10) qué hacer si algo falla y cómo apagar
  todo (quitar la ruta). Sin correos personales, sin ids reales.
- [ ] **Pasos:** prueba de limpieza falla → implementar → pasa; escribir DEPLOY.md;
  `security-suite` (auditor de endpoints + secretos) sobre `cloud/`: arreglar CRÍTICO/ALTO con su
  prueba; `npm test`, `npm run tipos`; `npx wrangler deploy --dry-run --outdir dist` (no despliega)
  compila. Commit `cloud: limpieza diaria y pasos para desplegar`.

---

## Carril ci (`.github/`)

### Tarea 20: archivos de la página en la publicación y pruebas del Worker

**Archivos:** Crear `.github/scripts/make_site_files.py`, `.github/scripts/test_make_site_files.py`;
modificar `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

**Interfaces:** `make_site_files.py --version vX.Y.Z --out <carpeta> [--wa-token T] [--turnstile-sitekey K]`
escribe `<out>/version.json` (contrato) y `<out>/js/config.js` (contrato, valores escapados con
`json.dumps`); sale con 2 si la versión no es `vX.Y.Z` o si un valor tiene algo fuera de `[A-Za-z0-9_-]`.
- [ ] **Paso 1: pruebas que fallan** (`test_make_site_files.py`, mismo estilo que `test_make_manifest.py`):
  versión buena → los dos archivos exactos; sin token → `analiticaToken: ''`; token con `'`/`<` → 2;
  `v1.2` → 2.
- [ ] **Paso 2–4:** falla → implementar → pasa.
- [ ] **Paso 5:** `release.yml`: después de copiar `site/` a `public/`, correr el script con
  `${{ vars.CF_WA_TOKEN }}` y `${{ vars.TURNSTILE_SITEKEY }}` (variables, no secretos: son públicos);
  `ci.yml`: job «Worker (cloud/)» con Node 22: `npm ci`, `npm test`, `npm run tipos` en `cloud/`, y
  correr `test_make_site_files.py` en «Pruebas en la PC». No agregar despliegue del Worker.
  Commit `ci: version.json y config.js en la publicación; pruebas del Worker`.

---

## Carril docs (`docs/`, `README.md`, `LICENSE`)

### Tarea 21: documentación de la fase 3

**Archivos:** Modificar `docs/api.md` (rutas nuevas de la placa y del Worker, con el contrato),
`docs/seguridad.md` (modelo de amenazas del Worker: reportes del panel sin firma y por qué, spam de
la respuesta automática, Access + JWT, límites sin guardar IP, `ID_PEPPER`), `docs/arquitectura.md`
(placa → Worker → D1, qué es opcional), `docs/operacion.md` (opciones del panel, cómo apagarlas,
qué pasa al borrar la configuración), `docs/pruebas.md` (`cd cloud && npm test`, sección 18 del
simulador, lo que falta probar con la placa: certificados de Cloudflare en el ESP32),
`docs/ideas-pendientes.md` (marcar ✅ fase 3 en §2.2, §3.2-8, §3.2-10, §3.3, §3.4 sin borrar lo
pendiente: modelo, OUI, Telegram), `README.md` (una línea: «SiBlock no manda nada a internet salvo
la lista, a menos que tú lo actives», con enlace a `site/privacidad.html` y a `cloud/DEPLOY.md`).
- [ ] Leer el plan y el diseño; escribir; revisar que cada ruta del contrato esté en `api.md` y que
  no aparezca ningún correo distinto de `contacto@s-dev.win`. Commit `docs: fase 3`.

---

## Orden y dependencias

- Carriles en paralelo: firmware (1→6), site (7→11), cloud (12→19), ci (20), docs (21). Solo se
  hablan por la tabla de contratos; nadie toca archivos de otro carril.
- Al final, quien orquesta corre todo junto: `make -C test_host test`, simulador + `test_sim.py`,
  `pio run`, `cd site && npm test`, `cd cloud && npm test && npm run tipos`, y la auditoría de
  `security-suite` del repo entero.

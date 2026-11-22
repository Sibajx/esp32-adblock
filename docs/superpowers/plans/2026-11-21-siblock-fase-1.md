# SiBlock fase 1 ("Instalar sin saber"): plan de implementación

> **Para agentes:** SUB-SKILL OBLIGATORIA: usa superpowers:subagent-driven-development (recomendado) o
> superpowers:executing-plans para hacer este plan tarea por tarea. Los pasos usan casillas (`- [ ]`).

**Meta:** que una persona sin conocimientos instale SiBlock desde el navegador, le dé el WiFi por
USB, cree la contraseña del panel en el primer uso y pueda recuperarse sola (portal por WiFi,
borrar con BOOT), con CI que compila y publica, una página de instalación mínima y el repo listo
para hacerse público.

**Arquitectura:** la lógica nueva que no depende de Arduino (PBKDF2, base64, validaciones,
generador de la contraseña del AP, Improv, botón BOOT, ventana de creación, política del portal)
va en una librería pura nueva `lib/siblock_core/`, probada en `test_host`. El firmware la usa
desde módulos chicos nuevos en `src/` (`settings`, `improv_serial`, `portal`, `button`). El
simulador gana shims (NVS, botón, reloj adelantable, AP, entrada serie) para probar todo de punta
a punta. `site/` es HTML/CSS/JS estático; `.github/` compila y publica.

**Tecnologías:** ESP32 clásico, Arduino core 2.0.17 (`espressif32@6.9.0`), PlatformIO, C++17,
mbedtls (solo en la placa), Python 3 (pruebas del simulador), HTML/CSS/JS sin framework,
ESP Web Tools (unpkg, versión fija), GitHub Actions, Cloudflare Pages (`wrangler pages deploy`).

**Diseño:** [`docs/superpowers/specs/2026-11-21-siblock-design.md`](../specs/2026-11-21-siblock-design.md),
sección "Fase 1 en detalle" (1.1 a 1.11). Las decisiones de 1.11 son del diseño, no del autor.
Fuente de ideas: [`docs/ideas-pendientes.md`](../../ideas-pendientes.md).

## Restricciones globales

- Rama `siblock-fases`. Prohibido: push, merge a `main`, cambiar la visibilidad del repo,
  `wrangler deploy` / `wrangler pages deploy` reales, tocar otros equipos de la red, mandar
  correos, leer o copiar archivos personales con
  tokens o claves, poner correos personales (solo
  `contacto@s-dev.win`).
- No cambiar `partitions.csv`. Flash del programa < 92 % en `pio run`.
- No leer, mostrar ni modificar `src/secrets.h` (es del autor, está en `.gitignore`).
- Deben pasar siempre: `make -C test_host test`; `make -C sim`, luego
  `cd sim && SIM_FLASH=./sim-flash ./esp32-sim` en segundo plano y `python3 sim/test_sim.py`;
  apagar con `pkill -x esp32-sim` (nunca `pkill -f`); `pio run`.
- Texto que ve la persona: español, palabras simples, sin tecnicismos. Marca **SiBlock**.
- Privacidad: sin cookies, sin guardar IPs ni MACs completas, nada sale de la placa a internet de
  fábrica salvo la descarga de la lista (como hoy).
- Seguridad: todo lo que toca auth, formularios o endpoints se piensa contra un atacante en la red
  local (otro aparato de la casa o un vecino al alcance del WiFi).
- No inventar datos de routers o proveedores: guía genérica y "falta verificar".
- Header anti-CSRF sigue siendo `X-Requested-With: adblock` (no se cambia).
- Contraseña del panel: 12 a 64 **bytes**. Sesiones, bloqueo por IP y topes del cuerpo (512 B)
  no cambian.
- Cada tarea termina con commit en la rama, con el pie
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Contratos entre carriles

Estos nombres los usan varios carriles; nadie los cambia sin cambiar este plan.

| Contrato | Valor |
|---|---|
| Versión en el firmware | macro `SIBLOCK_VERSION` (por defecto `"dev"`), inyectada por CI con `PLATFORMIO_BUILD_FLAGS='-DSIBLOCK_VERSION=\"vX.Y.Z\"'` |
| Archivos que deja `pio run` | `.pio/build/esp32dev/bootloader.bin`, `partitions.bin`, `firmware.bin` |
| Manifest (ruta publicada) | `firmware/manifest.json`, junto a `firmware/bootloader.bin` (0x1000), `firmware/partitions.bin` (0x8000), `firmware/firmware.bin` (0x10000) |
| `.bin` unido (solo Release) | `siblock-vX.Y.Z.bin`, `esptool merge_bin` desde 0x0 con las tres partes |
| Carpeta que se publica en Pages | `public/` = copia de `site/` + `firmware/` generado por CI; proyecto de Pages `siblock` |
| Secretos del repo | `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` (el despliegue solo corre si existe el token) |
| Improv propio | comando RPC `0xA0` sin datos → resultado RPC `0xA0` con 2 textos: `["SiBlock-XXXX", "<ap_pass>"]` |
| Improv estado actual | comando `0x02` → paquete de estado; si está conectado, además resultado RPC `0x02` con `["http://<ip>/"]` |
| Improv info | comando `0x03` → resultado con `["SiBlock", SIBLOCK_VERSION, "ESP32", "SiBlock-XXXX"]` |
| API nueva | `POST /api/setup`, `POST /api/password`, `GET /api/backup-ap`, `GET /api/wifi/scan`, `POST /api/wifi`; campos nuevos en `/api/stats`: `version`, `setupOpen`, `portal` |
| Nombres en la red | `siblock`, `siblock.local`; se aceptan también `adblock`, `adblock.local` |
| AP | SSID `SiBlock-XXXX` (4 últimos hex de la MAC en mayúsculas), WPA2, IP 192.168.4.1 |
| BOOT | < 2 s abre la ventana; 3–8 s modo de configuración; ≥ 10 s borra la configuración |

## Enfoque de revisión (lo que más puede morder)

1. **Corte de luz a mitad de guardar la contraseña**: se escribe `panel_salt` y `panel_iter`
   primero y `panel_hash` al final; `hasPanelPassword()` exige los tres. Un NVS con sal y sin
   hash se trata como "sin contraseña". Prueba en la tarea 6 (`nvs.json` a medias).
2. **SSID y contraseñas raras**: UTF-8 (`Casa de Mamá ñ`), espacios, `&`, `%`, `"`, SSID de 32
   bytes justos, red abierta (contraseña vacía). Se guardan byte por byte y el escaneo los
   devuelve bien escapados. Pruebas en las tareas 2 y 9.
3. **Contraseña del panel con `:` o acentos** en el login Basic: se parte en el **primer** `:`
   (el usuario no lleva `:`), el largo se cuenta en bytes. Prueba en la tarea 6.
4. **`millis()` que da la vuelta a los 49 días** en la ventana de creación, el botón y la
   política del portal: todo con restas sin signo. Prueba en la tarea 3 con `now` cerca de
   `0xFFFFFFFF`.
5. **Rebote del botón BOOT**: un pulso de menos de 50 ms no cuenta como pulsación (no abre la
   ventana). Prueba en la tarea 3.

---

## Carril firmware (`src/`, `lib/`, `sim/`, `test_host/`, `platformio.ini`)

Un solo agente, tareas en orden 1 → 10. Skill: `esp32` y `superpowers:test-driven-development`.

### Tarea 1: `siblock_core` — PBKDF2, base64, validaciones y contraseña del AP

**Archivos:**
- Crear: `lib/siblock_core/siblock_crypto.h`, `lib/siblock_core/siblock_crypto.cpp`
- Crear: `lib/siblock_core/siblock_text.h`, `lib/siblock_core/siblock_text.cpp`
- Crear: `test_host/test_siblock.cpp`
- Modificar: `test_host/Makefile` (nuevo binario `test_siblock`; `test` corre los dos)
- `.gitignore` es del carril docs (tarea 15 agrega `test_host/test_siblock`); aquí no se toca y no
  se hace `git add` del binario.

**Interfaces (producen):**
```cpp
namespace siblock {
// SHA-256 y PBKDF2-HMAC-SHA256 portátiles (PC y simulador). En la placa (ESP_PLATFORM)
// pbkdf2Sha256 usa mbedtls_pkcs5_pbkdf2_hmac con MBEDTLS_MD_SHA256.
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void pbkdf2Sha256(const uint8_t* pass, size_t passLen, const uint8_t* salt, size_t saltLen,
                  uint32_t iterations, uint8_t out[32]);
bool equal32(const uint8_t a[32], const uint8_t b[32]);   // tiempo constante
bool pbkdf2SelfTest();                                    // vector RFC 7914 c=1
std::string toHex(const uint8_t* p, size_t n);
bool fromHex(const std::string& s, uint8_t* out, size_t n); // false si largo o dígito malo
// Texto
bool base64Decode(const char* in, size_t len, std::string& out); // false si inválido
bool validPanelPassword(const std::string& pw);            // 12..64 bytes y != "CAMBIA_ESTA_CLAVE"
bool validWifi(const std::string& ssid, const std::string& pass); // ssid 1..32 B; pass 0 u 8..63 B
constexpr const char kApAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789"; // 31, sin i l o 0 1
std::string makeApPassword(uint32_t (*rnd)());             // 10 caracteres, muestreo con rechazo
std::string apSsid(const uint8_t mac[6]);                  // "SiBlock-" + hex mayúsculas de mac[4], mac[5]
}
```

- [ ] **Paso 1: pruebas que fallan** en `test_host/test_siblock.cpp` (mismo `CHECK` que `test_core.cpp`):
  - `sha256("abc")` → `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
  - `pbkdf2Sha256("passwd","salt",1)` → `55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc`.
  - `pbkdf2Sha256("Password","NaCl",80000)` → `4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56`.
  - `pbkdf2Sha256("clave-de-prueba", bytes 00..0f, 10000)` → `7b2fee5e1607395507ca2347b2d3b5ff43036febbe50d3d487e3d30a72ef4cd5`.
  - `pbkdf2SelfTest()` es true; `equal32` distingue un solo bit cambiado.
  - `base64Decode("YWRtaW46Y2xhdmU6Y29uOmRvc+KCrA==")` → `"admin:clave:con:dos€"`; rechaza `"YW*"`, `"Y"`, relleno en medio.
  - `validPanelPassword`: 11 B no, 12 B sí, 64 B sí, 65 B no, `"CAMBIA_ESTA_CLAVE"` no, `"ñññññññ"` (14 B) sí.
  - `validWifi`: `("", "")` no; `("Casa", "")` sí (red abierta); `("Casa","1234567")` no; 8 y 63 B sí; 64 B no; SSID de 32 B sí, 33 no.
  - `makeApPassword` con un `rnd` falso que devuelve `0,1,2,…`: largo 10, todo en `kApAlphabet`; con un `rnd` que devuelve primero 255 (byte ≥ 248) ese valor se descarta.
  - `apSsid({0x24,0x6f,0x28,0xab,0x0c,0x3f})` → `"SiBlock-0C3F"`.
- [ ] **Paso 2:** `make -C test_host test` → falla al compilar (no existen los archivos).
- [ ] **Paso 3:** implementar. SHA-256 según FIPS 180-4; HMAC con ipad/opad precalculados por
  iteración no es necesario (claridad sobre velocidad en la PC). La rama `#ifdef ESP_PLATFORM`
  de `pbkdf2Sha256` usa `mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1)`
  + `mbedtls_pkcs5_pbkdf2_hmac`. `makeApPassword` toma bytes de `rnd()` de 8 en 8 bits y rechaza
  los ≥ 248 (31 × 8).
- [ ] **Paso 4:** `make -C test_host test` → ambas pruebas imprimen 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: PBKDF2, base64, validaciones y contraseña del AP`.

### Tarea 2: `siblock_core` — Improv Serial

**Archivos:**
- Crear: `lib/siblock_core/improv.h`, `lib/siblock_core/improv.cpp`
- Modificar: `test_host/test_siblock.cpp`

**Protocolo** (https://www.improv-wifi.com/serial/ — el implementador lo confirma ahí):
paquete = `"IMPROV"` + versión `0x01` + tipo + largo + datos + checksum (suma de todos los bytes
anteriores, mod 256); al enviar se agrega `'\n'` al final. Tipos: `0x01` estado, `0x02` error,
`0x03` comando RPC, `0x04` resultado RPC. Datos de RPC: id, largo, y textos con prefijo de largo.
Estados: `0x02` listo, `0x03` conectando, `0x04` conectado. Errores: `0x00` ninguno, `0x01` RPC
inválido, `0x02` comando desconocido, `0x03` no se pudo conectar, `0xFF` desconocido.

**Interfaces (producen):**
```cpp
namespace siblock::improv {
enum : uint8_t { kState = 1, kError = 2, kRpc = 3, kRpcResult = 4 };
enum : uint8_t { kReady = 2, kProvisioning = 3, kProvisioned = 4 };
enum : uint8_t { kErrNone = 0, kErrInvalidRpc = 1, kErrUnknownCmd = 2, kErrNoConnect = 3, kErrUnknown = 0xFF };
enum : uint8_t { kCmdWifi = 1, kCmdState = 2, kCmdInfo = 3, kCmdScan = 4, kCmdBackupAp = 0xA0 };
struct Packet { uint8_t type = 0; uint8_t len = 0; uint8_t data[255]; };
class Parser {
 public:
  enum Status { kNone, kPacket, kBadChecksum };
  Status feed(uint8_t byte, Packet& out);  // ignora todo lo que no sea un paquete
};
struct Command { uint8_t id = 0; std::vector<std::string> args; };
bool parseRpc(const Packet& p, Command& out);          // false si los largos no cuadran
std::vector<uint8_t> buildPacket(uint8_t type, const uint8_t* data, uint8_t len);
std::vector<uint8_t> buildState(uint8_t state);
std::vector<uint8_t> buildError(uint8_t err);
std::vector<uint8_t> buildRpcResult(uint8_t cmd, const std::vector<std::string>& strings); // vacío si > 255 B
}
```

- [ ] **Paso 1: pruebas que fallan** (bytes exactos, en hex):
  - Comando WiFi `ssid="Casa"`, `pass="clave123"`:
    `49 4d 50 52 4f 56 01 03 10 01 0e 04 43 61 73 61 08 63 6c 61 76 65 31 32 33 25` →
    `feed` devuelve `kPacket` en el último byte; `parseRpc` da `id=1`, `args={"Casa","clave123"}`.
  - El mismo paquete con el último byte `0x26` → `kBadChecksum`, y el siguiente paquete bueno se lee bien.
  - El mismo paquete precedido de `"STATS,10,2,161000,120000\n[wifi] conectado\n"` y con
    `"STATS,..."` entre dos paquetes → se leen los dos.
  - Paquete cortado a la mitad seguido de uno completo → se recupera y lee el completo.
  - `buildState(kReady)` = `49 4d 50 52 4f 56 01 01 01 02 e2 0a`.
  - `buildRpcResult(1, {"http://192.168.1.50/"})` =
    `49 4d 50 52 4f 56 01 04 17 01 15 14 68 74 74 70 3a 2f 2f 31 39 32 2e 31 36 38 2e 31 2e 35 30 2f 05 0a`.
  - Comando `0xA0` sin datos `49 4d 50 52 4f 56 01 03 02 a0 00 83` → `id=0xA0`, `args` vacío.
  - `parseRpc` rechaza: largo interno mayor que el paquete; texto que se sale.
  - SSID UTF-8 `"Casa de Mamá ñ"` y contraseña vacía viajan byte por byte (ida con `buildPacket`, vuelta con `parseRpc`).
- [ ] **Paso 2:** `make -C test_host test` → falla.
- [ ] **Paso 3:** implementar. El parser es una máquina de estados por byte; al ver una `I` vuelve a
  buscar la cabecera sin perder bytes que puedan empezar otro paquete.
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: Improv Serial`.

### Tarea 3: `siblock_core` — botón BOOT, LED, ventana de creación y política del portal

**Archivos:**
- Crear: `lib/siblock_core/modes.h`, `lib/siblock_core/modes.cpp`
- Modificar: `test_host/test_siblock.cpp`

**Interfaces (producen):**
```cpp
namespace siblock {
enum class ButtonAction { kNone, kOpenSetupWindow, kConfigMode, kFactoryReset };
class ButtonTracker {           // 50 ms de antirrebote
 public:
  void update(bool pressed, uint32_t nowMs);
  ButtonAction take();          // devuelve y limpia la acción pendiente
  bool held() const;
  uint32_t heldMs(uint32_t nowMs) const;
};
// Al soltar: 50 ms..<2 s → kOpenSetupWindow; 3..8 s → kConfigMode; resto → kNone.
// kFactoryReset se dispara al llegar a 10 s todavía apretado (sin esperar a soltar), una sola vez.
bool ledWhileHeld(uint32_t heldMs, uint32_t nowMs); // <3 s: parpadeo 100 ms; 3..8 s: fijo; 8..10 s: parpadeo 100 ms; ≥10 s: apagado
class SetupWindow {             // 30 min
 public:
  void open(uint32_t nowMs);
  bool isOpen(uint32_t nowMs) const;
  uint32_t secondsLeft(uint32_t nowMs) const;
};
class PortalPolicy {
 public:
  // hasSavedWifi, staConnected, apStations: estado actual. Llamar en cada vuelta.
  void tick(uint32_t nowMs, bool hasSavedWifi, bool staConnected, int apStations);
  void forceOpen(uint32_t nowMs);   // BOOT 3–8 s
  void improvSeen(uint32_t nowMs);  // reinicia la espera de 2 min sin WiFi guardado
  bool wantPortal() const;
};
}
```
Reglas de `PortalPolicy`: sin WiFi guardado y sin conexión → abre a los 120 s (contando desde el
arranque o el último `improvSeen`); con WiFi guardado sin conexión → abre a los 600 s seguidos;
abierto con 0 estaciones durante 900 s → cierra y el conteo de 600 s vuelve a empezar; abierto y
`staConnected` → cierra; con estaciones conectadas no cierra.

- [ ] **Paso 1: pruebas que fallan:** pulso de 30 ms → `kNone`; 1 s → `kOpenSetupWindow`; 2,5 s →
  `kNone`; 5 s → `kConfigMode`; 9 s → `kNone`; mantener 10 s → `kFactoryReset` antes de soltar y
  solo una vez; `ledWhileHeld` en 1 s, 4 s, 9 s, 11 s; `SetupWindow` abierta en 29:59, cerrada en
  30:00; reabierta con `open`; todos los casos de `PortalPolicy` de arriba; y los mismos casos de
  ventana y portal con `nowMs` arrancando en `0xFFFFFFFF - 60000` (vuelta de `millis()`).
- [ ] **Paso 2:** `make -C test_host test` → falla.
- [ ] **Paso 3:** implementar con restas `uint32_t` (`now - start >= dur`), nunca comparando `now` con un vencimiento.
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5:** commit `siblock_core: botón, ventana de creación y política del portal`.

### Tarea 4: shims del simulador y dos binarios

**Archivos:**
- Crear: `sim/shim/Preferences.h` (+ implementación en `sim/shim/shim.cpp`)
- Modificar: `sim/shim/Arduino.h` (`pinMode`, `digitalRead`, `digitalWrite`, `INPUT`, `INPUT_PULLUP`, `OUTPUT`, `LOW`, `HIGH`; `SerialPort::available()`, `read()`, `write(const uint8_t*, size_t)`), `sim/shim/shim.cpp` (`millis` con adelanto), `sim/shim/WiFi.h` (AP y escaneo), `sim/Makefile`
- Crear: `sim/secrets.h` NO se toca (sigue siendo la semilla de `esp32-sim`).

**Interfaces (producen):**
- `Preferences`: `bool begin(const char* ns, bool readOnly=false)`, `void end()`, `bool isKey(const char*)`,
  `String getString(const char*, const String& def = String())`, `size_t putString(const char*, const String&)`,
  `size_t getBytes(const char*, void*, size_t)`, `size_t putBytes(const char*, const void*, size_t)`,
  `uint32_t getUInt(const char*, uint32_t def=0)`, `size_t putUInt(const char*, uint32_t)`,
  `bool remove(const char*)`, `bool clear()`. Guarda en `$SIM_FLASH/nvs.json` con forma
  `{"<espacio>": {"<clave>": "<texto>"}}`; bytes como `"hex:<hex>"`; enteros como texto decimal.
  Se reescribe entero en cada `put`/`remove`/`clear` (escribir a `nvs.json.tmp` y `rename`).
- Reloj: `millis()` = reloj real + entero leído de `$SIM_FLASH/.clock_ms` (si existe; se relee como
  mucho cada 200 ms reales).
- Botón: `digitalRead(0)` = `LOW` si existe `$SIM_FLASH/.boot`, si no `HIGH`. `digitalWrite(2, v)`
  imprime `[sim] led <0|1>` solo cuando cambia.
- Serie: si `SIM_SERIAL_STDIN=1`, `available()/read()` leen de stdin sin bloquear (`O_NONBLOCK`);
  si no, `available()` = 0. `write` escribe bytes crudos a stdout con el mismo mutex.
- WiFi: `WIFI_AP`, `WIFI_AP_STA`, `softAP(ssid, pass)` (imprime `[sim] AP <ssid>`; nunca la
  contraseña), `softAPdisconnect(bool)`, `softAPIP()` = 127.0.0.1, `softAPgetStationNum()` =
  `SIM_AP_STATIONS` (0 por defecto), `macAddress(uint8_t*)` = `24:6f:28:ab:0c:3f`,
  `scanNetworks()` = 2 redes fijas (`"CasaDemo"` −50 dBm cifrada, `"Café \"Ñandú\""` −70 dBm
  abierta), `SSID(i)`, `RSSI(i)`, `encryptionType(i)`, `scanDelete()`. Si `SIM_WIFI=down`,
  `status()` = `WL_DISCONNECTED`; `begin()` con SSID `"simulador-ok"` lo pasa a conectado (para
  probar el portal).
- `sim/Makefile`: `make` (objetivo por defecto `all`) arma `esp32-sim` (con `sim/secrets.h`) y
  `esp32-sim-fresh` (sin `secrets.h`, en `build-fresh/`). Ambos suman
  `../lib/siblock_core/*.cpp` y `-I../lib/siblock_core`.

- [ ] **Paso 1:** agregar a `sim/test_sim.py` la sección `0. Simulador` que comprueba que
  `nvs.json` existe tras arrancar y que `sim/esp32-sim-fresh` existe y es ejecutable.
- [ ] **Paso 2:** `make -C sim` → falla `esp32-sim-fresh` (no hay objetivo).
- [ ] **Paso 3:** implementar shims y Makefile. `.gitignore` de `sim/build-fresh/` y
  `sim/esp32-sim-fresh` lo agrega el carril docs (tarea 15); no hacer `git add` de esos binarios.
- [ ] **Paso 4:** `make -C sim`, levantar `esp32-sim`, `python3 sim/test_sim.py` → 0 fallas; `pkill -x esp32-sim`.
- [ ] **Paso 5:** commit `sim: NVS, botón, reloj adelantable, AP y binario sin secrets.h`.

### Tarea 5: `settings` (NVS), `secrets.h` opcional, versión y nombre SiBlock

**Archivos:**
- Crear: `src/settings.h`, `src/settings.cpp`
- Modificar: `src/main.cpp`, `src/config.h`, `src/secrets.example.h`, `platformio.ini` (sin cambios de particiones; solo si hace falta `-I lib/siblock_core`)

**Interfaces:**
- Consume: tarea 1 (`pbkdf2Sha256`, `equal32`, `validPanelPassword`, `validWifi`, `makeApPassword`, `apSsid`, `pbkdf2SelfTest`).
- Produce (`src/settings.h`):
```cpp
namespace settings {
constexpr uint32_t kPanelIterations = 10000;   // medir en la placa (fase 4)
void begin();                                  // Preferences "siblock"
bool hasWifi();
String wifiSsid();
String wifiPass();
bool setWifi(const String& ssid, const String& pass);   // valida con validWifi
bool hasPanelPassword();                       // exige panel_salt, panel_iter y panel_hash
bool setPanelPassword(const String& pw);       // valida; sal 16 B de esp_random; escribe sal, iter y al final hash
bool checkPanelPassword(const String& pw);     // PBKDF2 con la sal e iter guardadas; equal32
String apPassword();                           // genera y guarda ap_pass si falta
String apName();                               // apSsid(MAC de la placa)
void seedFromSecrets();                        // solo llena claves vacías con WIFI_SSID/WIFI_PASSWORD/WEB_PASSWORD si están definidos y son válidos
void factoryReset();                           // borra el espacio "siblock" entero
}
```
- `config.h`: `#ifndef SIBLOCK_VERSION` → `#define SIBLOCK_VERSION "dev"`; `DEVICE_HOSTNAME "siblock"`;
  `LEGACY_HOSTNAME "adblock"`. Claves NVS: `wifi_ssid`, `wifi_pass`, `panel_hash` (32 B),
  `panel_salt` (16 B), `panel_iter` (uint), `ap_pass`.
- `main.cpp`: `#if __has_include("secrets.h")` incluye; si no, nada (sin `#error`). Al arrancar:
  `=== SiBlock <versión> ===`, `settings::begin()`, `seedFromSecrets()`, `pbkdf2SelfTest()` (imprime
  `[clave] autoprueba ok` o `[clave] AUTOPRUEBA FALLÓ`), imprime `[ap] respaldo: <apName> / <apPassword>`.
  El WiFi deja de bloquear: si `hasWifi()` → `WiFi.begin(ssid, pass)` y sigue; DNS y web arrancan
  igual. Se quita el reinicio a los 10 min de `watchWifi` (lo reemplaza el portal en la tarea 9;
  mientras tanto solo reintenta a mano a los 2 min). `/api/stats` suma `"version"`.
- `secrets.example.h`: dice que es opcional y que el WiFi y la contraseña se dan desde la página.

- [ ] **Paso 1: pruebas que fallan** en `sim/test_sim.py`, sección `11. Configuración guardada`:
  - con `esp32-sim` (semilla de `sim/secrets.h`): `nvs.json` tiene `siblock.panel_hash`,
    `panel_salt`, `panel_iter = "10000"`, `wifi_ssid = "simulador"` y **no** contiene el texto
    `clave-de-prueba` en ninguna parte; `/api/stats` trae `"version": "dev"`.
  - el login con `clave-de-prueba` sigue dando token (sección 4 existente, sin cambios).
- [ ] **Paso 2:** correr el simulador y `test_sim.py` → fallan las nuevas.
- [ ] **Paso 3:** implementar `settings` y los cambios de `main.cpp`/`config.h`. `web.cpp` todavía
  compara con `WEB_PASSWORD` hasta la tarea 6: dejar `#include "secrets.h"` condicional ahí también.
- [ ] **Paso 4:** `make -C test_host test`; `make -C sim`; simulador + `test_sim.py` → 0 fallas;
  `pio run` compila. Que compile sin `secrets.h` lo prueba `esp32-sim-fresh` (y el CI, que nunca
  tiene `src/secrets.h`); no mover ni renombrar `src/secrets.h` del autor.
- [ ] **Paso 5:** commit `firmware: configuración en NVS, secrets.h opcional, versión`.

### Tarea 6: panel con hash, crear contraseña en el primer uso y cambiarla

**Archivos:**
- Modificar: `src/web.cpp`, `src/web.h`, `src/web_page.h`, `src/main.cpp`
- Modificar: `sim/test_sim.py`

**Interfaces:**
- Consume: `settings::*` (tarea 5), `SetupWindow` (tarea 3), `base64Decode` (tarea 1).
- Produce:
  - `web::begin(BlockList*, DnsServer*, volatile bool* updateRequested, siblock::SetupWindow* window)`.
  - Login Basic propio: lee `Authorization: Basic <b64>`, decodifica, parte en el primer `:`, usuario
    `admin`, `settings::checkPanelPassword(resto)`. Cuenta para el bloqueo por IP igual que hoy.
  - Sin contraseña guardada, toda ruta privada → 403 `{"ok":false,"error":"primero crea tu contraseña"}`.
  - `POST /api/setup` (Host, CSRF, cuerpo ≤ 512): 409 `"ya hay contraseña"` si existe; 403
    `"Pulsa el botón BOOT de tu SiBlock una vez y vuelve a intentar"` si la ventana está cerrada; 400
    `"la contraseña debe tener entre 12 y 64 caracteres"`; si no, guarda y responde
    `{"ok":true,"token":"<32 hex>"}`.
  - `POST /api/password` (privada, cuerpo `current`, `new`): `current` mala → 401 y cuenta para el
    bloqueo por IP; `new` inválida → 400; bien → guarda, borra **todas** las sesiones y responde
    token nuevo.
  - `GET /api/backup-ap` (privada) → `{"ssid":"SiBlock-XXXX","pass":"<ap_pass>"}`.
  - `/api/stats` suma `"setupOpen": <bool>` (sin contraseña y ventana abierta) y conserva `passwordSet`.
  - `hostOk()` acepta IP, `siblock`, `siblock.local`, `adblock`, `adblock.local`, `PANEL_EXTRA_HOST`.
  - Página: título y encabezado **SiBlock**; si `!passwordSet && setupOpen` muestra "Crea tu
    contraseña" (dos campos, botón "Guardar"); si `!passwordSet && !setupOpen` muestra el texto del
    403; sección "Cambiar contraseña"; tarjeta "Contraseña de respaldo del WiFi de SiBlock" que se
    llena desde `/api/backup-ap` ("Guárdala: la necesitas si un día cambias tu WiFi"); versión al pie.
    Todo con `textContent`. CSP sin cambios.
- Ventana: `main.cpp` la abre en `setup()`.

- [ ] **Paso 1: pruebas que fallan** en `sim/test_sim.py`:
  - Ajustar sección 4: la página contiene `SiBlock`; `Host: siblock.local` → 200; `adblock.local` sigue 200.
  - Nueva sección `12. Primer uso` que lanza `sim/esp32-sim-fresh` con `subprocess.Popen` (puertos
    `SIM_DNS_PORT=5363`, `SIM_WEB_PORT=8090`, `SIM_FLASH=<tmp>`, `SIM_HTTP_FAIL=1`, copia
    `sim/sim-flash/list.bin` si existe) y antes de arrancar escribe en `<tmp>/nvs.json`
    `{"siblock":{"wifi_ssid":"simulador","wifi_pass":"simulador"}}`. Comprueba:
    `passwordSet=false` y `setupOpen=true`; `/api/detail` → 403; `/api/setup` sin CSRF → 403;
    contraseña de 11 → 400; `"clave:con:dos€€"` → 200 con token; segundo `/api/setup` → 409;
    login Basic con `clave:con:dos€€` → 200; `/api/password` con `current` mala → 401; con la buena →
    200, el token viejo ya da 401 y el nuevo sirve; `/api/backup-ap` → `ssid == "SiBlock-0C3F"` y
    `pass` de 10 caracteres del alfabeto.
  - Ventana con reloj falso: otra instancia fresca; escribir `<tmp>/.clock_ms` = `1860000` (31 min)
    → `setupOpen=false` y `/api/setup` → 403 con el texto de BOOT. (El BOOT corto se prueba en la tarea 7.)
  - NVS a medias: instancia fresca con `nvs.json` que tiene `panel_salt` y `panel_iter` pero no
    `panel_hash` → `passwordSet=false`.
  - Las instancias lanzadas se cierran con `Popen.terminate()` y `wait()`; nunca `pkill`.
- [ ] **Paso 2:** `make -C sim`, simulador + `test_sim.py` → fallan las nuevas.
- [ ] **Paso 3:** implementar; quitar `#include "secrets.h"` y `WEB_PASSWORD` de `web.cpp`.
- [ ] **Paso 4:** `make -C test_host test`, simulador + `test_sim.py` → 0 fallas; `pio run` (flash < 92 %).
- [ ] **Paso 5:** skill `security-suite`, al menos el auditor de auth/endpoints sobre `src/web.cpp`
  y `src/settings.cpp`; arreglar lo CRÍTICO/ALTO antes del commit y anotar el resto en el mensaje.
- [ ] **Paso 6:** commit `panel: contraseña con hash, primer uso con ventana y cambio de contraseña`.

### Tarea 7: botón BOOT, LED y borrar la configuración

**Archivos:**
- Crear: `src/button.h`, `src/button.cpp`
- Modificar: `src/main.cpp`, `sim/test_sim.py`

**Interfaces:**
- Consume: `ButtonTracker`, `ledWhileHeld`, `SetupWindow` (tarea 3), `settings::factoryReset` (tarea 5).
- Produce: `namespace button { void begin(); siblock::ButtonAction poll(uint32_t nowMs); }`.
  `begin()` hace `pinMode(0, INPUT_PULLUP)` y `pinMode(2, OUTPUT)` **después** del arranque; GPIO0
  nunca como salida. `poll` mueve el LED con `ledWhileHeld` mientras está apretado y lo apaga al soltar.
  En `main.cpp`: `kOpenSetupWindow` → `window.open(now)` y `[boot] ventana de 30 min para crear la contraseña`;
  `kConfigMode` → `portalPolicy.forceOpen(now)` (se conecta en la tarea 9; hasta entonces solo imprime);
  `kFactoryReset` → `[boot] borrando la configuración`, `settings::factoryReset()`, `ESP.restart()`.
  `/list.bin` y `/custom.txt` no se tocan.

- [ ] **Paso 1: pruebas que fallan**, sección `13. Botón BOOT` con instancias frescas:
  - Ventana cerrada (`.clock_ms` = 31 min), crear `<tmp>/.boot`, esperar 0,5 s, borrarlo → `setupOpen=true`.
  - Borrar: instancia fresca con contraseña creada por `/api/setup` y una regla propia agregada;
    crear `.boot`, subir `.clock_ms` 11 s, esperar a que el proceso salga con código 3; `nvs.json`
    ya no tiene `siblock.panel_hash` ni `ap_pass`; `custom.txt` sigue existiendo con la regla;
    relanzar → `passwordSet=false`.
- [ ] **Paso 2:** correr → fallan.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** test_host, simulador + `test_sim.py` → 0 fallas; `pio run`.
- [ ] **Paso 5:** commit `firmware: botón BOOT (ventana, modo de configuración, borrar) y LED`.

### Tarea 8: Improv Serial en el firmware

**Archivos:**
- Crear: `src/improv_serial.h`, `src/improv_serial.cpp`
- Modificar: `src/main.cpp`, `sim/test_sim.py`

**Interfaces:**
- Consume: `siblock::improv::*` (tarea 2), `settings::*`, `PortalPolicy::improvSeen` (tarea 3).
- Produce: `namespace improv_serial { void begin(siblock::PortalPolicy* policy); void loop(); }`.
  `loop()` lee todo lo disponible de `Serial` y responde:
  - `kCmdWifi`: valida con `validWifi` (inválido → error `0x01`); manda estado `kProvisioning`;
    `WiFi.begin` con los datos nuevos y espera hasta 20 s; si conecta → `setWifi`, estado
    `kProvisioned` y resultado `kCmdWifi` con `["http://<ip>/"]`; si no → error `0x03` y vuelve al
    WiFi guardado (no guarda los datos malos).
  - `kCmdState`: estado actual (`kProvisioned` si conectado, si no `kReady`); si conectado, además
    resultado `kCmdState` con `["http://<ip>/"]`.
  - `kCmdInfo`: `["SiBlock", SIBLOCK_VERSION, "ESP32", apName()]`.
  - `kCmdScan`: un resultado por red `[ssid, rssi en texto, "YES"|"NO"]` y uno vacío al final.
  - `kCmdBackupAp` (`0xA0`): `[apName(), apPassword()]`.
  - Otro id → error `0x02`. Checksum malo → error `0x01`.
  Todo paquete visto llama `policy->improvSeen(now)`. La línea `STATS` y los mensajes siguen igual.

- [ ] **Paso 1: pruebas que fallan**, sección `14. Improv por USB`: lanzar `esp32-sim-fresh` con
  `SIM_SERIAL_STDIN=1` y `stdin=PIPE`; mandar los bytes del comando `0xA0` de la tarea 2 → en stdout
  aparece un resultado RPC `0xA0` con `SiBlock-0C3F` y una contraseña de 10 caracteres (buscar
  `b"IMPROV"` en la salida y decodificar con un parser Python de 15 líneas en el propio test);
  comando WiFi con `ssid="simulador-ok"`, `pass="clave123"` → estado `0x03`, luego `0x04` y
  resultado con `http://127.0.0.1/`; `nvs.json` tiene `wifi_ssid = "simulador-ok"`; comando `0x02`
  → estado `0x04` + URL; comando `0x77` → error `0x02`; las líneas `STATS,` siguen saliendo.
- [ ] **Paso 2:** correr → fallan.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** test_host, simulador + `test_sim.py` → 0 fallas; `pio run` (anotar flash y RAM).
- [ ] **Paso 5:** commit `firmware: Improv Serial (WiFi por USB y contraseña de respaldo)`.

### Tarea 9: modo de configuración por WiFi (AP + portal cautivo)

**Archivos:**
- Crear: `src/portal.h`, `src/portal.cpp`, `src/portal_page.h`
- Modificar: `src/dns_server.h`, `src/dns_server.cpp`, `src/web.cpp`, `src/main.cpp`
- Modificar: `lib/adblock_core/adblock_core.h`, `lib/adblock_core/adblock_core.cpp`, `test_host/test_core.cpp`, `sim/test_sim.py`

**Interfaces:**
- Consume: `PortalPolicy` (tarea 3), `settings::*`, `jsonEscape`.
- Produce:
  - `adblock::buildAddressResponse(const uint8_t* query, const DnsQuestion& q, uint32_t ipv4, uint8_t* out)`:
    A → `ipv4` con TTL 0; otros tipos → NOERROR sin respuestas. Prueba en `test_core.cpp`
    (A da 192.168.4.1, AAAA vacío, ID y pregunta copiados).
  - `DnsServer::setCaptive(uint32_t apIp)` (0 = apagado). Encendido: toda consulta de un cliente de
    la red del AP se contesta con `buildAddressResponse`; `isLocalClient` acepta también la red del AP.
  - `namespace portal { void start(); void stop(); bool active(); bool isApClient(IPAddress ip); }`.
    `start()`: `WiFi.mode(WIFI_AP_STA)`, `softAP(apName(), apPassword())` (WPA2), `dns.setCaptive`,
    imprime `[portal] abierto: <apName>` (sin contraseña). `stop()` lo deshace.
  - Rutas en `web.cpp` mientras `portal::active()`:
    - Host que no sea `192.168.4.1`/`siblock.local`/permitidos → 302 a `http://192.168.4.1/`.
    - `GET /` → `kPortalPage` (título "Configura el WiFi de SiBlock"; lista de redes, campo de
      contraseña, botón "Conectar"; mismo CSP que el panel; todo con `textContent`).
    - `GET /api/wifi/scan` (Host, CSRF) → `{"networks":[{"ssid":"…","rssi":-50,"open":false}]}`
      con `jsonEscape`, máx. 20 redes.
    - `POST /api/wifi` (Host, CSRF, cuerpo ≤ 512, `ssid`, `pass`) → 403 si `!isApClient(ip)`;
      400 si `!validWifi`; si no `setWifi`, 200 `{"ok":true}` y a los 2 s `portal::stop()` + `WiFi.begin`.
    - Fuera del portal, `/api/wifi*` → 404.
  - `main.cpp`: `portalPolicy.tick(...)` en cada vuelta; `wantPortal()` abre/cierra. `/api/stats` suma `"portal": <bool>`.
  - En el simulador `softAPIP()` es 127.0.0.1, así que la "red del AP" es 127.0.0.0/24 y
    `SIM_CLIENT_IP=192.168.1.50` hace de vecino de la casa.

- [ ] **Paso 1: pruebas que fallan:** `test_core.cpp` para `buildAddressResponse`; sección
  `15. Modo de configuración` con `esp32-sim-fresh`, `SIM_WIFI=down`, NVS vacío:
  - a los 119 s (`.clock_ms`) `portal=false`; a los 121 s `portal=true` y la salida tiene `[portal] abierto: SiBlock-0C3F` y no la contraseña del AP.
  - DNS `example.com` → `127.0.0.1`; `GET /` con `Host: connectivitycheck.gstatic.com` → 302 a `http://192.168.4.1/`.
  - `GET /` con Host `192.168.4.1` → 200 y contiene `Configura el WiFi de SiBlock`.
  - `/api/wifi/scan` → dos redes; la segunda trae `Café "Ñandú"` bien escapada y `open=true`.
  - `POST /api/wifi` con `ssid="Casa de Mamá ñ & 100%"`, `pass="1234567"` → 400; sin CSRF → 403.
  - Otra instancia igual lanzada con `SIM_CLIENT_IP=192.168.1.50`: `POST /api/wifi` válido → 403.
  - `POST /api/wifi` con `ssid="simulador-ok"`, `pass="clave123"` → 200; a los 3 s `portal=false`
    y `nvs.json` tiene `wifi_ssid="simulador-ok"`.
  - Cierre solo: instancia con WiFi guardado `"malo"` y `SIM_WIFI=down`: a los 601 s `portal=true`;
    a los 601+901 s `portal=false`.
- [ ] **Paso 2:** correr → fallan.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** test_host, simulador + `test_sim.py` → 0 fallas; `pio run`.
- [ ] **Paso 5:** skill `security-suite`, auditor de endpoints/formularios sobre `src/portal.cpp`,
  `src/web.cpp`, `src/improv_serial.cpp`; arreglar CRÍTICO/ALTO.
- [ ] **Paso 6:** commit `firmware: modo de configuración por WiFi con portal`.

### Tarea 10: cierre del carril firmware

**Archivos:** solo los que haga falta arreglar.

- [ ] **Paso 1:** `make -C test_host clean test` → 0 fallas.
- [ ] **Paso 2:** `make -C sim`, levantar `esp32-sim` en segundo plano, `python3 sim/test_sim.py` → 0 fallas; `pkill -x esp32-sim`.
- [ ] **Paso 3:** `pio run` → éxito; anotar RAM y flash (flash < 92 %).
- [ ] **Paso 4:** `PLATFORMIO_BUILD_FLAGS='-DSIBLOCK_VERSION=\"v0.0.0-prueba\"' pio run` y `strings .pio/build/esp32dev/firmware.bin | grep v0.0.0-prueba` → aparece.
- [ ] **Paso 5:** `git grep -n "WEB_PASSWORD\|WIFI_SSID" src/` solo aparece en `settings.cpp` (semilla) y `secrets.example.h`.
- [ ] **Paso 6:** devolver al orquestador: RAM %, flash %, número de pruebas de cada suite. Commit si hubo arreglos.

---

## Carril site (`site/`)

Un agente. Skills: `frontend-design`, `web-design-guidelines`, `break-ui`. Puede ir en paralelo con
el firmware: solo usa los contratos de arriba.

### Tarea 11: página de instalación

**Archivos:**
- Crear: `site/index.html`, `site/estilos.css`, `site/js/instalar.js`, `site/_headers`,
  `site/favicon.svg`, `site/package.json` (solo `"type":"module"` y `"scripts":{"test":"node --test tests/"}`), `site/tests/instalar.test.mjs`

**Interfaces:**
- `site/js/instalar.js` exporta `puedeInstalar(nav) -> boolean` (`'serial' in nav`) y
  `export function prepararPagina(doc, nav)` que muestra `#instalar` u `#sin-soporte`.
- ESP Web Tools: `<script type="module" src="https://unpkg.com/esp-web-tools@<X.Y.Z>/dist/web/install-button.js?module">`,
  con versión exacta 10.x publicada hace más de 2 semanas (confirmar con `npm view esp-web-tools time --json`);
  `<esp-web-install-button manifest="firmware/manifest.json">` con un botón propio "Instalar SiBlock".
- `site/_headers`: para `/*` — `Content-Security-Policy: default-src 'self'; script-src 'self' https://unpkg.com; connect-src 'self' https://unpkg.com; img-src 'self' data:; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`,
  `Referrer-Policy: no-referrer`, `X-Content-Type-Options: nosniff`, `Permissions-Policy: interest-cohort=()`;
  para `/firmware/*` — `Cache-Control: no-cache`. Si ESP Web Tools necesita otro origen, ajustar y anotarlo.
- Contenido (español simple, en este orden, con `id`s):
  1. `#que-es`: qué es SiBlock en 3 líneas y qué **no** bloquea (anuncios de YouTube, Twitch,
     Instagram, Facebook y TikTok, porque vienen del mismo sitio que el video o las fotos).
  2. `#compra`: placa ESP32 clásico (WROOM-32, 4 MB) y cuáles no sirven (ESP8266, placas sin USB);
     cable **de datos**; chip USB CH340 o CP2102 y drivers por sistema (enlaces a los sitios de
     los fabricantes, marcados "falta verificar" si no se comprobó el enlace); cargador de 5 V.
     Sin precios inventados ni ligas de afiliado.
  3. `#instalar`: botón; `#sin-soporte` (oculto salvo sin WebSerial): "Para instalar usa Chrome o
     Edge en una computadora. Firefox y Safari todavía no pueden."
  4. `#despues`: dar el WiFi (lo pide el mismo instalador), contraseña de respaldo (tarea 12),
     "Abrir mi panel" (tarea 12) y crear la contraseña del panel.
  5. `#router`: guía general para poner la IP de SiBlock como DNS en el router y apagar el DNS por
     IPv6; "Las guías por marca llegan pronto." Nada de menús de marcas concretas.
  Pie: licencia GPLv3, enlace al código, `contacto@s-dev.win`.
- Sin cookies, sin analítica, sin fuentes externas. Modo oscuro con `prefers-color-scheme`. Legible
  a 360 px de ancho sin desplazamiento horizontal.

- [ ] **Paso 1:** prueba `site/tests/instalar.test.mjs`: `puedeInstalar({serial:{}})` true,
  `puedeInstalar({})` false; con un `doc` falso (objetos con `hidden`) `prepararPagina` deja
  `#sin-soporte` visible y `#instalar` oculto cuando no hay `serial`, y al revés.
- [ ] **Paso 2:** `cd site && node --test tests/` → falla.
- [ ] **Paso 3:** implementar la página.
- [ ] **Paso 4:** `cd site && node --test tests/` → pasa; abrir `site/index.html` con un servidor
  local (`python3 -m http.server -d site 8765`) en el navegador del panel y revisar a 360 px y
  1280 px, claro y oscuro; skill `break-ui` con textos largos; `web-design-guidelines`.
- [ ] **Paso 5:** commit `site: página de instalación de SiBlock`.

### Tarea 12: contraseña de respaldo y "Abrir mi panel" por Improv

**Archivos:**
- Crear: `site/js/improv-lite.js`, `site/tests/improv-lite.test.mjs`
- Modificar: `site/index.html`, `site/js/instalar.js`

**Interfaces:**
- `improv-lite.js` exporta:
  - `paquete(tipo, datos: Uint8Array) -> Uint8Array` (con checksum y `\n`);
  - `comando(id, textos: string[]) -> Uint8Array` (RPC tipo 0x03);
  - `class Lector { agregar(bytes: Uint8Array): Array<{tipo, datos}> }` (ignora texto como `STATS,…`,
    descarta checksum malo);
  - `leerTextos(datos) -> {comando, textos: string[]}` para resultados RPC (UTF-8);
  - `async pedirRespaldo(port) -> {red, clave, panel|null}`: abre el puerto a 115200, espera 2 s,
    manda `0xA0` y `0x02`, junta respuestas hasta 8 s, cierra el puerto.
- Página: tras instalar, botón "Ver mi contraseña de respaldo" (pide el puerto con
  `navigator.serial.requestPort()`), muestra red y contraseña con "Guárdala: la necesitas si un día
  cambias tu WiFi", y si llegó la URL, botón "Abrir mi panel" (solo se acepta `http://` + IPv4 +
  `/`; cualquier otra cosa no se enlaza). Si falla: "Desconecta y vuelve a conectar la placa y
  prueba otra vez. También la ves en tu panel."

- [ ] **Paso 1:** pruebas con los bytes exactos de la tarea 2: `comando(0xA0, [])` =
  `49 4d 50 52 4f 56 01 03 02 a0 00 83 0a`; `comando(1, ["Casa","clave123"])` es el paquete de la
  tarea 2 + `0a`; `Lector` con `"STATS,1,2,3,4\n"` + resultado de URL de la tarea 2 → un paquete y
  `leerTextos` da `["http://192.168.1.50/"]`; checksum malo → nada; un resultado `0xA0` con
  `["SiBlock-0C3F","abcdefghjk"]` armado con `paquete` se lee de vuelta; validación de URL rechaza
  `javascript:alert(1)` y `http://evil.example/`.
- [ ] **Paso 2:** `cd site && node --test tests/` → falla.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** `node --test tests/` → pasa; revisar la página otra vez.
- [ ] **Paso 5:** commit `site: contraseña de respaldo y enlace al panel por Improv`.

---

## Carril ci (`.github/`)

Un agente. Skill: `ecc:github-ops`. Prohibido correr `wrangler` de verdad o hacer push.

### Tarea 13: CI en cada push y PR

**Archivos:** Crear `.github/workflows/ci.yml`.

- Disparadores: `push` (todas las ramas) y `pull_request`. `permissions: contents: read`.
- Trabajo `host` (ubuntu-24.04): `actions/checkout@v4`; `make -C test_host test`.
- Trabajo `sim`: `sudo apt-get install -y python3-dnspython`; `make -C sim`;
  `(cd sim && SIM_FLASH=./sim-flash ./esp32-sim > sim.log 2>&1 &)`; esperar a que
  `curl -fs http://127.0.0.1:8080/api/stats` conteste (máx. 120 s, la lista se baja de internet);
  `python3 sim/test_sim.py`; siempre al final `pkill -x esp32-sim || true` y subir `sim/sim.log` como artefacto si falla.
- Trabajo `firmware`: `actions/setup-python@v5` (3.12), `pip install platformio==<versión exacta 6.1.x>`,
  cache de `~/.platformio` con `actions/cache@v4` (clave por `platformio.ini`), `pio run`;
  falla si la línea `Flash:` supera 92,0 % (script de una línea con `grep -oP` sobre la salida).
- Fijar todas las acciones por versión mayor de GitHub (`@v4`, `@v5`).

- [ ] **Paso 1:** escribir el archivo.
- [ ] **Paso 2:** `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"` → sin error;
  si hay `actionlint` disponible por `go run`/descarga, **no** se descarga (no está instalado): basta YAML válido y revisión.
- [ ] **Paso 3:** correr a mano en local los mismos comandos de cada trabajo (`make -C test_host test`,
  simulador + `test_sim.py`, `pio run`) para confirmar que los pasos son correctos.
- [ ] **Paso 4:** commit `ci: pruebas, simulador y compilación en cada push`.

### Tarea 14: publicación en cada versión

**Archivos:** Crear `.github/workflows/release.yml`, `.github/scripts/make_manifest.py`,
`.github/scripts/test_make_manifest.py`.

**Interfaces:**
- `make_manifest.py --version vX.Y.Z --out <dir>` escribe `<dir>/manifest.json`:
```json
{"name":"SiBlock","version":"vX.Y.Z","new_install_prompt_erase":true,
 "builds":[{"chipFamily":"ESP32","improv":true,"parts":[
   {"path":"bootloader.bin","offset":4096},
   {"path":"partitions.bin","offset":32768},
   {"path":"firmware.bin","offset":65536}]}]}
```
  Rechaza versiones que no sean `^v\d+\.\d+\.\d+$` (sale con código 2). Confirmar los nombres de
  campo con la documentación de la versión fijada de ESP Web Tools (si `improv` ya no va en
  `builds`, moverlo o quitarlo y anotarlo).
- `release.yml`: `on: push: tags: ['v*.*.*']`; `permissions: contents: write`.
  1. checkout, Python, PlatformIO (misma versión que ci.yml), `test -f src/secrets.h && exit 1 || true`.
  2. `PLATFORMIO_BUILD_FLAGS='-DSIBLOCK_VERSION=\"${GITHUB_REF_NAME}\"' pio run`.
  3. `mkdir -p public/firmware`; copiar las tres partes; `python3 .github/scripts/make_manifest.py --version "$GITHUB_REF_NAME" --out public/firmware`.
  4. `python -m esptool --chip esp32 merge_bin -o siblock-${GITHUB_REF_NAME}.bin 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin` (esptool de `~/.platformio/packages/tool-esptoolpy` o `pip install esptool==<exacta 4.x>`).
  5. `gh release create "$GITHUB_REF_NAME" siblock-*.bin public/firmware/* --title "SiBlock $GITHUB_REF_NAME" --notes "…"` con `GH_TOKEN: ${{ github.token }}`.
  6. `cp -r site/. public/` (sin pisar `public/firmware`).
  7. Despliegue: trabajo/paso con `env: CLOUDFLARE_API_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN }}`,
     `CLOUDFLARE_ACCOUNT_ID: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}` y
     `if: ${{ env.CLOUDFLARE_API_TOKEN != '' }}`; corre `npx --yes wrangler@<versión exacta> pages deploy public --project-name siblock --branch main`.
     Si no hay token, un paso que imprime "Sin CLOUDFLARE_API_TOKEN: no se publica la página".
- Comentario arriba del archivo: el token debe tener solo permiso "Cloudflare Pages: Edit" en esa cuenta.

- [ ] **Paso 1:** prueba `test_make_manifest.py` (unittest): `v1.2.3` produce exactamente el JSON de
  arriba; `1.2.3`, `v1.2`, `v1.2.3; rm` salen con código 2.
- [ ] **Paso 2:** `python3 .github/scripts/test_make_manifest.py` → falla.
- [ ] **Paso 3:** implementar script y `release.yml`.
- [ ] **Paso 4:** pruebas pasan; YAML válido; en local, sin publicar: `pio run`, armar `public/` en
  el scratchpad con los pasos 3, 4 y 6 y comprobar que `merge_bin` produce un archivo y que
  `public/firmware/manifest.json` existe. **No** correr `wrangler` ni `gh release`.
- [ ] **Paso 5:** commit `ci: publicación de versiones y despliegue a Pages (si hay token)`.

---

## Carril docs (`docs/`, `README.md`, `LICENSE`, `.gitignore`)

Un agente. Skill: `engineering:documentation`. Puede escribir desde este plan sin esperar al
código; al final se revisa contra el código real (tarea 16, paso 3).

### Tarea 15: licencia, README y `.gitignore`

**Archivos:** Crear `LICENSE`; modificar `README.md`, `.gitignore`.

- [ ] **Paso 1:** `cp /usr/share/common-licenses/GPL-3 LICENSE` y comprobar
  `sha256sum LICENSE` = `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`
  (texto completo de la GPLv3, sin cambios).
- [ ] **Paso 2:** README nuevo: título **SiBlock**; arriba "Instálalo desde la página:
  https://siblock.s-dev.win" (con nota "la página se publica con la primera versión"); qué hace y qué
  no bloquea; qué necesitas (placa, cable de datos, Chrome o Edge); pasos cortos; contacto
  `contacto@s-dev.win`; línea de licencia: "SiBlock es software libre bajo la licencia GPLv3
  (ver `LICENSE`). El nombre y el logo de SiBlock no los cubre la licencia."; abajo, sección
  "Para desarrolladores" con lo técnico actual (tabla de archivos actualizada con `settings`,
  `improv_serial`, `portal`, `button`, `lib/siblock_core/`, `site/`, `.github/`; `secrets.h` opcional).
- [ ] **Paso 3:** `.gitignore` suma `test_host/test_siblock`, `sim/build-fresh/`, `sim/esp32-sim-fresh`,
  `sim/sim.log`, `site/node_modules/`, `public/`.
- [ ] **Paso 4:** `git grep -nE "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[a-z]{2,}" -- README.md docs LICENSE`
  solo muestra `contacto@s-dev.win`, `noreply@anthropic.com` o direcciones de la propia GPL (`fsf.org`).
- [ ] **Paso 5:** commit `docs: licencia GPLv3 y README de SiBlock`.

### Tarea 16: documentación técnica de la fase 1

**Archivos:** Modificar `docs/api.md`, `docs/seguridad.md`, `docs/operacion.md`, `docs/pruebas.md`,
`docs/arquitectura.md`, `docs/README.md`.

- [ ] **Paso 1:** escribir, con los contratos y las interfaces de este plan:
  - `api.md`: `/api/setup`, `/api/password`, `/api/backup-ap`, `/api/wifi/scan`, `/api/wifi`,
    campos nuevos de `/api/stats`; Improv (comandos 1–4 y `0xA0`).
  - `seguridad.md`: amenazas nuevas y cómo se cubren: crear la contraseña antes que el dueño
    (ventana de 30 min + BOOT), vecino y AP (WPA2, `ap_pass` al azar, `/api/wifi` solo desde el AP),
    USB físico (Improv y `0xA0` = mismo poder que BOOT 10 s), hash PBKDF2 (sal 16 B, iteraciones
    guardadas), corte de luz al guardar; riesgos que quedan (HTTP del panel, el portal sin HTTPS).
  - `operacion.md`: BOOT < 2 s / 3–8 s / ≥ 10 s y el LED; qué hacer si se cambia el WiFi; qué borra
    y qué no el borrado; actualizar por USB desde la página (no borra el WiFi).
  - `pruebas.md`: suites nuevas (`test_siblock`, secciones 11–15 del simulador, `esp32-sim-fresh`,
    `.clock_ms`, `.boot`, `SIM_SERIAL_STDIN`, `SIM_WIFI`, `SIM_AP_STATIONS`); lista para la placa
    (1.10): Windows, Mac y Linux con CH340 y CP2102, Improv, AP, BOOT corto/medio/largo, RAM y flash,
    tiempo real de PBKDF2 con 10 000 iteraciones.
  - `arquitectura.md`: módulos nuevos y arranque sin bloqueo del WiFi.
  - `docs/README.md`: índice con lo nuevo.
- [ ] **Paso 2:** todo en español simple donde lo lea una persona; lo técnico solo en `docs/`.
- [ ] **Paso 3:** cuando el carril firmware termine, comparar con el código (`git grep` de cada
  ruta y comando) y corregir lo que no coincida. Si no hay código todavía, marcar "pendiente de
  confirmar con el código" en lo que dependa de él.
- [ ] **Paso 4:** commit `docs: fase 1 (API, seguridad, operación, pruebas, arquitectura)`.

---

## Orden y paralelismo

- Firmware 1 → 10, un agente, en orden.
- Site 11 → 12, CI 13 → 14 y docs 15 → 16 en paralelo con el firmware (no comparten archivos).
- Docs 16 paso 3 y CI 13 paso 3 se cierran después de la tarea 10.
- Cierre de la fase (fuera de este plan, lo hace el orquestador): `security-suite` completo con
  todo el historial de git (1.9 paso 1–2) y revisión de todo el branch. El cambio de visibilidad
  del repo lo hace el autor.

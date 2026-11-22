# SiBlock fase 5 ("Extras"): plan de implementación

> **Para agentes:** SUB-SKILL OBLIGATORIA: usa superpowers:subagent-driven-development (recomendado) o
> superpowers:executing-plans para hacer este plan tarea por tarea. Los pasos usan casillas (`- [ ]`).

**Meta:** que SiBlock sirva para el día a día de una familia: nombres a los aparatos, pausa por
aparato, horarios por aparato y grupo de sitios, listas opcionales (estafas y virus, anuncios en
español), LED de estado, "cuánto te ahorra", respaldo y restauración de la configuración; y en la
página, versión en inglés, explicación del LED con un dibujo y botón opcional de apoyo.

**Arquitectura:** la lógica sin Arduino va a `lib/siblock_core/` en archivos nuevos
(`family`, `backup`, `lists`) y en `modes`/`usage` (LED y ahorro), probada en `test_host`. En la
placa, `src/family_store.{h,cpp}` une esa lógica con la tabla ARP (lwIP), LittleFS
(`/familia.txt`) y la tarea DNS; `web.cpp` suma rutas y `web_page.h` sus secciones. El
simulador finge la tabla ARP con IPs `127.0.0.x`. `site/` suma `site/en/` y un módulo de textos
por idioma; `.github/scripts/make_site_files.py` escribe el enlace de apoyo.

**Tecnologías:** ESP32 clásico (WROOM-32, sin PSRAM, 4 MB), Arduino core 2.0.17, PlatformIO,
C++17, lwIP (`etharp_find_addr`, `tcpip_api_call`); HTML/CSS/JS sin framework con
`node --test` (Node 22); Python 3 (simulador y scripts de CI).

**Diseño:** [`docs/superpowers/specs/2026-11-21-siblock-design.md`](../specs/2026-11-21-siblock-design.md),
sección "Fase 5 en detalle" (5.1 a 5.3). Fuente: [`docs/ideas-pendientes.md`](../../ideas-pendientes.md)
§3.1 (1 a 5) y §3.5 (3, 4, 5, 6 y 9).

## Restricciones globales

- Rama `siblock-fases`. Prohibido: push, merge a `main`, cambiar la visibilidad del repo,
  `wrangler deploy` / `wrangler pages deploy` reales, tocar otros equipos de la red, mandar
  correos, leer o copiar archivos personales con
  tokens o claves, poner correos personales (solo
  `contacto@s-dev.win`). No leer ni mostrar `src/secrets.h`.
- No cambiar `partitions.csv`. No cambiar `MAX_DOMAINS` (180000). Flash del programa < 92 % en
  `pio run`; RAM estática (la que reporta `pio run`) no más de 8 KB por encima de la de antes de
  la fase (anotar el número de partida en la Tarea 1).
- Deben pasar siempre: `make -C test_host test`; `make -C sim`, luego
  `cd sim && SIM_FLASH=./sim-flash ./esp32-sim` en segundo plano y `python3 sim/test_sim.py`
  ("TODO BIEN"; si reaparece la FALLA vieja de la sección 8 de carga, no debe empeorar ni sumarse
  otra); apagar con `pkill -x esp32-sim` (nunca `pkill -f`); `pio run`; `cd site && npm test`;
  `python3 .github/scripts/test_make_site_files.py`.
- Texto que ve la persona: español, palabras simples, sin tecnicismos. Marca **SiBlock**. El panel
  de la placa sigue solo en español.
- Privacidad: sin cookies; **no se guardan IPs ni MACs completas** (ni en la placa fuera de RAM ni
  en archivos ni en el respaldo): un aparato se guarda por su huella con sal. Nada nuevo manda
  datos a internet; las listas nuevas solo se bajan y vienen apagadas de fábrica.
- Seguridad: toda ruta nueva del panel va por `guarded<true, …>` (sesión + header anti-CSRF
  `X-Requested-With: adblock`), pensada contra un atacante en la red de la casa. Cuerpos de POST
  con el tope de 512 B (`kMaxBody`), salvo los pedazos del respaldo (3072 B, Tarea 8). Todo lo
  que viene de la red se pinta con `textContent`, nunca `innerHTML`.
- No inventar datos: URLs y tamaños de listas, dominios de los grupos y el "30 KB por anuncio"
  llevan el comentario `// falta verificar` (en la página, el texto "aproximado").
- Commit al final de cada tarea solo si el orquestador lo pide (pie
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`); nunca push.

## Contratos entre carriles

| Contrato | Valor |
|---|---|
| `site/js/config.js` (repo y CI, byte a byte salvo los valores) | `export const CONFIG = Object.freeze({ api: '/api/v1', analiticaToken: '', turnstileClave: '', apoyoUrl: '' });` |
| Comentarios de `config.js` | Cada lado escribe los suyos (el repo y el script ya difieren hoy); los dos mencionan "enlace de apoyo". Solo la línea `export` es contrato |
| Enlace de apoyo válido | `^https://[A-Za-z0-9.-]{1,100}(/[A-Za-z0-9._~/-]{0,150})?$` o vacío |
| Variable del repo | `SIBLOCK_APOYO_URL` → `make_site_files.py --apoyo-url` |
| Páginas en inglés | `site/en/index.html`, `site/en/privacy.html` (`<html lang="en">`) |
| Nombre de la luz en la guía | ancla `#luz` en `site/index.html` y `site/en/index.html` (el panel enlaza a `https://siblock.s-dev.win/#luz`) |

## Enfoque de revisión (lo que más puede morder)

1. **Teléfono con MAC privada que cambia** (Android/iOS "dirección privada" que rota): el aparato
   aparece como nuevo sin nombre. Esperado: no rompe nada; el panel deja nombrarlo otra vez y
   el viejo se puede "Olvidar". Prueba: Tarea 5 (la misma IP con otra MAC en `$SIM_FLASH/.arp`
   → aparato nuevo sin nombre, ninguno bloqueado de más).
2. **Respaldo cortado, editado a mano o de otra placa con más reglas**: esperado: no se aplica
   nada y el panel dice qué línea falla. Prueba: Tarea 2 (sin `fin`, línea de 301 bytes, horario
   con aparato que no existe, 201 reglas, NUL en un nombre) y Tarea 8 (subir uno cortado deja todo
   igual).
3. **Sin hora de internet o reloj que salta**: un horario nunca bloquea sin hora creíble.
   Prueba: Tarea 1 (`localMinuteOfWeek` con epoch < 1700000000 → sin hora) y Tarea 4.
4. **Apagar una lista opcional deja la lista nueva más chica que la mitad**: la protección contra
   listas cortadas no debe rechazarla. Prueba: Tarea 3 (`acceptListSize` con `allowShrink`).
5. **Nombre con emoji, acentos o 32 bytes justos, o un nombre `<img onerror>`**: se guarda tal
   cual si es UTF-8 válido de 1 a 32 bytes y se pinta como texto. Prueba: Tarea 1
   (`validDeviceName`) y Tarea 5 (el JSON lo escapa y el panel usa `textContent`).

---

## Carril firmware (`src/`, `lib/`, `sim/`, `test_host/`, `platformio.ini`)

Un solo agente, tareas en orden (1 → 9). Skills: `esp32`, `superpowers:test-driven-development`;
al tocar rutas del panel, `security-suite` (auditor del área).

### Tarea 1: `siblock_core/family` — huella, nombres, horarios y grupos

**Archivos:**
- Crear: `lib/siblock_core/family.h`, `lib/siblock_core/family.cpp`
- Modificar: `test_host/test_siblock.cpp` (incluir `family.h`, `testFamily()`)

**Interfaces (namespace `siblock`):**
- Produce:
  - `constexpr size_t kMaxDevices = 32, kMaxSchedules = 8, kDeviceNameMax = 32;`
  - `constexpr uint8_t kGroupSocial = 1, kGroupGames = 2, kGroupAll = 4;`
  - `struct DeviceId { uint8_t b[8]; };` con `==`; `std::string deviceIdHex(const DeviceId&)`
    (16 hex minúsculas); `bool parseDeviceId(const char* s, size_t n, DeviceId& out)` (exactamente
    16 hex, mayúsculas o minúsculas).
  - `DeviceId deviceIdFor(const uint8_t salt[16], const uint8_t mac[6])` = primeros 8 bytes de
    `sha256(salt ‖ mac)` (usa `sha256` de `siblock_crypto.h`).
  - `bool validDeviceName(const std::string& s)`: 1..32 bytes, UTF-8 válido (usa la misma regla
    que `jsonEscape` acepta), sin bytes < 0x20 ni 0x7F, sin espacio al inicio ni al final.
  - `struct Schedule { uint32_t devices; uint8_t groups, days; uint16_t start, end; };`
    (`devices` = bits de casillas 0..31; `days` bit0 = lunes … bit6 = domingo; minutos 0..1439).
  - `bool validSchedule(const Schedule&)`: `groups` en 1..7, `days` en 1..127, `devices != 0`,
    `start`,`end` ≤ 1439, `start != end`.
  - `int32_t localMinuteOfWeek(int64_t epochSeconds, int16_t tzMinutes)`: −1 si
    `epochSeconds < 1700000000`; si no, `(lunes=0)*1440 + minuto del día` en la hora local
    (`epoch + tz*60`; el 1 de enero de 1970 fue jueves → `dow = (días + 3) % 7`).
  - `bool scheduleActive(const Schedule&, int32_t minuteOfWeek)`: falso con −1; si
    `start < end`: día con bit y minuto en `[start, end)`; si `end < start`: (día con bit y
    minuto ≥ start) o (día anterior con bit y minuto < end).
  - `bool inGroups(const std::string& name, uint8_t groups)`: `kGroupAll` → true; si no, el
    nombre o un padre (`adblock::forEachSuffix`) está en el catálogo del grupo.
  - `bool validTzMinutes(int32_t)`: −720..840.
  - `class Family` (sin lock; el firmware lo protege): `bool setName(const DeviceId&, const
    std::string&)` (crea en la primera casilla libre o renombra; falso si no válido o lleno),
    `bool forget(const DeviceId&)` (libera la casilla y quita su bit de todos los horarios; un
    horario que queda sin aparatos se borra), `int slotOf(const DeviceId&) const` (−1),
    `const char* nameAt(int slot) const` (nullptr si libre), `bool used(int) const`,
    `DeviceId idAt(int) const`, `bool setSchedule(int index, const Schedule&)` (index −1 = nuevo;
    falso si inválido, lleno, o usa casillas libres), `bool removeSchedule(int)`,
    `size_t scheduleCount() const`, `const Schedule& scheduleAt(size_t) const`,
    `bool blockedBySchedule(int slot, const std::string& name, int32_t minuteOfWeek) const`,
    `int16_t tz`, `uint8_t salt[16]`, `bool hasSalt`, `void clear()`.
- Catálogo (en `family.cpp`, comentario `// lista corta inicial, falta verificar que cubra las apps`):
  redes = `facebook.com fbcdn.net instagram.com cdninstagram.com tiktok.com tiktokcdn.com
  tiktokv.com snapchat.com x.com twitter.com twimg.com threads.net pinterest.com reddit.com
  discord.com discord.gg`; juegos = `roblox.com rbxcdn.com epicgames.com fortnite.com
  minecraft.net xboxlive.com playstation.net steampowered.com steamcommunity.com supercell.com`.
  Arreglos ordenados y búsqueda binaria (sin `std::regex`, sin memoria dinámica).

- [ ] **Paso 1: anotar el punto de partida.** Correr `pio run` y anotar RAM y flash en el
  informe de la tarea (son la base de la regla "no más de 8 KB de RAM").
- [ ] **Paso 2: escribir `testFamily()` (falla al compilar).** Casos con estos valores:
  - `deviceIdHex(deviceIdFor(sal 00..0f, mac 02:00:00:00:00:05)) == "9d2cbeaf31115773"`.
  - `parseDeviceId("9D2CBEAF31115773")` ok; 15 o 17 caracteres, o `"9d2cbeaf3111577g"` → falso.
  - `validDeviceName`: `"Tele sala"`, `"Cel de mamá"`, `"📺"`, 32 bytes justos → sí; `""`, 33
    bytes, `" a"`, `"a "`, `"a\tb"`, `"a\x7f"`, `"\xc3"` (UTF-8 cortado), `std::string("a\0b",3)` → no.
  - `localMinuteOfWeek(1795219200, 0) == 5*1440` (sábado 2026-11-21 00:00 UTC);
    `localMinuteOfWeek(1795419000, 0) == 0*1440 + 450` (lunes 07:30);
    `localMinuteOfWeek(1795419000, -360) == 0*1440 + 90` (lunes 01:30 con UTC−6);
    `localMinuteOfWeek(1795219200, -360) == 4*1440 + 1080` (viernes 18:00);
    `localMinuteOfWeek(1699999999, 0) == -1`.
  - `scheduleActive({1, 1, 0b0011111 /*lun-vie*/, 480, 840}, 0*1440+480)` sí; `…+840` no;
    sábado 600 no. Medianoche: `{1,4,0b0000001 /*lunes*/, 1260, 420}` → lunes 22:00 sí, martes
    06:59 (`1*1440+419`) sí, martes 07:00 no, lunes 06:00 no (el domingo no tiene bit).
  - `inGroups("www.instagram.com", kGroupSocial)` sí; `"instagram.com.evil.net"` no;
    `"roblox.com", kGroupSocial` no; `"wikipedia.org", kGroupAll` sí.
  - `Family`: 32 nombres caben, el 33 no; renombrar no gasta casilla; `forget` quita el bit y
    borra el horario que queda vacío; `setSchedule` con una casilla libre → falso; 9.º horario
    → falso; `blockedBySchedule` combina casilla, grupo y hora.
- [ ] **Paso 3:** `make -C test_host test` → falla de compilación (falta `family.h`).
- [ ] **Paso 4:** implementar `family.h/.cpp` con las firmas de arriba.
- [ ] **Paso 5:** `make -C test_host test` → `OK: N pruebas, 0 fallas`.
- [ ] **Paso 6: commit** (si el orquestador lo pide): `git add lib/siblock_core/family.* test_host/test_siblock.cpp`.

### Tarea 2: `siblock_core/backup` — formato del respaldo y de `/familia.txt`

**Archivos:**
- Crear: `lib/siblock_core/backup.h`, `lib/siblock_core/backup.cpp`
- Modificar: `test_host/test_siblock.cpp` (`testBackup()`)

**Formato** (una entrada por línea, `\n`; se acepta `\r\n`; máximo 300 bytes por línea; nada de
líneas vacías ni palabras desconocidas):

```
SIBLOCK-RESPALDO 1          (o SIBLOCK-FAMILIA 1 en /familia.txt)
listas <1, 3, 5 o 7: impar = incluye la general>   (solo respaldo)
relay <0|1>                   (solo respaldo)
zona <-720..840>
sal <32 hex>
bloquear <dominio>            (solo respaldo; adblock::normalizeDomain)
permitir <dominio>            (solo respaldo)
aparato <casilla 0..31> <huella 16 hex> <nombre: el resto de la línea>
horario <aparatos 8 hex> <grupos 1..7> <días 1..127> <inicio 0..1439> <fin 0..1439>
fin
```

**Interfaces (namespace `siblock`):**
- Consume: `Family`, `Schedule`, `validDeviceName`, `validSchedule`, `parseDeviceId` (Tarea 1);
  `adblock::normalizeDomain`.
- Produce:
  - `struct Backup { bool full; uint8_t lists; bool relay; std::vector<std::string> block, allow; Family family; };`
  - `constexpr size_t kBackupMaxBytes = 24576, kBackupMaxLine = 300, kBackupMaxRules = 200;`
  - `class BackupReader { public: explicit BackupReader(bool full); bool line(const char* p, size_t n); bool finish(); const Backup& result() const; int errorLine() const; const char* error() const; };`
    `line` devuelve falso en el primer error y desde ahí todo es falso; `finish` exige haber
    visto cabecera, `sal` y `fin` (y en el respaldo, `listas`), y que cada horario apunte a
    casillas usadas. Mensajes de error en español simple (p. ej. `"el archivo está cortado"`,
    `"línea 12: no se entiende"`, `"demasiadas reglas"`).
  - `std::string writeFamily(const Family&)` y
    `std::string writeBackup(uint8_t lists, bool relay, const std::vector<std::string>& block, const std::vector<std::string>& allow, const Family&)`:
    el orden de líneas es el de la tabla de arriba; los aparatos por casilla; `aparatos` en
    `%08x`.

- [ ] **Paso 1: escribir `testBackup()`** con: ida y vuelta (`writeBackup` → `BackupReader` →
  mismo contenido, incluido un nombre `"Cel de mamá 📱"`); `\r\n` aceptado; errores con su
  `errorLine()`: sin `fin`, cabecera `SIBLOCK-RESPALDO 2`, línea de 301 bytes, `listas 2` (sin
  bit 1), `zona 900`, `sal` de 31 hex, `aparato 32 …`, dos `aparato` en la misma casilla,
  horario con casilla no usada, `inicio == fin`, 201 reglas, `bloquear no_es_dominio`, nombre con
  `\0` o `\x01`, palabra `listas` en `/familia.txt` (lector con `full=false`), línea vacía.
- [ ] **Paso 2:** `make -C test_host test` → falla de compilación.
- [ ] **Paso 3:** implementar `backup.h/.cpp`.
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas.
- [ ] **Paso 5: commit** (si se pide).

### Tarea 3: `siblock_core/lists`, LED y ahorro

**Archivos:**
- Crear: `lib/siblock_core/lists.h`, `lib/siblock_core/lists.cpp`
- Modificar: `lib/siblock_core/modes.{h,cpp}` (LED), `lib/siblock_core/usage.{h,cpp}` (ahorro),
  `src/config.h` (quitar `BLOCKLIST_URLS` y dejar un comentario que apunta a `lists.cpp`),
  `test_host/test_siblock.cpp` (`testLists()`, `testStatusLed()`, `testSavings()`)

**Interfaces (namespace `siblock`):**
- Produce:
  - `constexpr uint8_t kListGeneral = 1, kListScam = 2, kListSpanish = 4, kListsDefault = kListGeneral;`
  - `bool validListMask(uint32_t)`: impar (incluye `kListGeneral`) y ≤ 7.
  - `size_t listUrls(uint8_t mask, const char** out, size_t max)`: orden **estafas, general,
    español**; general = las dos de hoy (StevenBlack y HaGeZi Light, mismas URLs que
    `config.h`); máscara inválida → solo general.
  - URLs nuevas, cada una con `// falta verificar (URL, tamaño, licencia)`:
    estafas = `https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/tif.mini-onlydomains.txt`;
    español = `https://easylist-downloads.adblockplus.org/easylistspanish.txt` (solo cuentan sus
    líneas `||dominio^`; `parseListLine` ya descarta las que traen `$` u otra cosa).
    Al implementar, comprobar solo leyendo (`curl -sL <url> | wc -l`, y para la española
    `grep -c '^||[^$/]*\^$'`) y anotar el número en el comentario; si una URL no responde, dejar
    la opción con `const char* nullptr` (no se ofrece en el panel) y decirlo en el informe.
  - `enum class LedState { kOk, kUpdating, kProblem };`
    `LedState ledStateFor(bool staConnected, bool portalOpen, bool updating, uint32_t listCount, bool upstreamDown)`:
    `kUpdating` si `updating`; si no, `kProblem` si `!staConnected || portalOpen || listCount == 0 || upstreamDown`; si no, `kOk`.
    `bool ledOn(LedState, uint32_t nowMs)`: kOk → siempre; kUpdating → `(nowMs / 1000) % 2 == 0`;
    kProblem → `(nowMs / 200) % 2 == 0`.
  - `bool acceptListSize(uint32_t total, uint32_t current, bool sizeUnknown, uint32_t rejectedTotal, bool allowShrink)`:
    la regla que hoy está escrita dentro de `BlockList::buildFromBuckets` (aceptar salvo
    `current > 1000 && total < mínimo && !igualALaRechazada`, con mínimo = `current/2`, o
    `current − current/10` si `sizeUnknown`; "igual a la rechazada" = dentro de ±1 %), movida
    aquí sin cambiarla, más: `allowShrink` → siempre acepta.
  - `constexpr uint32_t kSavedBytesPerBlocked = 30 * 1024; // estimado, falta verificar con fuente`
    `uint64_t savedBytes(const UsageHistory::Slot* slots, size_t n)` = suma de `blocked` × 30 KB.

- [ ] **Paso 1: pruebas:** `listUrls(1)` = 2 URLs (las de hoy, en el mismo orden);
  `listUrls(7)` = estafas primero, luego las 2 generales, luego español; `listUrls(2)` (sin bit 1)
  = solo general; `validListMask(0/2/8)` falso, `1/3/5/7` cierto. LED: tabla de verdad de
  `ledStateFor` (5 casos) y `ledOn` en 0, 199, 200, 999, 1000 ms. Ahorro: 168 casillas con
  `blocked = 10` → `168*10*30720`; casillas vacías → 0. `acceptListSize`: (150000, 160000, …) sí;
  (70000, 160000, false, 0, false) no; lo mismo con `allowShrink=true` sí; (145000, 160000, true,
  0, false) no (90 %); (70000, 160000, false, 70500, false) sí (igual a la rechazada); current
  ≤ 1000 → siempre sí.
- [ ] **Paso 2:** `make -C test_host test` → falla.
- [ ] **Paso 3:** implementar; mover las URLs generales de `config.h` a `lists.cpp` sin
  cambiarlas; `blocklist.cpp` llama a `acceptListSize` (con `allowShrink=false` por ahora) en
  lugar de su cuenta propia. `main.cpp` sigue compilando (usa `listUrls(kListGeneral, …)` hasta la Tarea 6).
- [ ] **Paso 4:** `make -C test_host test` → 0 fallas; `pio run` compila.
- [ ] **Paso 5: commit** (si se pide).

### Tarea 4: `src/family_store` — aparatos en la placa y decisión en el DNS (+ simulador)

**Archivos:**
- Crear: `src/family_store.h`, `src/family_store.cpp`, `sim/shim/lwip/etharp.h` (si hace falta
  para compilar) o una función del shim
- Modificar: `src/blocklist.h` (`Verdict::kBlockedSchedule`, `Verdict::kPausedDevice`;
  `isBlockedVerdict` incluye `kBlockedSchedule`), `src/dns_server.cpp` (`answerLocally`),
  `src/web.cpp` (`verdictName`: `"horario"` y `"pausa-aparato"`), `src/main.cpp` (arranque),
  `sim/shim/shim.cpp`, `sim/test_sim.py` (sección 19)

**Interfaces (namespace `family`):**
- Consume: `siblock::Family`, `BackupReader(false)`, `writeFamily`, `localMinuteOfWeek`.
- Produce:
  - `void begin();` lee `/familia.txt` (si no existe o no se puede leer: tabla vacía y sal nueva
    con `esp_random`, que se guarda al primer cambio).
  - `bool idForIp(uint32_t ip, siblock::DeviceId& out);` caché en RAM de 16 entradas
    `{ip, id, ok, checkedMs}`, válida 5 min; si falla, pregunta a la tabla ARP con
    `bool arpLookup(uint32_t ip, uint8_t mac[6])` (en la placa: `tcpip_api_call` +
    `etharp_find_addr(netif_default, …)`; en el simulador: IP `127.0.0.x` → MAC
    `02:00:00:00:00:<x>`, salvo que el archivo `$SIM_FLASH/.arp` (se relee en cada consulta,
    como `.clock_ms`) tenga una línea `127.0.0.x 02:aa:bb:cc:dd:ee` (otra MAC) o
    `127.0.0.x -` (no está en la tabla)). Si `isLocalClient` del simulador no acepta
    `127.0.0.x`, ajustar solo el shim (máscara 255.0.0.0 para la IP 127.0.0.1).
  - `enum class Decision { kNone, kBlockSchedule, kPaused };`
    `Decision decide(uint32_t ip, const std::string& name);` — devuelve `kNone` de inmediato
    (sin ARP ni lock) si no hay horarios ni pausas (bandera atómica); si no, horario primero
    (`blockedBySchedule` con `localMinuteOfWeek(time(nullptr), tz)`), luego pausa del aparato.
  - `bool setName(const DeviceId&, const String&)`, `bool forget(const DeviceId&)`,
    `bool pause(const DeviceId&, uint32_t minutes)` (0 = quitar; solo casillas con nombre),
    `uint32_t pauseLeft(int slot)`, `bool setSchedule(int, const siblock::Schedule&, int16_t tz)`,
    `bool removeSchedule(int)`, `void snapshot(siblock::Family& out)` (copia bajo lock),
    `bool replace(const siblock::Family&)` (para restaurar), `void erase()` (borra
    `/familia.txt` y deja la tabla vacía con sal nueva).
  - Guardado atómico: escribir `/familia.new` y renombrar sobre `/familia.txt` (como
    `/custom.txt`). Pausas solo en RAM (`pauseUntilMs[32]`, restas sin signo).
- `answerLocally`: después de los nombres especiales (paso 1) y antes de la lista (paso 3):
  `kBlockSchedule` → `v = Verdict::kBlockedSchedule`, respuesta `buildBlockedResponse`, cuenta
  como bloqueada (historia y `stats_.blocked`; no suma al top); `kPaused` → si la lista diría
  bloqueado, `v = Verdict::kPausedDevice` y se reenvía. Los horarios valen aunque la casa esté en
  pausa y ganan sobre "Permitir".

- [ ] **Paso 1: sección 19 en `test_sim.py` (falla).** Placa propia (`Fresh`, flash nueva, con
  contraseña creada) y consultas DNS con `source="127.0.0.2"` y `"127.0.0.3"`. Como aún no hay
  rutas (Tarea 5), la sección prepara la carpeta antes de arrancar (`tempfile.mkdtemp`, escribir
  `nvs.json` y `familia.txt`, copiar `list.bin`, luego `Fresh(dir=…)` y borrarla al final): sal `00…0f`, `aparato 0 <huella de 02:00:00:00:00:02> Tele`, horario
  `00000001 4 127 <minuto UTC actual − 5> <minuto UTC actual + 5>` con `zona 0`. Comprobar:
  `example.com` desde .2 → `0.0.0.0`; desde .3 → IP real; con `.arp` = `127.0.0.2 -` → IP real
  (falla abierto); con `/familia.txt` roto (sin `fin`) la placa arranca y no bloquea a nadie.
  Usar el valor de la huella calculado en Python con `hashlib.sha256`.
- [ ] **Paso 2:** `make -C sim` y correr solo la sección (agregarla a `nuevas()`) → FALLA.
- [ ] **Paso 3:** implementar `family_store`, el hook en `answerLocally`, `verdictName` y el shim.
- [ ] **Paso 4:** `make -C test_host test`, `make -C sim`, `python3 sim/test_sim.py --solo-nuevas`
  → la sección 19 pasa; `pio run` compila (si `etharp_find_addr` o `tcpip_api_call` no existen
  con ese nombre en el core 2.0.17, buscar el equivalente en
  `~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/include/lwip/` y anotarlo).
- [ ] **Paso 5: commit** (si se pide).

### Tarea 5: rutas y panel de aparatos y horarios

**Archivos:**
- Modificar: `src/web.cpp`, `src/web_page.h`, `sim/test_sim.py` (sección 20)

**Rutas** (todas `guarded<true, …>`; JSON con `appendJsonString` para los nombres):
- `GET /api/devices` → `{"devices":[{"s":<casilla o -1>,"h":"<16 hex o vacío>","n":"<nombre o vacío>","ip":"<IP de ahora o vacío>","q":N,"b":N,"ago":s,"pausa":s}],"tz":<min>,"hora":<true si hay hora creíble>}`:
  los aparatos con nombre (aunque no se vean ahora) y los de `topClients` (hasta 10) con su
  huella si la tabla ARP la da. Una casilla con nombre y una IP de ahora se juntan en una fila.
- `POST /api/devices` `action=name&h=<hex>&name=<texto>` | `action=forget&h=` |
  `action=pause&h=&minutes=<0..1440>`. `name` solo acepta huellas que ya están en la tabla o en
  la caché de IPs de los últimos 5 min (no se pueden inventar aparatos). Errores 400 en español.
- `GET /api/schedules` → `{"items":[{"i":0,"dev":"<8 hex>","g":N,"d":N,"ini":N,"fin":N}],"tz":N}`.
- `POST /api/schedules` `action=save&i=<-1..7>&dev=<8 hex>&g=&d=&ini=&fin=&tz=` |
  `action=delete&i=`. `tz` se guarda en cada `save`.

**Panel** (`web_page.h`, sección "Aparatos" debajo de "Bloqueo" y sección "Horarios"):
- Cada aparato: nombre (o "Aparato sin nombre" + su IP), consultas y bloqueadas, botones
  "Ponerle nombre", "Pausar 1 h" / "Reanudar", "Olvidar" (pide confirmar). "Este aparato" marca
  el que usa el panel. Sin huella: "Todavía no lo reconozco; vuelve a intentar en un minuto".
- Horarios: elegir aparatos (casillas), grupos ("Redes sociales", "Juegos", "Todo internet"),
  días (L M M J V S D) y horas (`<input type="time">`). Al guardar manda
  `tz = -new Date().getTimezoneOffset()`. Si `tz` guardado ≠ el del navegador:
  "La hora de tu casa cambió. Guarda otra vez un horario para corregirla". Si `hora` es falso:
  "Los horarios empiezan cuando SiBlock tenga la hora de internet".
- La lista de consultas recientes muestra el nombre del aparato cuando lo hay.

- [ ] **Paso 1: sección 20 en `test_sim.py` (falla):** sin sesión → 401 en las 4 rutas; sin
  `X-Requested-With` → 403; nombrar el aparato `127.0.0.2` (huella tomada de `GET /api/devices`
  después de una consulta desde .2) con `"<b>Tele</b> 📺"` y leerlo igual (JSON escapado);
  nombre de 33 bytes → 400; `h` inventada → 400; pausar .2 con un dominio bloqueado de la lista →
  pasa desde .2 y sigue bloqueado desde .3; `minutes=0` lo reanuda; horario "Todo internet" para
  .2 en la hora actual → `example.com` bloqueado desde .2; `delete` → ya no; `forget` borra
  también su horario; reiniciar la placa (misma carpeta) conserva nombre y horario y pierde la
  pausa; MAC que rota: escribir en `.arp` otra MAC para `127.0.0.2`, adelantar el reloj 6 min
  (`set_clock`) y consultar desde .2 → `GET /api/devices` muestra un aparato nuevo sin nombre y
  el horario de "Tele" ya no bloquea a esa IP.
- [ ] **Paso 2:** correr → FALLA.
- [ ] **Paso 3:** implementar rutas y panel.
- [ ] **Paso 4:** sim completo "TODO BIEN"; `pio run` (anotar flash).
- [ ] **Paso 5:** revisión con `security-suite` (auditor de endpoints) sobre las rutas nuevas;
  arreglar lo CRÍTICO/ALTO antes de seguir.
- [ ] **Paso 6: commit** (si se pide).

### Tarea 6: listas elegibles (estafas y español)

**Archivos:**
- Modificar: `src/config.h` (`NVS_LISTS "lists"`, `NVS_LISTS_BUILT "lists_built"`),
  `src/settings.{h,cpp}` (`uint8_t lists()`, `bool setLists(uint8_t)`, `uint8_t listsBuilt()`,
  `bool setListsBuilt(uint8_t)`; valor inválido o faltante → `kListsDefault`),
  `src/blocklist.{h,cpp}` (`bool update(const char* const* urls, size_t n, bool allowShrink)`:
  con `allowShrink` no se aplica el mínimo de la mitad / 90 %), `src/main.cpp` (usa
  `listUrls(settings::lists(), …)`, `allowShrink = lists() != listsBuilt()`, y tras una
  actualización buena `setListsBuilt(lists())`), `src/web.cpp` (`/api/options` GET suma
  `"lists":N,"listsOffer":N,"skipped":N`; POST acepta `lists=<1..7>` además de `relayBlock`,
  guarda y pide una actualización), `src/web_page.h` (dos casillas en "Bloqueo":
  "Cuidar de estafas y virus (recomendado para abuelos y niños)" y "Quitar también anuncios de
  sitios en español"; si `skipped > 0`: "No cupieron N dominios de anuncios; las estafas sí
  están todas"), `sim/test_sim.py` (sección 21)

- [ ] **Paso 1: sección 21 (falla):** `Fresh` con `lists=7` en NVS y `SIM_HTTP_FAIL=1`; pedir
  "Actualizar lista" y leer en la salida las líneas `[lista] 1/4 …`, `2/4`, `3/4`, `4/4` en el
  orden estafas, general×2, español (con la descarga fallando se ve solo la 1/4: comprobar que
  es la de estafas). `POST /api/options lists=2` → 400; `lists=3` → 200 y queda en NVS;
  `GET /api/options` la devuelve. Con NVS `lists` = 9 → se comporta como 1.
- [ ] **Paso 2:** correr → FALLA.
- [ ] **Paso 3:** implementar (`update` pasa `allowShrink` a `acceptListSize`, ya probada en
  la Tarea 3).
- [ ] **Paso 4:** sim "TODO BIEN"; `pio run`.
- [ ] **Paso 5: commit** (si se pide).

### Tarea 7: LED de estado y "cuánto te ahorra"

**Archivos:**
- Modificar: `src/button.{h,cpp}` (`siblock::ButtonAction poll(uint32_t nowMs, siblock::LedState st)`:
  mientras BOOT está apretado manda `ledWhileHeld`; si no, `ledOn(st, nowMs)`; **escribe el pin
  solo cuando cambia**), `src/main.cpp` (calcula `ledStateFor(WiFi conectado, portal::active(),
  blocklist.updating(), blocklist.count(), dns.upstreamDown())`), `src/web.cpp`
  (`/api/history` suma `"ahorroMb":N` = `savedBytes(168 h) / 1048576`, entero), `src/web_page.h`
  (debajo de la gráfica: "Aproximado: en 7 días tu casa se ahorró unos N MB de anuncios"; si
  N = 0, no se muestra; liga "¿Qué dice la luz?" a `https://siblock.s-dev.win/#luz`),
  `sim/test_sim.py` (sección 22)

- [ ] **Paso 1: sección 22 (falla):** `Fresh` sin WiFi (`SIM_WIFI` apagado como en las secciones
  del portal) → en la salida aparecen `[sim] led 1` y `[sim] led 0` alternando en menos de 1 s
  (parpadeo rápido); con WiFi y lista → `[sim] led 1` y ningún `led 0` en 3 s; sin más de 20
  líneas `[sim] led` por segundo (se escribe solo al cambiar). `GET /api/history` trae
  `ahorroMb` entero ≥ 0.
- [ ] **Paso 2:** correr → FALLA.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** sim "TODO BIEN" (las pruebas viejas del botón BOOT siguen pasando).
- [ ] **Paso 5: commit** (si se pide).

### Tarea 8: respaldo y restauración

**Archivos:**
- Modificar: `src/web.cpp`, `src/web_page.h`, `src/blocklist.{h,cpp}`
  (`bool replaceRules(const std::vector<std::string>& block, const std::vector<std::string>& allow)`:
  todo o nada, mismo guardado atómico que `saveRules`), `src/main.cpp` (BOOT 10 s también llama
  `family::erase()`), `sim/test_sim.py` (sección 23)

**Rutas** (todas `guarded<true, …>`):
- `GET /api/backup` → `text/plain; charset=utf-8`, `Content-Disposition: attachment;
  filename="siblock-respaldo-AAAA-MM-DD.txt"` (fecha de la placa; sin hora: `siblock-respaldo.txt`),
  `Cache-Control: no-store`, cuerpo de `writeBackup(...)`.
- `POST /api/restore/part?n=<0..7>` cuerpo `text/plain` ≤ 3072 B: callback de cuerpo propia que,
  en `RAW_START`, revisa el header anti-CSRF y la sesión (si no, descarta todo) y escribe en
  `/restore.tmp` (`n=0` lo vacía; `n` debe ser el siguiente al último; total ≤ 24576 → si no,
  413 y borra el temporal).
- `POST /api/restore/check` → lee `/restore.tmp` por líneas con `BackupReader(true)` y contesta
  `{"ok":true,"reglas":N,"aparatos":N,"horarios":N,"listas":N}` o
  `{"ok":false,"error":"…","linea":N}`.
- `POST /api/restore/apply` `confirm=1` → vuelve a leer y validar; si `ESP.getMaxAllocHeap()` <
  40 KB → 503 "Intenta otra vez en un momento"; aplica reglas (`replaceRules`), familia
  (`family::replace`), `setLists`, `setRelayBlock` (y `gDns->setRelayBlock`); borra el temporal;
  limpia la caché DNS; si `lists` cambió, pide actualizar la lista.
- Panel, sección "Respaldo": "Bajar respaldo" (fetch con los headers → `Blob` →
  `<a download>`), "Subir respaldo" (`<input type="file" accept=".txt,text/plain">`, parte el
  texto en pedazos de ≤ 3072 B **en finales de línea**, `check`, muestra el resumen y pide
  "Sí, reemplazar" antes de `apply`). Texto: "El respaldo guarda tus reglas, los nombres de los
  aparatos, los horarios y las listas elegidas. No guarda el WiFi ni la contraseña."

- [ ] **Paso 1: sección 23 (falla):** placa A con 2 reglas, 1 aparato nombrado y 1 horario →
  `GET /api/backup` (sin sesión 401; con sesión: cabecera `SIBLOCK-RESPALDO 1`, termina en
  `fin\n`, no contiene la contraseña del WiFi de `SEED_WIFI`, ni `install_id`, ni ninguna IP
  `127.0.0.`); placa B nueva: subir en 2 pedazos, `check` da los números, `apply` → reglas,
  nombre y horario iguales; el mismo archivo sin `fin` → `check` y `apply` dan error y B no
  cambia; pedazo `n=2` sin `n=1` → 400; pedazo de 3073 B → 413; más de 24 KB → 413; pedazos sin
  sesión no escriben `/restore.tmp`. BOOT 10 s en B → `/familia.txt` ya no existe.
- [ ] **Paso 2:** correr → FALLA.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** sim "TODO BIEN".
- [ ] **Paso 5:** `security-suite` (auditor de subidas y endpoints) sobre `/api/restore/*` y
  `/api/backup`; arreglar lo CRÍTICO/ALTO.
- [ ] **Paso 6: commit** (si se pide).

### Tarea 9: números finales del carril

- [ ] **Paso 1:** `make -C test_host test` → `OK: N pruebas, 0 fallas` (anotar N).
- [ ] **Paso 2:** sim principal + `python3 sim/test_sim.py` → "TODO BIEN" (anotar ok/FALLA);
  `pkill -x esp32-sim`.
- [ ] **Paso 3:** `pio run` → RAM y flash; flash < 92 %; RAM ≤ base de la Tarea 1 + 8 KB.
- [ ] **Paso 4:** `git grep -nE '([0-9a-f]{2}:){5}[0-9a-f]{2}' -- src lib` no muestra MACs reales
  (solo las `02:00:…` de prueba en el shim y en `test_host`).
- [ ] **Paso 5:** informe con los números y lo que quedó "falta verificar".

---

## Carril site (`site/`)

Un agente, Tareas 10 → 12. Skills: `frontend-design`, `break-ui`, `web-design-guidelines`.

### Tarea 10: textos por idioma y botón de apoyo

**Archivos:**
- Crear: `site/js/textos.js`, `site/tests/textos.test.mjs`, `site/tests/apoyo.test.mjs`
- Modificar: `site/js/config.js` (línea del contrato, con `apoyoUrl: ''`), `site/js/instalar.js`
  (todo texto visible sale de `texto(clave)`), `site/js/inicio.js` (botón de apoyo),
  `site/index.html` (pie: `<a id="apoyo" hidden rel="noopener noreferrer">Invítame un café</a>`)

**Interfaces:**
- `textos.js`: `export const TEXTOS = { es: {...}, en: {...} }`;
  `export function idioma(doc = document)` → `'en'` si `<html lang>` empieza con `en`, si no `'es'`;
  `export function texto(clave, doc = document)` → el texto del idioma, o el español si falta.
- `inicio.js`: `export function enlaceApoyo(url)` → `url` si cumple la expresión del contrato
  y `new URL(url).protocol === 'https:'`, si no `''`; al cargar, si hay enlace, pone `href` y
  quita `hidden` a `#apoyo` (en inglés el texto es "Buy me a coffee").

- [ ] **Paso 1: pruebas (fallan):** `TEXTOS.es` y `TEXTOS.en` tienen las mismas claves y ningún
  valor vacío; `idioma` con `lang="en"`, `"en-US"`, `"es"`, sin `lang`; `enlaceApoyo('')`,
  `'http://x.com'`, `'https://x.com/a"b'`, `'javascript:alert(1)'` → `''`;
  `'https://ko-fi.com/siblock'` → igual. `instalar.js` importa `texto` y cada clave que usa
  (`texto('…')`, buscada con una expresión regular sobre el archivo) existe en `TEXTOS.es` y
  `TEXTOS.en`; `instalar.test.mjs` sigue pasando.
- [ ] **Paso 2:** `cd site && npm test` → falla.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** `npm test` → todo pasa.
- [ ] **Paso 5: commit** (si se pide).

### Tarea 11: página en inglés

**Archivos:**
- Crear: `site/en/index.html`, `site/en/privacy.html`
- Modificar: todas las `site/*.html` (en el pie: `<a href="en/" hreflang="en" lang="en">English</a>`
  y `<link rel="alternate" hreflang="en" href="https://siblock.s-dev.win/en/">` en las dos que
  tienen versión), `site/tests/paginas.test.mjs` (revisa también `site/en/*.html`)

- `en/index.html`: lo mismo que `index.html` (qué comprar, instalar, router, la luz) traducido
  en inglés simple; rutas relativas `../estilos.css`, `../js/instalar.js`; enlaces a las guías
  (`../guia-router.html` …) con la nota "This guide is only in Spanish for now". El aviso de
  navegador (Chrome/Edge) también en inglés.
- `en/privacy.html`: traducción de `privacidad.html`.

- [ ] **Paso 1: pruebas (fallan):** `paginas.test.mjs` también recorre `en/`: `lang="en"`,
  título, sin JS en línea, enlaces internos existen (resolviendo `../`), solo
  `contacto@s-dev.win`, externos con `rel="noopener"`; cada página `es` con versión `en`
  enlaza a ella y al revés (`hreflang`).
- [ ] **Paso 2:** `npm test` → falla.
- [ ] **Paso 3:** escribir las páginas.
- [ ] **Paso 4:** `npm test` → pasa; revisar a 375 px de ancho (sin scroll de lado) con
  `break-ui` o el navegador.
- [ ] **Paso 5: commit** (si se pide).

### Tarea 12: "¿Qué dice la luz?" con dibujo

**Archivos:**
- Modificar: `site/index.html` y `site/en/index.html` (sección `id="luz"`), `site/estilos.css`,
  `site/tests/paginas.test.mjs`

- Sección con un dibujo SVG propio en línea (la placa del dibujo que ya existe, con el LED azul
  junto al USB) y tres filas: **Luz fija**: todo bien. **Parpadea despacio**: está bajando la
  lista, espera unos minutos. **Parpadea rápido**: algo falla (sin WiFi, modo de configuración o
  sin internet); qué hacer: abrir el panel o la red `SiBlock-XXXX`. Nota: "Si la luz está apagada
  y la placa tiene corriente, desconéctala y vuelve a conectarla". Animación del parpadeo con
  CSS y `@media (prefers-reduced-motion: reduce)` que la quita.
- Mencionar en "Qué puede hacer" los extras nuevos: nombres y pausa por aparato, horarios para
  los niños, listas de estafas y en español, respaldo.

- [ ] **Paso 1: prueba (falla):** `paginas.test.mjs`: `#luz` existe en `index.html` y
  `en/index.html`, con un `<svg` que tiene `role="img"` y `aria-label`.
- [ ] **Paso 2:** implementar. **Paso 3:** `npm test` pasa. **Paso 4: commit** (si se pide).

---

## Carril ci (`.github/`)

### Tarea 13: enlace de apoyo en `config.js`

**Archivos:**
- Modificar: `.github/scripts/make_site_files.py`, `.github/scripts/test_make_site_files.py`,
  `.github/workflows/release.yml` (los dos pasos que llaman al script: `env`
  `SIBLOCK_APOYO_URL: ${{ vars.SIBLOCK_APOYO_URL }}` y `--apoyo-url "$SIBLOCK_APOYO_URL"`; además,
  `test -d public/en`)

**Interfaces:** `make_site_files.py --apoyo-url URL` (opcional, vacío por defecto);
`config_js(token, clave, apoyo)` escribe la línea del contrato; el enlace se valida con
`APOYO_RE = re.compile(r"(?:https://[A-Za-z0-9.-]{1,100}(?:/[A-Za-z0-9._~/-]{0,150})?)?")`
(`fullmatch`); inválido → sale con 2 sin escribir nada y sin imprimir el valor. Cabecera nueva del
contrato.

- [ ] **Paso 1: pruebas (fallan):** `config_js` del test con el tercer valor; `--apoyo-url
  https://ko-fi.com/siblock` queda en el archivo; sin la opción → `apoyoUrl: ''`; malos:
  `http://x.com`, `https://x.com/a'b`, `https://x.com/<s>`, `javascript:x`, `https://x.com/a b`,
  `https://` + 101 letras → código 2 y nada escrito.
- [ ] **Paso 2:** `python3 .github/scripts/test_make_site_files.py` → falla.
- [ ] **Paso 3:** implementar; actualizar el docstring.
- [ ] **Paso 4:** la prueba pasa; `python3 -c "import yaml,sys;yaml.safe_load(open('.github/workflows/release.yml'))"` sin error.
- [ ] **Paso 5: commit** (si se pide).

---

## Carril docs (`docs/`, `README.md`, `LICENSE`)

### Tarea 14: documentación de la fase 5

**Archivos:** `docs/api.md` (rutas de las Tareas 5, 6, 7 y 8 con ejemplos y errores),
`docs/operacion.md` (aparatos, pausa por aparato, horarios, listas, LED, ahorro, respaldo, "borrar
la configuración" también borra `/familia.txt`), `docs/seguridad.md` (huella con sal en vez de
MAC; amenazas: alguien de la casa con la contraseña ve los nombres; subida del respaldo con
sesión, tope y todo o nada; el respaldo no lleva secretos pero sí nombres: guardarlo con
cuidado), `docs/pruebas.md` (secciones 19 a 23 del simulador; lista para la placa: tabla ARP con
más de 10 aparatos, LED real, tamaños de las listas nuevas y RAM al bajarlas),
`docs/arquitectura.md` (`family`, `backup`, `lists`, `family_store`), `docs/lista.md` (listas
opcionales y orden de prioridad), `docs/ideas-pendientes.md`, `README.md` (funciones nuevas en una
línea cada una).

- `ideas-pendientes.md`: marcar ✅ (fase 5) §3.1 ideas 1 a 5 y §3.5 ideas 3, 4, 5, 6 y 9 con una
  línea de "Hecho:" y lo pendiente (OUI, MAC que rota, verificar URLs y tamaños de listas,
  30 KB por anuncio sin fuente, guías solo en español, gráfica por aparato de §3.3). La cajita 3D
  (§3.2-12) y el video (§3.5-2) ya tienen su nota "Pendiente (fuera de la fase 5)" (la puso quien
  planeó): no tocarla. Actualizar el párrafo de arriba ("La fase 5 cubrió…").

- [ ] **Paso 1:** escribir los cambios con lo que digan las tareas 1–13 ya hechas (si el
  carril corre antes, usar las interfaces de este plan y revisar al final).
- [ ] **Paso 2:** `git grep -n '@' -- docs README.md | grep -v contacto@s-dev.win` no muestra
  correos nuevos.
- [ ] **Paso 3: commit** (si se pide).

---

## Orden y dependencias

- firmware: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 (un agente, en orden).
- site: 10 → 11 → 12; independiente del firmware salvo el ancla `#luz` (contrato).
- ci: 13; su prueba no lee `site/`, así que no depende del carril site; el contrato de
  `config.js` los mantiene iguales.
- docs: 14, al final (lee lo que hicieron los otros carriles).

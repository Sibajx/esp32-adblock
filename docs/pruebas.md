# Pruebas

Hay tres niveles: la lógica pura en la PC, el firmware completo en el simulador y la placa real.
CI corre los dos primeros, las pruebas de la página (`site/`, `npm test`), *(fase 3)* las del
Worker (`cloud/`, `npm test` y `npm run tipos`) y las de `make_site_files.py`, y `pio run` en
cada push y PR (`.github/workflows/ci.yml`). Todo junto, antes de dar algo por terminado:

```bash
make -C test_host test
make -C sim
(cd sim && SIM_FLASH=./sim-flash ./esp32-sim) &                   # en segundo plano
python3 sim/test_sim.py; pkill -x esp32-sim                      # nunca pkill -f
pio run                                                          # flash < 92 %
cd site && npm test; cd ..
cd cloud && npm test && npm run tipos; cd ..
python3 .github/scripts/test_make_site_files.py
```

## 1. Lógica pura: `test_host/`

```bash
make -C test_host test       # compila con -Werror, AddressSanitizer y UBSan; corre test_core y test_siblock
```

### `test_core` (`lib/adblock_core`)

- normalizar y leer líneas de lista;
- hash e índice;
- reglas propias;
- el parser DNS con paquetes rotos y al azar;
- caché (TTL, quién sale cuando se llena, `clear`);
- anti-rebinding;
- escape de JSON;
- respuesta del portal cautivo (`buildAddressResponse`: A con la IP del AP, AAAA vacío);
- *(fase 2)* respuesta NXDOMAIN (`buildNxdomain`): mismo ID y pregunta, RCODE 3, RD copiado, sin
  respuestas y sin EDNS aunque la consulta lo traiga.

### `test_siblock` (`lib/siblock_core`)

- SHA-256 y PBKDF2-HMAC-SHA256 contra vectores conocidos (RFC 7914 y uno propio con 10 000
  iteraciones); autoprueba; `equal32` distingue un solo bit;
- base64 (incluida una contraseña con `:` y `€`) y rechazo de entradas rotas;
- validaciones: contraseña del panel de 12 a 64 **bytes**, WiFi (SSID de 1 a 32 B, contraseña
  vacía o de 8 a 63 B);
- contraseña del AP: 10 caracteres del alfabeto sin letras confusas, muestreo con rechazo;
  nombre `SiBlock-0C3F` a partir de la MAC;
- Improv: bytes exactos de paquetes, checksum malo, basura y líneas `STATS` entre paquetes,
  paquete cortado, SSID UTF-8 y contraseña vacía; la prueba de un WiFi nuevo (`evalTry`) no
  cuenta como conectada antes de 0,5 s ni si la red conectada es otra;
- botón BOOT (antirrebote de 50 ms, < 2 s / 3–8 s / ≥ 10 s), LED, ventana de 30 min y política
  del portal, también con el reloj a punto de dar la vuelta (`0xFFFFFFFF - 60000`);
- red propia de SiBlock (`pickApAddress`): 192.168.4.1, u otra /24 que no choque con la de la
  casa (casa en 192.168.4.x, /22, /16, /8).

*(Fase 2)*:

- `testDnsNames`: nombre de prueba, canario de Firefox y Private Relay (exactos, sin subdominios
  salvo el de prueba); nombres de DNS cifrado por sufijo de etiqueta (`notdns.google` y
  `cloudflare-dns.com.evil.net` no cuentan); `isProbeHost` con mayúsculas, puerto, `.evil.com`
  al final, punto final, `_` y etiquetas vacías;
- `testUsageHistory`: `epochHour` (hora anterior a `1700000000`, nov. 2023 → 0), suma por hora, huecos en cero, salto de
  168 h o más que limpia todo, hora vieja dentro de la semana que suma en su casilla, más vieja
  que se ignora, hora 0 que no cuenta, guardar y leer 1352 B con cabecera `SBH1` y rechazo de
  archivos cortos, con otra cabecera o con contenido incoherente (última hora enorme o anterior a
  2023, casillas con datos sin última hora, más bloqueadas que consultas);
- `testTopBlocked`: orden, un dominio "pesado" entre 200 únicos sigue primero (Space-Saving),
  nombres de 253 caracteres cortados a 63, `halve()`;
- `testBypassTable` y `testDiagnose`: flags que se suman, olvido a las 24 h, 17 aparatos (sale el
  más viejo), orden bad → warn → info → ok, cada código de la tabla de
  [api.md](api.md#get-apidiagnoseprobeokfail-fase-2), router como DNS (≥ 50 consultas y ≥ 80 %,
  puerta de enlace 0 → no) y DNS de arriba caído (hacen falta 3 timeouts seguidos; 1 o 2 no
  bastan) con el reloj dando la vuelta.

*(Fase 3)* `testCloud` (`lib/siblock_core/cloud.*`):

- `validInstallId` (32 hex minúsculas; no 31/33, mayúsculas, `g`, vacío, `\0` dentro) y
  `makeInstallId` (16 bytes 0x00..0x0f → `000102…0e0f`);
- `parseSemver` / `isNewer` (`v1.2.10` > `v1.2.9`; `dev` y basura nunca son "más nueva"; sin ceros
  a la izquierda ni partes de más de 9999) y `parseVersionJson` (con espacios y saltos, clave
  `version` dentro de un valor, número en vez de texto, archivo de 1025 B, archivo cortado);
- `validReportDomain` (`ads.ejemplo.com` sí; `ejemplo`, `-a.com`, `a..com`, mayúsculas, `_`, 254 B,
  etiqueta de 64 y punto final no);
- los tres cuerpos JSON exactos (`pingJson`, `modemReportJson`, `blockedReportJson`) y `""` con
  cualquier campo malo;
- bloqueadas de las últimas 24 h y 168 h;
- `CloudSchedule`: primero al minuto, luego 24 h + el azar, fallos a 1, 2 y 4 h, tope de 24 h y la
  vuelta de `millis()`.

*(Fase 5)*:

- `testFamily` (`family.*`): huella (`deviceIdFor` con sal `00..0f` y MAC `02:00:00:00:00:05` →
  `9d2cbeaf31115773`), `parseDeviceId` (15, 17 o una `g` → no), nombres válidos (`Tele sala`,
  `Cel de mamá`, `📺`, 32 bytes justos) y no válidos (vacío, 33 bytes, espacio en las puntas,
  tabulador, `0x7F`, UTF-8 cortado, `\0` dentro), `localMinuteOfWeek` (sábado 2026-11-21 00:00 UTC,
  lunes 07:30, la misma hora con UTC−6, sin hora creíble → −1), horarios de lunes a viernes y que
  cruzan la medianoche, grupos (`www.instagram.com` sí, `instagram.com.evil.net` no, todo internet),
  32 aparatos y el 33 no, renombrar sin gastar casilla, `forget` que borra el horario vacío, 9.º
  horario no, horario con una casilla libre no;
- `testBackup` (`backup.*`): ida y vuelta con un nombre `Cel de mamá 📱`, `\r\n`, y cada error con
  su línea: sin `fin`, cabecera de otra versión, línea de 301 bytes, `listas 2`, `zona 900`, sal
  corta, casilla 32, dos aparatos en la misma casilla, horario con un aparato que no existe,
  `inicio == fin`, 201 reglas, dominio malo, nombre con `\0` o `\x01`, `listas` en `/familia.txt`,
  línea vacía;
- `testLists` (`lists.*`): orden de descarga (estafas, español, general ×2), máscaras válidas
  (`1 3 5 7`) e inválidas (que bajan solo la general) y la regla para aceptar una lista nueva
  (`acceptListSize`: la mitad, el 90 % sin tamaño, igual a la rechazada, `allowShrink`);
- `testStatusLed` (`modes.*`): `ledStateFor` (5 casos) y `ledOn` en 0, 199, 200, 999 y 1000 ms;
- `testSavings` (`usage.*`): 168 horas con 10 bloqueadas → `168 × 10 × 30 720` bytes; vacías → 0.

`list_stats` es una herramienta aparte: cuenta dominios únicos de las listas reales y calcula
falsos positivos.

## 2. Firmware completo en Linux: `sim/`

El simulador compila **el mismo `src/`** contra una capa falsa en `sim/shim/`:

| Del ESP32 | En el simulador |
|---|---|
| WiFi | `127.0.0.1/8`; MAC fija `24:6f:28:ab:0c:3f` (AP `SiBlock-0C3F`). Conectado a `CasaDemo` salvo con `SIM_WIFI=down`; `begin("simulador-ok", …)` siempre conecta |
| AP y escaneo | `softAPIP()` = 127.0.0.1 (choca con la red de la casa si hay WiFi: sirve para probar ese caso); `softAPConfig` solo imprime `[sim] AP en <ip>`; dos redes fijas: `CasaDemo` (−50 dBm, cifrada) y `Café "Ñandú"` (−70 dBm, abierta) |
| NVS (`Preferences`) | `$SIM_FLASH/nvs.json` (`{"siblock": {"clave": "texto"}}`; bytes como `"hex:…"`) |
| LittleFS (2,44 MB) | carpeta `sim/sim-flash`, con el mismo tamaño y bloques de 4 KB |
| Botón BOOT (GPIO0) | apretado mientras exista `$SIM_FLASH/.boot` |
| LED (GPIO2) | imprime `[sim] led 0/1` cuando cambia (*fase 5*: también la luz de estado) |
| Tabla ARP *(fase 5)* | `127.0.0.x` → `02:00:00:00:00:<x>`, o lo que diga `$SIM_FLASH/.arp` |
| `millis()` | reloj real + los ms escritos en `$SIM_FLASH/.clock_ms` (para adelantar el tiempo) |
| Serie de entrada | stdin, solo con `SIM_SERIAL_STDIN=1` |
| Tareas, colas y semáforos de FreeRTOS | hilos de `std::thread` y primitivas de C++ |
| AsyncUDP / WiFiServer | sockets POSIX |
| HTTPS (`WiFiClientSecure`) | `curl`, verificando certificados. *(Fase 3)* Una URL que empieza con `https://siblock.s-dev.win` va a `SIM_CLOUD_URL`; **sin esa variable no sale** (se anota `[sim] nube … (sin SIM_CLOUD_URL: no sale)`). El simulador nunca llama al Worker de verdad |
| `WebServer` | shim con los mismos topes que `lib/WebServer` |

`make -C sim` arma dos binarios:

| Binario | Para qué |
|---|---|
| `sim/esp32-sim` | Con `sim/secrets.h` como semilla (WiFi `simulador`, contraseña `clave-de-prueba`). El de siempre |
| `sim/esp32-sim-fresh` | Sin `secrets.h`, como el firmware publicado. Las secciones 12–15 lo lanzan con su propia carpeta temporal |

Variables de entorno:

| Variable | Efecto |
|---|---|
| `SIM_FLASH` | Carpeta de la flash (lista, reglas, `nvs.json`, `.clock_ms`, `.boot`) |
| `SIM_DNS_PORT`, `SIM_WEB_PORT` | Puertos (5353 y 8080 por defecto) |
| `SIM_HTTP_FAIL` | Las descargas fallan (no baja la lista) |
| `SIM_NVS_FAIL_KEY` | Guardar esa clave en la NVS falla (como una NVS llena o dañada) |
| `SIM_SERIAL_STDIN=1` | La serie lee de stdin (para mandar paquetes Improv) |
| `SIM_WIFI=down` | El WiFi no conecta; `WiFi.begin("simulador-ok", …)` sí conecta |
| `SIM_AP_STATIONS` | Cuántos aparatos hay conectados al AP (0 por defecto) |
| `SIM_CLIENT_IP` | IP que ve el panel para todas las peticiones (p. ej. `192.168.1.50` = un aparato de la casa, fuera de la red del AP) |
| `SIM_GATEWAY` *(fase 2)* | IP de la puerta de enlace simulada (`WiFi.gatewayIP()`); sin ella es `127.0.0.254`, para que `127.0.0.1` no cuente como router |
| `SIM_CLOUD_URL` *(fase 3)* | Prefijo (p. ej. `http://127.0.0.1:<puerto>`) que reemplaza a `https://siblock.s-dev.win`: ahí contesta el Worker de mentira (`NubeFalsa` de `test_sim.py`). Cada petición a la nube se anota en el registro del simulador |

```bash
make -C sim
(cd sim && SIM_FLASH=./sim-flash ./esp32-sim)   # DNS 127.0.0.1:5353, panel http://127.0.0.1:8080 (clave-de-prueba)
python3 sim/test_sim.py                         # en otra terminal; necesita python3-dnspython
python3 sim/test_sim.py --lento                 # + 70 s: la pausa vence sola
python3 sim/test_sim.py --solo-nuevas           # solo las secciones 12 en adelante
pkill -x esp32-sim                              # nunca pkill -f
```

`test_sim.py` hace pruebas de punta a punta, agrupadas por sección:

0. el simulador: `nvs.json` existe y `esp32-sim-fresh` es ejecutable;
1. bloqueo por lista (A/AAAA/TXT y mayúsculas);
2. reenvío al DNS real; 2b. caché; 2c. DNS por TCP;
3. paquetes raros (no deben tumbarlo);
4. panel: permisos; 4b. Host (`siblock.local` y `adblock.local`), tamaño y bloqueo por IP
   (incluida la tabla llena); *(fase 2)* la página trae `comprobar`, `algo`, `hallazgos`,
   `grafica`, `ver24`, `ver7`, `top`, `saltan`, `relay` y no usa `innerHTML`;
5. reglas propias; *(fase 2)* 5b. nombres especiales y Private Relay: canario NXDOMAIN (A y AAAA)
   y `canary` en `/api/stats`, nombre de prueba (A = 127.0.0.1 con TTL 0, AAAA vacío),
   `/api/options` (401, 403, 400 con `relayBlock=2` o vacío), Private Relay apagado/encendido y en
   pausa, `relay_block` en una placa nueva y que "borrar la configuración" lo quite;
   5c. comprobación, diagnóstico e historia: GIF con `Host` de prueba (200, 43 B, `no-store`) y con
   `Host` falso (403), `/api/stats` con `Host` de prueba (403), CSP con `img-src`,
   `/api/diagnose` (`usa_siblock`, `navegador_dns_propio`, desde otro aparato `no_usa_siblock` +
   `ipv6`, sin anti-CSRF 403, sin dominios ni IPs en la respuesta), `dns.google` en `bypass` y
   `dns_cifrado`, `/api/history` (401 sin sesión, 168 horas, top con `doubleclick.net`, dominio de
   70 caracteres cortado a 63), router como DNS con `SIM_GATEWAY` (60 consultas sí, 10 no) e
   historia que sobrevive a un reinicio;
6. pausa;
7. nombres maliciosos en el registro; 7b. anti-rebinding y nombres tramposos;
8. carga (3000 consultas, 50 a la vez);
9. actualizar la lista desde el panel;
10. que la pausa venza sola (solo con `--lento`);
11. configuración guardada: `nvs.json` tiene hash, sal, `panel_iter = 10000` y el WiFi, y **no**
    tiene la contraseña en texto; `/api/stats` trae `version`;
12. primer uso (`esp32-sim-fresh`): crear la contraseña, 409, login con `:` y `€`, cambiar la
    contraseña, `/api/backup-ap`; 12b. ventana cerrada a los 31 min; 12c. NVS a medias (corte
    de luz al guardar); 12d. corte de luz a mitad de **cambiar** la contraseña (la nueva completa
    vale y se termina de copiar al arrancar; una a medias se descarta y sigue la vieja, sin dejar
    `/api/setup` abierto);
13. botón BOOT: un toque abre la ventana; 10 s borra la configuración (sale con código 3), las
    reglas siguen;
14. Improv por USB (`SIM_SERIAL_STDIN=1`): `0xA0`, WiFi, estado, comando desconocido y que las
    líneas `STATS` sigan saliendo; 14b. con el WiFi de la casa conectado, pedir otra red no se da
    por buena (error `0x03` a los 20 s, nada guardado);
15. modo de configuración (`SIM_WIFI=down`): se abre a los 2 min, DNS cautivo, redirección 302,
    escaneo con nombres raros, `/api/wifi` con datos malos, sin CSRF y desde la casa
    (`SIM_CLIENT_IP`), conexión buena, cierre solo a los 15 min; abierto con BOOT y una red de la
    casa que choca con la del AP: nadie cuenta como cliente del AP (panel normal, `/api/wifi`
    403, DNS sin portal cautivo);
16. *(fase 2)* opción de Private Relay en una placa nueva;
17. *(fase 2)* router que hace de DNS e historia que sobrevive al reinicio;
18. *(fase 3)* nube apagada de fábrica (`esp32-sim-fresh` con un Worker de mentira, `NubeFalsa`,
    un `http.server` en un puerto libre que anota cada petición): con la NVS vacía, 25 h de reloj
    (`.clock_ms`, de hora en hora) sin **ninguna** petición; con `cloud_share=2` y `ver_check=2`
    escritos a mano, otras 25 h sin peticiones. Luego, opciones: `GET /api/cloud` (401 sin sesión,
    403 sin anti-CSRF, JSON del contrato con todo apagado e `installId` de 32 hex), `share=2` →
    400 y sigue apagado, `share=1` → al minuto llega `POST /api/v1/placa` con solo `i`, `v`, `b`,
    `Content-Type: application/json` y `User-Agent: SiBlock/…`; con la nube en 500, `lastPing:"fail"`
    y reintento a la hora; `versionCheck=1` → `GET /version.json` sin cuerpo, `latest` y
    `update:false` con la versión `dev`; con las dos apagadas, 25 h más sin peticiones;
    18b. reportes desde el panel: apagado → 409 y nada sale; encendido → 200 y el cuerpo exacto;
    valores malos y cuerpo de 600 B → 400/413 sin salir; dominio con mayúsculas y espacios llega
    limpio; nube en 429 → 502 `limite`, nube caída → 502 `sin_internet`, sin sesión → 401;
    18c. panel: trae «Compartir con SiBlock», el aviso completo, «No debió bloquearse»,
    `reporte.html#i=` y `contacto.html#diag=`, la CSP no cambió y no hay `<script>` nuevo;
    18d. borrar la configuración (BOOT 10 s) apaga las dos opciones, cambia el `installId` y
    deja 25 h sin peticiones.

19. *(fase 5)* aparatos y horarios sin rutas: carpeta preparada a mano (`familia.txt` con sal
    `00…0f`, `Tele` en la casilla 0 y un horario "todo internet" en la hora actual); `example.com`
    desde `127.0.0.2` → `0.0.0.0`, desde `127.0.0.3` → IP real; con `.arp` = `127.0.0.2 -` → IP
    real (falla abierto); `/familia.txt` roto → arranca y no bloquea a nadie;
20. *(fase 5)* rutas de aparatos y horarios: 401 y 403 en las 4; nombre `<b>Tele</b> 📺` que se lee
    igual; 33 bytes → 400; huella inventada → 400; pausa de un aparato (pasa en `.2`, sigue
    bloqueado en `.3`) y `minutes=0`; horario puesto y borrado; `forget` que borra su horario;
    reinicio que conserva nombre y horario y pierde la pausa; **MAC que rota** (otra MAC en `.arp`
    y el reloj 6 min adelante → aparato nuevo sin nombre, el horario ya no lo bloquea);
20b. *(fase 5)* con `SIM_FS_FAIL_PATH=/familia.new` (la flash no guarda): renombrar, guardar un
    horario y olvidar contestan 500 y **nada cambia** (sigue el nombre, sin horario, la hora de la
    casa igual, no bloquea);
21. *(fase 5)* listas: con `lists=7` en la NVS, las líneas `[lista] 1/4 …` en el orden estafas,
    español, general ×2; `lists=2` → 400; `lists=3` → 200 y queda en la NVS; `lists=9` en la NVS
    se porta como `1`;
22. *(fase 5)* LED: sin WiFi, `[sim] led 1` y `led 0` alternando en menos de 1 s; con WiFi y
    lista, fijo; nunca más de 20 líneas `[sim] led` por segundo (solo escribe al cambiar);
    `/api/history` con `ahorroMb` entero;
23. *(fase 5)* respaldo: placa A con 2 reglas, 1 aparato y 1 horario → `GET /api/backup` (401 sin
    sesión; cabecera, `fin`, sin la contraseña del WiFi, sin `install_id`, sin IPs `127.0.0.`);
    placa B nueva: subir en 2 pedazos, `check`, `apply` → todo igual; sin `fin` → error y B no
    cambia; `X-Parte: 2` sin el `1` → 400 (el número de pedazo va en el header `X-Parte`, no en
    la URL); `/restore.tmp` o `/familia.txt` de más de 24 KB → "el archivo es demasiado grande" con
    `linea` 0; 200 reglas largas → `GET /api/backup` da 500; pedazo de 3073 B o más de 24 KB → 413; sin sesión no escribe;
    BOOT 10 s borra `/familia.txt`.

*(Fase 5)* El simulador finge la tabla ARP: la IP `127.0.0.x` tiene la MAC `02:00:00:00:00:<x>`,
salvo que `$SIM_FLASH/.arp` (se relee en cada consulta) diga otra cosa: `127.0.0.x 02:aa:bb:cc:dd:ee`
(otra MAC) o `127.0.0.x -` (no está en la tabla).

Las instancias que lanza el propio test se cierran con `terminate()`/`wait()`, nunca con `pkill`.

Al final dice `TODO BIEN` o cuántas fallas hubo. Mira también el registro del simulador:
AddressSanitizer imprime ahí cualquier error de memoria.

El header `X-Sim-Client` cambia la IP del cliente **solo en el simulador**, para probar el
bloqueo por IP. El firmware real lo ignora.

Lo que el simulador **no** reproduce:

- la RAM y la velocidad reales (incluido el tiempo del PBKDF2);
- la fragmentación de memoria;
- el vigilante;
- los cortes de WiFi reales y el AP de verdad;
- el tiempo real de escribir en la flash;
- el chip USB (CH340 / CP2102) y ESP Web Tools;
- *(fase 3)* el TLS real del ESP32 contra Cloudflare (el simulador usa `curl` con los
  certificados de la PC).

Eso se prueba en la placa.

## 3. Placa real (cuando llegue)

### Lo de siempre

1. `pio run -t upload && pio device monitor`: que aparezca `[listo] DNS en …`.
2. Primera descarga: que diga `[lista] lista nueva: ~161000 dominios`. Anota cuánto tarda y la
   RAM libre en las líneas `STATS`.
3. `dig @<ip> doubleclick.net` → `0.0.0.0`; `dig @<ip> google.com` → IP real.
4. Correr el mismo `test_sim.py` contra la placa:
   `SIM_DNS_PORT=53 SIM_WEB_PORT=80`, cambiando `DNS`/`WEB` a la IP de la placa. Las pruebas que
   usan `X-Sim-Client`, `esp32-sim-fresh`, `.clock_ms` o `.boot` no aplican.
5. Que a 1 min del arranque diga `[dns] autoprueba del puerto 53: ok`. Si no lo dice, la placa no
   entrega paquetes a su propia IP y la autoprueba queda apagada (no es grave).
6. Apagar el router 1 min y volver a prenderlo: el DNS tiene que volver solo, y debe aparecer
   `[wifi] reconectado`.
7. Dejarla 48 h: que la RAM libre de `STATS` no baje con el tiempo.

### Fase 1 (diseño, 1.10)

| Prueba | Qué mirar |
|---|---|
| Instalar desde la página en **Windows, Mac y Linux**, con placa **CH340** y con **CP2102** (6 combinaciones) | Que se vea el puerto (drivers), que instale y que pida el WiFi |
| Improv | El WiFi llega por USB; la página muestra la contraseña de respaldo y "Abrir mi panel" lleva al panel |
| Primera instalación sin lista | Justo después de dar el WiFi, pulsar "Ver mi contraseña de respaldo": la página dice que se está preparando y la muestra al terminar de bajar la lista (medir cuánto tarda; la página espera hasta 3 min) |
| Nombres del instalador | Al actualizar, el botón dice "Update SiBlock" y si aparece "Erase device"; corregir la guía en inglés de `site/index.html` y quitar el "falta verificar" |
| Actualizar desde la página | El WiFi y la contraseña del panel siguen después |
| Modo de configuración | Se ve la red `SiBlock-XXXX` con WPA2; la contraseña de respaldo entra; un celular Android y un iPhone abren solos la página; elegir red funciona; `/api/wifi` desde la red de la casa → 403 |
| BOOT corto (< 2 s) | Abre la ventana de 30 min |
| BOOT medio (3–8 s) | LED fijo; al soltar abre el modo de configuración |
| BOOT largo (≥ 10 s) | Borra WiFi y contraseñas; la lista y las reglas siguen |
| LED | Parpadeo, fijo y apagado en los tiempos de [operacion.md](operacion.md) |
| RAM y flash | `pio run` (flash < 92 %) y la RAM libre de `STATS` con el panel y el portal abiertos |
| Tiempo real del PBKDF2 con **10 000** iteraciones | Medir cuánto tarda el login; ajustar `kPanelIterations` para quedar en 0,3–0,5 s |

### Fase 2 (diseño, 2.7)

| Prueba | Qué mirar |
|---|---|
| Comprobación desde un **celular real** con el WiFi de la casa (SiBlock como DNS del router) | El panel dice "✅ Este aparato está usando SiBlock."; con el celular en el WiFi pero con el DNS puesto a mano en otro (p. ej. 1.1.1.1), "❌" y "Algo no funciona" da `no_usa_siblock` + `ipv6` |
| **Firefox** con DoH automático (sin tocar ajustes) | `canary` sube en `/api/stats`; Firefox no usa su DoH en la casa; la comprobación da ✅ |
| Firefox con DoH puesto **a mano** | "Algo no funciona" da `navegador_dns_propio` + `ipv6`, con "Ver qué hacer" |
| **iPhone con iCloud Private Relay**, opción apagada | Aparece en "Aparatos que se saltan SiBlock" con Private Relay; "Algo no funciona" da `private_relay` |
| iPhone con Private Relay, opción **encendida** | `relay` sube; el iPhone avisa que Private Relay está apagado en esta red; la comprobación desde Safari da ✅ |
| **Android con "DNS privado"** (p. ej. `dns.google`) | Aparece en "Aparatos que se saltan" con DNS cifrado y "Algo no funciona" da `dns_cifrado` (falta verificar: si Android ya tenía la dirección guardada, puede no preguntar a SiBlock) |
| Router **mesh** que hace de DNS | "Algo no funciona" da `router_dns` |
| **Gráficas tras un reinicio** | Dejarla más de 1 h con la hora de internet, reiniciar: las barras de antes siguen (se pierde como mucho la última hora) |
| Gráficas sin hora (NTP bloqueado) | "Todavía no hay datos"; los contadores de arriba sí suben |
| Panel en el celular (375 px), claro y oscuro | Nada se sale; botones fáciles de tocar; las tablas se deslizan de lado |
| RAM y flash | `pio run` (flash < 92 %) y la RAM libre de `STATS` con las gráficas abiertas |

### Fase 3 (diseño, 3.3)

Pide el Worker publicado (ver [`cloud/DEPLOY.md`](../cloud/DEPLOY.md)).

| Prueba | Qué mirar |
|---|---|
| Placa recién instalada, 25 h sin tocar nada | Por la serie no sale ninguna línea `[nube]`; en el router (si deja ver conexiones) la placa no habla con `siblock.s-dev.win` |
| **Certificados de Cloudflare en el ESP32** (falta verificar) | Con "Compartir" encendido, `[nube] aviso diario: ok` al minuto. Si sale `falló` siempre, revisar qué raíz usa hoy `siblock.s-dev.win` (`openssl s_client -connect siblock.s-dev.win:443 -showcerts`) y que esté en `certs.h` (GTS Root R1/R4, SSL.com TLS RSA/ECC 2022 o las de antes) |
| Aviso de versión | Con "Avisarme de versiones nuevas" y una placa `vX.Y.Z` más vieja que la publicada: `[nube] versión publicada: …` y el aviso en el panel |
| Reportes desde el panel | Módem y «No debió bloquearse» dan «listo» y aparecen en D1 (`wrangler d1 execute siblock --remote`); con el WiFi cortado, «SiBlock no pudo llegar a internet» |
| RAM durante el TLS | La RAM libre de `STATS` antes y después del aviso diario; que la descarga de la lista siga funcionando el mismo día |
| Enlaces a la página | «Reportar desde la página» lleva el número; «Escríbenos» llena el diagnóstico y solo lo manda si se marca la casilla |
| Borrar la configuración | Después, `/api/cloud` da todo apagado y otro `installId` |

### Fase 5 (diseño, 5.3)

| Prueba | Qué mirar |
|---|---|
| **Tabla ARP con más de 10 aparatos** (falta verificar) | Con 15 o más aparatos usando internet a la vez, `GET /api/devices` da la huella de todos (la tabla ARP de lwIP trae 10 entradas por defecto). Si no alcanza, los que faltan salen "Todavía no lo reconozco" y no se les aplica nada |
| Teléfono con **dirección privada** (MAC que cambia) | Al rotar sale como aparato nuevo sin nombre; nada se bloquea de más |
| Pausa por aparato | "Pausar 1 h" en un celular: ese celular ve anuncios, otro aparato no |
| Horario real | "Todo internet" para un aparato por 10 min: no abre nada en ese rato y vuelve solo; sin hora de internet (NTP bloqueado) no bloquea |
| **LED real** | Fijo con todo bien; parpadeo lento al bajar la lista; rápido sin WiFi o con el modo de configuración; que se vea a través de la caja |
| **Tamaño de las listas nuevas** (falta verificar) | Con las tres listas: `[lista] lista nueva: N dominios` y `skipped` de `/api/options`; que la de estafas quede entera |
| **RAM al bajar las listas** | La RAM libre de `STATS` durante las 4 descargas (`[lista] 1/4 … (RAM libre N KB)`); que no reinicie por RAM |
| Respaldo entre dos placas | Bajar en una, subir en otra: reglas, nombres y horarios iguales; el WiFi y la contraseña no cambian |
| Página en inglés y botón de apoyo | `/en/` en un celular; el botón de apoyo solo aparece si CI puso el enlace |

## Compilar el firmware

```bash
pio run            # flash < 92 % (CI falla si se pasa)
PLATFORMIO_BUILD_FLAGS='-DSIBLOCK_VERSION=\"v0.0.0-prueba\"' pio run   # versión como la pone CI
```

Con la fase 1 (`pio run`, 21-11-2026): RAM 19,7 % (64 620 de 327 680 B) y flash 70,5 %
(1 108 685 de 1 572 864 B). Antes de la fase 1: RAM ~19,6 %, flash ~67,5 %.

El build usa `lib/WebServer` (la copia parcheada) y no la del framework. Se comprueba con
`pio run -v | grep lib/WebServer`.

## Página de instalación

```bash
cd site && npm test     # = node --test tests/*.test.mjs: instalar.js, improv-lite.js y (fase 2) guías y páginas
```

*(Fase 2)*:

- `guia-router.test.mjs`: país por idioma del navegador (`es-MX` → México; sin región o
  desconocido → "otro"), proveedores por país (siempre termina en "Otro proveedor"), guía general
  de 7 pasos con `verificada:false` cuando no hay ficha con fuente, `pintarGuia` que no crea
  elementos con un título como `<img src=x onerror=…>`, ids únicos.
- `paginas.test.mjs`: recorre **todos** los `site/*.html`: `lang="es"`, viewport, `<title>` con
  "SiBlock", ningún `<script>` en línea ni `on…=` ni `javascript:`, enlaces internos (archivo e
  `id`) que existen, el único correo es `contacto@s-dev.win`, enlaces externos con `noopener`.

*(Fase 3)*:

- `nube.test.mjs`: analítica (token vacío → no se agrega nada; con token, un solo `<script>`
  aunque se llame dos veces), Turnstile, `enviar` con un `fetch` falso (200, 429, red caída) y
  `contarPaso` que no manda pasos fuera de la lista;
- `reporte.test.mjs`: `#i=` bueno y malos (mayúsculas, HTML, 33 hex), cuerpos con valores fuera de
  las listas → nada, dominio limpiado a minúsculas;
- `contacto.test.mjs`: `#diag=` bueno, roto, enorme o con claves de más (`ip` se quita), códigos
  con HTML fuera; correo con `\r\nBcc:` y mensaje de 9 caracteres rechazados;
- `graficas.test.mjs`: barras en SVG (sin datos → «Todavía no hay datos.»), nombres con `<script>`
  como texto, semanas cortas; la guía del router elige el país que da `/api/v1/pais`;
- `paginas.test.mjs` suma: todas las páginas cargan `js/inicio.js`, enlazan a
  `privacidad.html` y la CSP tiene los orígenes de Web Analytics y Turnstile.

*(Fase 5)*:

- `textos.test.mjs`: `TEXTOS.es` y `TEXTOS.en` con las mismas claves y ningún valor vacío; `idioma`
  con `lang="en"`, `"en-US"`, `"es"` y sin `lang`; cada `texto('…')` de `instalar.js` existe en
  los dos idiomas;
- `apoyo.test.mjs`: `enlaceApoyo` deja pasar `https://ko-fi.com/siblock` y devuelve `''` con vacío,
  `http://`, comillas o `javascript:`; sin enlace, `#apoyo` sigue oculto;
- `paginas.test.mjs` suma: también recorre `site/en/*.html` (`lang="en"`, enlaces con `../`), cada
  página con versión en el otro idioma enlaza a ella (`hreflang`), y `#luz` existe en
  `index.html` y `en/index.html` con un `<svg role="img" aria-label=…>`.

`node --test tests/` (con la carpeta) no funciona en Node 22: hay que pasar los archivos.

## Worker (`cloud/`) *(fase 3)*

```bash
cd cloud && npm ci         # una vez (versiones fijas en package-lock.json)
cd cloud && npm test       # vitest con @cloudflare/vitest-pool-workers: Worker y D1 locales
cd cloud && npm run tipos  # tsc --noEmit
npx wrangler deploy --dry-run --outdir dist   # dentro de cloud/: compila sin publicar (borrar dist/ después)
```

Las pruebas no salen a internet: llaman a `manejar(req, env, ctx, deps)` con `deps.fetch`
(Turnstile, llaves de Access) y `deps.ahora` falsos, y `LIMITE`/`CORREO` de mentira. Las tablas se
crean en una D1 local con las migraciones de `cloud/migrations/`.

| Archivo | Qué prueba |
|---|---|
| `http.test.ts` | 404, 405, 413 (9000 B), 400 (`[1]`, JSON roto), `Origin` de otra página → 403, sin `Origin` pasa, las tablas existen |
| `validar.test.ts` | Listas iguales al contrato, `esId`, `esDominio`, `esVersion`, `esCorreo` (`<correo>\r\nBcc:` → no), `hashId` estable y distinto del número |
| `turnstile.test.ts` | Token bueno, `hostname` de otro sitio, `success:false`, red caída, token de 3000 caracteres (no llama) |
| `reportes.test.ts` | Una fila por número (con `hashId`, no el número), Turnstile solo desde la página, valores malos, clave `ip` que no se guarda, límite → 429, ninguna columna de IP (`PRAGMA table_info`), `/pais` |
| `placas.test.ts` | Bloqueo en mayúsculas → 400, uno por número y dominio, aviso de placa (el último del día gana), `b` y `v` malos, embudo que suma |
| `estadisticas.test.ts` | Umbral de 5 por país y `otros`, placa de hace 7 días fuera, umbral de 3 por módem, `otro` y `dns_ok:0` fuera, siempre 12 semanas |
| `access.test.ts`, `dueno.test.ts` | JWT con un par RSA de prueba: bueno, vencido, otra `aud`, otro `iss`, otra llave, `alg:none`, sin header, variables vacías; resumen en orden y top 50 |
| `contacto.test.ts` | Dos correos (dueño y respuesta), asunto sin texto de la persona, sin `<script>`, respuesta sin nombre ni mensaje, correo con `\r\n`, mensaje de 9 o 2001, tema raro, sin Turnstile, tope de 200, respuesta apagada, correo que falla → 500 sin contar, `sinHtml` |
| `limpieza.test.ts` | Filas de hace 401 días se borran, las de 399 se quedan |

Nada de esto publica el Worker: `wrangler deploy` real, `wrangler pages deploy` y `wrangler d1 … --remote`
solo los corre el autor con [`cloud/DEPLOY.md`](../cloud/DEPLOY.md).

`make_site_files.py` (CI escribe `version.json` y `js/config.js` de la página en cada versión) se
prueba con `python3 .github/scripts/test_make_site_files.py`. *(Fase 5)* La prueba suma el enlace de apoyo
(`--apoyo-url`, variable `SIBLOCK_APOYO_URL`): uno bueno queda en el archivo, sin la opción queda
`apoyoUrl: ''`, y uno malo (`http://`, comillas, `<`, `javascript:`, espacios, más de 100 letras)
sale con código 2 sin escribir nada.

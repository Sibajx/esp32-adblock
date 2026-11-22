# SiBlock: diseño

Fecha: 2026-11-21. Fuente de las ideas y decisiones: [`docs/ideas-pendientes.md`](../../ideas-pendientes.md)
(las secciones se citan como §2.4, §3.3, etc.). Este documento ordena ese material en fases y
detalla la **fase 1**, que es la que se planea primero. Cada fase siguiente tendrá su propio
documento corto antes de su plan.

## Intención

Hoy `esp32-adblock` es un bloqueador de anuncios por DNS para el ESP32 clásico (WROOM-32, sin
PSRAM) que solo puede usar quien sabe compilar firmware. Se quiere convertir en **SiBlock**: algo
que una persona sin conocimientos técnicos pueda comprar, instalar y mantener sola, y que la trate
"como familia": palabras simples, nadie se queda atorado, nada que asuste.

Es para la casa del autor y su familia, y para cualquiera de fuera con el mismo trato.

## Metas

- Instalar desde el navegador con un botón, sin instalar programas ni escribir comandos.
- Dar el WiFi y la contraseña del panel sin recompilar.
- Saber en todo momento si un aparato está protegido y, si no, qué hacer.
- Aprender de la comunidad (módems, proveedores, bloqueos de más) sin recoger datos personales.
- Código público y auditable.

## No metas (por ahora)

- OTA (no cabe con la lista; se actualiza por USB desde la página).
- DHCP en el ESP32 (espera a la placa real, §Decisiones).
- Soportar Firefox y Safari en la instalación (WebSerial solo existe en Chrome y Edge).
- Placas que no sean el ESP32 clásico de 4 MB.

## Anti-metas (si pasa esto, falló aunque funcione)

- La casa se queda sin internet por culpa de SiBlock.
- Se recoge algo que identifique a una persona o una casa (IP, MAC completa, dominios visitados).
- Se cambia algo en el router sin que la persona lo vea y diga "Sí".
- Se guarda la contraseña del router.
- Se promete bloquear lo que un DNS no puede (anuncios de YouTube, Instagram, etc.).

## Restricciones

- ESP32 clásico: ~320 KB de RAM sin PSRAM, 4 MB de flash, 1,5 MB de programa (hoy al 67,9 %),
  LittleFS de 2,44 MB con la lista, NVS de 20 KB (`partitions.csv`). No se cambian las
  particiones: cambiarlas borra la lista y obliga a reinstalar.
- Todo lo de `src/` debe seguir corriendo en el simulador (`sim/`) y pasando `test_sim.py` y
  `test_host`. Lo nuevo trae sus pruebas.
- Seguridad: lo que toca auth, formularios o endpoints pasa por `security-suite` (regla del
  autor). El panel sigue siendo HTTP (riesgo bajo aceptado, `docs/seguridad.md`).
- Privacidad: sin cookies, sin IPs guardadas, todo lo que sale de la placa apagado de fábrica.
- Español primero en todo texto que vea la persona.

## Decisiones ya tomadas (por el autor)

| Tema | Decisión |
|---|---|
| Nombre | **SiBlock** |
| Dirección | `siblock.s-dev.win`, en **Cloudflare Pages** |
| Código | **Público, GPLv3**, después de una revisión completa de seguridad que incluya todo el historial |
| Orden | **Por fases** (abajo) |
| Placas | Cada persona compra y carga la suya |
| Reportes de módems | Desde la página **y** el panel; **solo opciones fijas**; con aviso de privacidad; en el panel apagado de fábrica |
| Contacto | Formulario → `contacto@s-dev.win` (Cloudflare Email Routing reenvía al correo personal, que no va en el repo) |
| Panel del dueño | Detrás de Cloudflare Access, sesión de **30 días** |
| DHCP en el ESP32 | Espera a la placa real |
| iCloud Private Relay | Apagarlo es **una opción que la persona activa** |
| Router automático | Siempre muestra qué cambia y pide "Sí"; no guarda credenciales; deshace solo si falla; nunca toca firewall (el modo estricto va aparte con su propio "Sí") |

## Enfoques considerados

- **A. Por fases** (elegido): primero lo que permite instalar sin la placa; después acompañamiento,
  Cloudflare, lo que necesita la placa y extras. Dentro de cada fase, los agentes trabajan en
  paralelo en lo que no comparte archivos (firmware / página / Cloudflare); en el firmware, uno a
  la vez.
- B. Todos los frentes a la vez: descartado, casi todo toca `src/web.cpp` y `src/main.cpp` y se
  pisarían.
- C. Solo lo mínimo hasta tener la placa: descartado, deja parado todo lo que no la necesita.

## Fases

| Fase | Qué entrega | Necesita la placa | Secciones de las notas |
|---|---|---|---|
| **1. Instalar sin saber** | Firmware configurable sin recompilar, CI que publica, página de instalación mínima, repo público | No (se valida al final con la placa) | §0, §1, Decisiones |
| 2. Acompañamiento | Comprobación "¿estoy protegido?", botón "algo no funciona", canarios de DoH y Private Relay (opción), guías por marca y proveedor (§2, §2.1, §2.3, §2.5), gráficas del panel (§3.3, parte placa), panel cómodo en el celular | No | §2–§2.5, §3, §3.2, §3.3, §3.5 |
| 3. Cloudflare | Worker + D1 de reportes y aprendizaje (§2.2), gráficas públicas y del dueño (§3.3), Contáctanos (§3.4), aviso de versión (§3.2-10) | No | §2.2, §3.3, §3.4 |
| 4. Con la placa | Pruebas reales (§5), asistente del router con FortiGate y luego MikroTik/OpenWrt (§2.4), modo estricto (§2.5), decisión del DHCP | **Sí** | §2.4, §2.5, §5 |
| 5. Extras | Nombres de aparatos, pausa por aparato, lista contra estafas, horarios, LED, respaldo de configuración, lista en español, cajita 3D, video, página en inglés | Parcial | §3.1, §3.5 |

## Fase 1 en detalle

### 1.1 Configuración guardada en la placa (NVS)

- Nuevo módulo `src/settings.{h,cpp}` sobre `Preferences` (NVS, espacio `siblock`). Guarda:
  - `wifi_ssid`, `wifi_pass`;
  - `panel_hash`, `panel_salt`: la contraseña del panel **nunca en texto**, solo
    PBKDF2-HMAC-SHA256 (mbedtls, ya está en el core) con sal aleatoria de 16 B. Iteraciones: las
    que tarden ~0,3–0,5 s en la placa (medir; en el simulador da igual);
  - `ap_pass`: contraseña del modo de configuración (1.3).
- `secrets.h` pasa a ser **opcional**: si existe y define valores, se usan como valores de fábrica
  solo cuando NVS está vacío (para quien compila). `main.cpp` deja de fallar sin `secrets.h`. El
  firmware publicado se compila **sin** `secrets.h`.
- La validación de la contraseña sigue igual (12 a 64 caracteres) y se aplica también al crearla.
- `web.cpp` deja de comparar con `WEB_PASSWORD`: el login Basic compara el hash. El resto de la
  sesión (tokens, bloqueo por IP, R8) no cambia.
- Simulador: shim de `Preferences` que guarda en un archivo dentro de `SIM_FLASH`.

### 1.2 WiFi desde la página (Improv Serial)

- Implementar Improv Serial (protocolo abierto de ESP Web Tools) en el firmware: recibe SSID y
  contraseña por USB, prueba la conexión, la guarda en NVS y contesta con la URL del panel
  (`http://<ip>/`). La página la muestra con un botón "Abrir mi panel".
- Puede usarse la librería de referencia `improv-wifi/sdk-cpp` (MIT, compatible con GPLv3) o
  escribir lo mínimo; lo decide quien construye, midiendo flash.
- La salida por serie actual (`STATS`, mensajes) no debe romper Improv: Improv tiene su propia
  cabecera de paquete y ESP Web Tools ignora el resto, pero se prueba.

### 1.3 Modo de configuración por WiFi (respaldo)

Para cuando Improv no se pudo usar o el WiFi guardado dejó de funcionar.

- Se entra si: no hay WiFi guardado y pasan 2 min sin Improv; el WiFi guardado falla 10 min
  seguidos (hoy eso reinicia; se cambia a entrar en este modo y seguir intentando el WiFi guardado
  en segundo plano); o se mantiene BOOT entre 3 y 8 s.
- Abre un AP `SiBlock-XXXX` (4 últimos hex de la MAC) **con WPA2**. La contraseña del AP
  (`ap_pass`, 10 caracteres sin letras confusas) se genera al azar la primera vez, se guarda en
  NVS, se imprime por serie y **la página de instalación la muestra** al terminar de instalar
  ("Guárdala: la necesitas si un día cambias tu WiFi"). *Decisión del diseño*: un AP abierto
  permitiría a un vecino apuntar la placa a su propio WiFi y ver el DNS de la casa.
- Portal cautivo: DNS que contesta todo con la IP del AP y una página chica para elegir red y dar
  contraseña. Al guardar, sale del modo y se conecta.
- Mientras está en este modo, el DNS de la casa no funciona (la placa no está en la red). Es
  aceptable porque solo pasa sin WiFi.
- El modo se cierra solo a los 15 min si nadie entra, y vuelve a intentar el WiFi guardado.

### 1.4 Crear la contraseña del panel en el primer uso

- Si no hay `panel_hash`, el panel muestra "Crea tu contraseña" en vez del login.
- **Ventana de creación**: solo durante los primeros 30 min tras el arranque, o 30 min después de
  una pulsación corta de BOOT (< 2 s). Fuera de la ventana, el panel dice "Pulsa el botón BOOT de
  tu SiBlock una vez y vuelve a intentar". *Decisión del diseño*: sin ventana, cualquiera en la red
  podría crear la contraseña antes que el dueño.
- Endpoint nuevo `POST /api/setup` (mismos topes y header `X-Requested-With` que el resto);
  responde 409 si ya hay contraseña. Tras crearla, entra con sesión igual que `/api/login`.
- Cambiar la contraseña desde el panel (`POST /api/password`, pide la actual).

### 1.5 Borrar la configuración

- Mantener BOOT **10 s o más**: borra el espacio NVS `siblock` (WiFi, contraseña del panel,
  contraseña del AP) y reinicia. **No** borra `/list.bin` ni `/custom.txt` (las reglas propias
  quedan; *decisión del diseño*: perder las reglas por olvidar la contraseña es demasiado castigo;
  si se quieren borrar está "Borrar todo" desde la página).
- El LED azul (GPIO2) parpadea rápido mientras se mantiene, para que se note cuándo soltar.
  Duraciones de BOOT en resumen: < 2 s abre la ventana de 1.4; 3–8 s modo de configuración;
  ≥ 10 s borrar.
- GPIO0 (BOOT) solo se lee después del arranque; no se configura como salida.

### 1.6 Nombre visible

- Mensajes, panel y página dicen **SiBlock**. `DEVICE_HOSTNAME` pasa a `siblock`
  (`http://siblock.local`). El chequeo de `Host` acepta también `adblock` y `adblock.local` para
  no romper la instalación que ya exista.
- El header anti-CSRF (`X-Requested-With: adblock`) se mantiene tal cual: cambiarlo no aporta y
  rompe scripts.

### 1.7 Compilación y publicación (GitHub Actions)

- En cada push y PR: `make -C test_host test`, simulador + `test_sim.py` (sin `--lento`), `pio run`.
- En cada tag `vX.Y.Z`:
  1. `pio run` sin `secrets.h`;
  2. `esptool.py merge_bin` → `siblock-vX.Y.Z.bin` desde 0x0 (bootloader, particiones, app);
     el archivo **no incluye LittleFS** para no pisar la lista (ESP Web Tools con "borrar todo"
     opcional);
  3. `manifest.json` de ESP Web Tools (chip `ESP32`, `improv: true`, versión);
  4. publica el `.bin` en la Release de GitHub y despliega `site/` + `firmware/` a Cloudflare
     Pages con `wrangler pages deploy` (token de Cloudflare como secreto del repo, con permiso
     solo de Pages en esa cuenta).
- La versión se inyecta como `-DSIBLOCK_VERSION="vX.Y.Z"` y se ve en el panel y en `/api/stats`.

### 1.8 Página de instalación mínima (`site/`)

- HTML + CSS estático, sin framework, en español. Contenido:
  1. Qué es SiBlock en 3 líneas, y qué **no** bloquea (YouTube, Instagram…).
  2. Guía de compra (§0): qué placa, cable **de datos**, drivers CH340/CP2102 por sistema.
  3. Botón ESP Web Tools "Instalar SiBlock"; si el navegador no es Chrome/Edge, un aviso en lugar
     del botón.
  4. Tras instalar: WiFi por Improv, contraseña del AP (1.3) para guardar, botón "Abrir mi panel".
  5. Siguiente paso: poner la IP en el router, con una guía general (las guías por marca son
     fase 2).
- Sin cookies ni analítica en esta fase (la analítica entra en fase 3).
- `site/` vive en este mismo repo.

### 1.9 Hacer público el repo

En este orden, antes de cambiar la visibilidad:
1. `security-suite` en modo completo sobre el repo, incluido todo el historial de git (secretos,
   correos personales, referencias a otros equipos de la casa). Una CRÍTICA bloquea hasta que el
   autor decida.
2. Confirmar que los reportes de seguridad siguen fuera del repo.
3. `LICENSE` (GPLv3) y línea de licencia en el README; README nuevo orientado a "instálalo desde
   la página", con la sección para desarrolladores abajo.
4. El cambio de visibilidad en GitHub lo hace el autor (o se le pide confirmación explícita).

### 1.10 Pruebas de la fase 1

- Simulador: NVS vacío arranca en modo "crear contraseña"; ventana de 30 min (con reloj falso);
  409 si ya hay contraseña; login con hash; cambio de contraseña; borrar configuración no borra la
  lista; `secrets.h` ausente compila; `secrets.h` presente siembra NVS.
- Host: PBKDF2 contra vectores conocidos; generador de `ap_pass` (alfabeto, longitud).
- Improv: prueba del parser de paquetes en `test_host` (paquetes buenos, rotos, checksum malo).
- Placa (al llegar): instalación desde la página en Windows, Mac y Linux con CH340 y CP2102;
  Improv; modo AP; BOOT corto / medio / largo; RAM y flash.

### 1.11 Decisiones tomadas al planear la fase 1

Todas son *decisión del diseño* (no del autor); el autor puede cambiarlas.

- **Publicación sin pisar la configuración**: el `manifest.json` de ESP Web Tools carga tres
  partes por separado (arranque en 0x1000, particiones en 0x8000, programa en 0x10000). Un solo
  `.bin` desde 0x0 rellena el hueco de NVS (0x9000) y borraría el WiFi y la contraseña en cada
  actualización. El `.bin` unido desde 0x0 se publica igual en la Release, para instalar de cero
  con `esptool`.
- **La página lee la contraseña del AP por Improv**: después de instalar, la página abre otra vez
  el puerto USB y manda un comando propio de Improv (`0xA0`, "contraseña de respaldo"). La placa
  contesta con el nombre y la contraseña del AP. También se ve en el panel (con sesión) y se
  imprime por serie al arrancar. Quien tiene el USB ya podría borrar todo con BOOT, así que no
  abre nada nuevo.
- **El portal solo acepta cambios desde el AP**: `POST /api/wifi` responde 403 si la petición no
  viene de la red del AP (192.168.4.0/24). Así nadie en la red de la casa puede reapuntar la placa.
- **Iteraciones de PBKDF2 guardadas junto al hash** (`panel_iter`), 10 000 de arranque, a medir con
  la placa. En la placa se usa mbedtls; en la PC y el simulador, una copia portátil probada con
  los vectores del RFC 7914. Al arrancar, una autoprueba con un vector conocido.
- **Largo de la contraseña del panel en bytes** (12 a 64), igual que hoy.
- **LED mientras se mantiene BOOT**: parpadeo rápido hasta los 3 s; fijo de 3 a 8 s (soltar ahí
  abre el modo de configuración); parpadeo rápido de 8 a 10 s; a los 10 s se apaga y borra.
- **Portal que se cierra solo**: si nadie se conecta en 15 min se cierra; vuelve a intentar el WiFi
  guardado 10 min y, si sigue sin conectar, se abre otra vez.
- **Simulador**: reloj adelantable (`$SIM_FLASH/.clock_ms`), botón BOOT de mentira
  (`$SIM_FLASH/.boot`), NVS en `$SIM_FLASH/nvs.json` y un segundo binario `esp32-sim-fresh`
  compilado sin `secrets.h`.

## Fase 2 en detalle

Fuente: §2, §2.1, §2.3, §2.5, §3, §3.2 (ideas 6 y 7; la 8 va en la fase 3), §3.3 (solo "En el
panel de cada placa") y §3.5 idea 7. Lo que no viene de las notas está marcado como
*decisión del diseño* (el autor puede cambiarlo). Plan:
[`docs/superpowers/plans/2026-11-21-siblock-fase-2.md`](../plans/2026-11-21-siblock-fase-2.md).

### 2.1 "¿Este aparato usa SiBlock?" (§3)

- Idea del autor: un nombre de prueba que solo SiBlock contesta con una IP especial; el navegador
  lo carga y la página sabe si la respuesta vino de SiBlock.
- *Decisión del diseño*: el nombre es `<n>.prueba.siblock.internal` (`.internal` está reservado
  para redes privadas y nunca existe en internet; `n` es al azar en cada prueba para que no ayude
  ninguna caché). SiBlock contesta `A` con su propia IP (TTL 0) y `AAAA` vacío. El panel carga la
  imagen `http://<n>.prueba.siblock.internal[:puerto]/siblock-prueba.gif`: si carga, el aparato
  usa SiBlock; si no, no.
- *Decisión del diseño*: esa ruta con ese nombre es la única excepción al chequeo de `Host`
  (anti-rebinding); devuelve un GIF de 1×1 fijo, sin datos. La CSP del panel suma solo
  `img-src *.prueba.siblock.internal:*`.
- *Decisión del diseño*: la comprobación vive en el panel. La página pública (HTTPS) no puede
  cargar una dirección HTTP de la casa; solo explica y lleva al panel.

### 2.2 Botón "algo no funciona" (§3.2-6)

- Revisa lo común y dice qué hacer en palabras simples: ¿este aparato usa SiBlock? (2.1), ¿preguntó
  algo a SiBlock en los últimos 10 min?, DNS cifrado (DoH/DoT), iCloud Private Relay, pausa, lista
  cargada, DNS de internet que no contesta, router que hace de DNS (mesh), y el consejo de apagar el
  DNS por IPv6 del router cuando el aparato no llega.
- *Decisión del diseño*: `GET /api/diagnose` es **pública** (con el header anti-CSRF) para que
  cualquier persona de la casa lo use sin la contraseña; solo habla del aparato que pregunta (por
  su IP) y de lo que ya muestra `/api/stats`. Nunca dice qué visitó nadie.
- *Decisión del diseño*: "router que hace de DNS" = la puerta de enlace hizo el 80 % o más de al
  menos 50 consultas recientes (§2.5, mesh).

### 2.3 Canarios y aparatos que se saltan el bloqueo (§2.5, §3.2-7)

- `use-application-dns.net` → NXDOMAIN **siempre** (apaga el DoH automático de Firefox).
- iCloud Private Relay: `mask.icloud.com` y `mask-h2.icloud.com` → NXDOMAIN **solo si la persona
  lo activa** (decisión del autor). Apagado de fábrica, guardado en NVS (`relay_block`), con aviso de
  que también quita la privacidad de Private Relay. "Borrar la configuración" lo vuelve a apagar.
  *Decisión del diseño*: cuando está activado se aplica también durante la pausa.
- Detección: consultas a nombres conocidos de DNS cifrado (Google, Cloudflare, Quad9, OpenDNS,
  NextDNS, AdGuard, CleanBrowsing, Mullvad) y a `mask*.icloud.com` con la opción apagada. Se anotan
  por aparato **solo en RAM** (16 aparatos, se olvidan a las 24 h) y se muestran en el panel y en
  "algo no funciona" con qué hacer. *Decisión del diseño*: esos nombres **no** se bloquean (cortarlos
  es el modo estricto de la fase 4).

### 2.4 Gráficas del panel (§3.3, "En el panel de cada placa")

- *Decisión del diseño*: en vez de 24 + 7 pares se guardan **168 horas** (7 días × 24) de pares
  consultas/bloqueadas por hora UTC (1,3 KB de RAM): así el navegador arma "24 h" y "7 días" con la
  hora local de la persona. Sin hora de NTP no se cuenta en la gráfica (sí en los totales).
- Top 10 de bloqueados con una tabla fija chica: *decisión del diseño*, 32 casillas con el método
  Space-Saving; las cuentas se parten a la mitad cada 24 h para que muestre "lo de estos días".
- *Decisión del diseño*: las 168 horas se guardan en `/history.bin` (LittleFS) cada 60 min; al
  reiniciar se pierde como mucho 1 h. El top 10 vive solo en RAM.
- *Decisión del diseño*: "Borrar la configuración" (BOOT ≥ 10 s, §1.5) también borra
  `/history.bin` (y `/history.tmp` si quedó a medias): la historia dice cuándo hay gente en casa y
  no debe pasar al próximo dueño. La lista y las reglas propias se siguen conservando.
- *Decisión del diseño*: `GET /api/history` pide sesión (las horas dicen cuándo hay gente en casa;
  los dominios, qué se visita).
- SVG a mano en el navegador, sin librerías, dentro de la CSP actual. La gráfica por aparato con
  nombres espera a los nombres de aparatos (fase 5, §3.1-1).

### 2.5 Panel cómodo en el celular (§3.5-7)

- Letra base de 16 px o más, campos de 16 px (el iPhone no hace zoom), botones de 44 px de alto,
  modo oscuro (ya existe), tablas que se deslizan de lado en vez de salirse de la pantalla.

### 2.6 Página: guías (§2, §2.1, §2.3, §2.5)

- `site/guia-router.html`: selector "¿Qué módem tienes?" (TP-Link, Huawei, ZTE, Technicolor,
  Arris, Netgear, ASUS, MikroTik, módem del proveedor, "No sé / otra") y "¿Quién te da el
  internet?" por país. Plan A (DNS en cada aparato) para el módem que no deja cambiar el DNS;
  recordatorio de apagar el DNS por IPv6 en todas.
- *Decisión del diseño*: el país se adivina por el idioma del navegador (`es-MX` → México) y se
  puede cambiar; la detección por la conexión llega con el Worker de la fase 3.
- Regla del autor: sin fuente no se inventa. Toda ficha de marca o proveedor sin fuente muestra la
  guía general y la marca "falta verificar". No hay capturas hasta tener propias.
- `site/fortigate.html`: los 9 pasos de §2.3 tal cual (sin notas sobre otros equipos de la red).
- `site/obstaculos.html`: las tablas de §2.5 en palabras simples y la sección "Lo que un DNS no
  puede bloquear" (YouTube, Twitch, Instagram, Facebook, TikTok).

### 2.7 Pruebas de la fase 2

- Host: NXDOMAIN, clasificador de nombres especiales, historia de 168 h (vuelta de hora, huecos,
  guardar/leer), top 10, tabla de aparatos que se saltan, diagnóstico.
- Simulador: canario, nombre de prueba, Private Relay apagado/encendido (y tras "borrar"), GIF de
  prueba y su `Host`, `/api/history` con y sin sesión, `/api/diagnose` desde el mismo aparato y
  desde otro, router como DNS, historia que sobrevive al reinicio.
- Página: `node --test` del selector y de enlaces internos; sin `<script>` en línea.

## Fase 3 en detalle

Fuente: §2.2, §3.2 (ideas 8 y 10), §3.3 (página pública y panel del dueño) y §3.4. Lo que no
viene de las notas está marcado como *decisión del diseño* (el autor puede cambiarlo). Plan:
[`docs/superpowers/plans/2026-11-21-siblock-fase-3.md`](../plans/2026-11-21-siblock-fase-3.md).

### 3.1 Decisiones del autor (tal cual, de las notas)

- Reportes de módem desde la página **y** desde el panel; en el panel **apagado por defecto**, y
  se explica que es lo único, aparte de la lista, por lo que la placa habla con internet.
- Cada instalación genera un número al azar (no ligado a la persona ni a la placa) que viaja en los
  dos reportes; el Worker guarda **uno solo por número**.
- **Solo opciones fijas**, nada de texto libre; "otro / no lo encontré" no se publica, solo suma a
  un contador de guías que faltan. Una sugerencia sale solo con **3 o más** reportes iguales.
- Aviso de privacidad corto antes de enviar: qué se manda, qué no (IP, MAC completa, nombre,
  ubicación) y que se puede no mandar nada. Turnstile en la página; límite por IP sin guardarla.
- "Esto no debió bloquearse" (§3.2-8): opciones fijas, mismo cuidado de privacidad.
- Aviso de versión (§3.2-10): la placa consulta un `version.json` público; apagable.
- Gráficas públicas sin cookies (Cloudflare Web Analytics); un país solo se muestra con **5 placas
  o más**. Panel del dueño detrás de **Cloudflare Access**, solo su correo, sesión de **30 días**.
- Contáctanos (§3.4): Worker → `contacto@s-dev.win` (Email Routing reenvía; el correo personal no
  va en el repo), `Reply-To` = correo de la persona, respuesta automática, Turnstile, límite por IP
  sin guardarla, tamaño máximo, sin HTML; SPF, DKIM y DMARC. Los temas cuentan para "qué falla más".

### 3.2 Decisiones del diseño

- **Dónde vive**: Worker `siblock-api` en `cloud/` con ruta `siblock.s-dev.win/api/*` (misma
  dirección que la página: sin CORS y la CSP sigue en `'self'`); D1 `siblock`. Rutas en `/api/v1/`.
- **Versión**: `version.json` es un archivo estático de la página que escribe CI en cada tag. La
  consulta de versión **no manda nada** (ni el número de instalación). Es una opción aparte
  ("Avisarme de versiones nuevas"), apagada de fábrica.
- **Placas activas y anuncios bloqueados entre todos** se cuentan con un aviso diario
  (`POST /api/v1/placa`: número, versión, bloqueadas en 24 h) que la placa manda **solo** con
  "Compartir datos anónimos" encendido. Las notas decían contarlas por la consulta de
  `version.json`; un archivo estático no puede contar, y así la consulta de versión no deja rastro.
- **Número de instalación**: 16 bytes al azar (32 hex) en NVS `install_id`; "Borrar la
  configuración" lo cambia. En D1 se guarda `SHA-256(ID_PEPPER ‖ número)` (32 hex), no el número.
- **Sin firma desde el panel**: un secreto dentro de un firmware público no prueba nada. Los
  reportes del panel van sin Turnstile y se protegen con el límite por IP, uno por número y el
  umbral de 3. Riesgo aceptado: alguien puede ensuciar conteos; nada se ejecuta ni se publica
  texto libre.
- **Sin modelo de módem ni OUI** en esta fase: no hay lista de modelos con fuente y la tabla de
  OUI necesita fuente (fase 4). Menús fijos genéricos: `internet_wan`, `red_local`, `avanzado`,
  `otro`. Fechas guardadas solo como **día** (no hora).
- **Proveedor y país** salen de `request.cf` (`asOrganization`, `asn`, `country`). `GET
  /api/v1/pais` los devuelve a quien pregunta (para la guía del router) y no guarda nada.
- **Contáctanos desde el panel**: el panel abre `contacto.html#diag=<…>` con el diagnóstico en el
  fragmento (nunca llega a un servidor); la persona marca si lo adjunta. La placa no manda correos.
- **Correo**: un solo binding `send_email`; destino `CONTACTO_DESTINO` (var `contacto@s-dev.win`;
  si Cloudflare no acepta enviar a una dirección de Email Routing, el autor la cambia por su correo
  verificado como **secreto**, nunca en el repo). La respuesta automática es texto fijo (no repite
  nada de lo que escribió la persona) y se puede apagar (`RESPUESTA_AUTOMATICA="0"`): mandar a
  cualquier correo necesita Email Sending (plan de pago de Workers). Tope de 200 mensajes al día.
  Respuesta automática: como mucho una por dirección al día y 50 al día en total
  (`TOPE_RESPUESTAS`). Para saberlo se guarda solo SHA-256(pepper ‖ correo) en la tabla
  `respuestas_dia`, nunca el correo; el cron diario borra lo del día anterior.
- **Embudo** (§3.3): contadores por día de `entro`, `conecto`, `instalo`, `wifi`, `panel`,
  mandados por la página con `sendBeacon`, sin cookies ni número.
- **"Qué falla más"** = temas de Contáctanos + motivos de "no debió bloquearse" (la placa no manda
  su diagnóstico).
- **Panel del dueño**: `site/dueno.html` + `GET /api/v1/dueno/resumen`; Access protege
  `/dueno*` y `/api/v1/dueno/*`, y el Worker además verifica el JWT de Access (si faltan
  `ACCESS_TEAM_DOMAIN` o `ACCESS_AUD`, responde 403).
- **Mapa por país** = tabla con barras (sin librería de mapas).
- **Analítica y Turnstile en la página**: `site/js/config.js` con el token de Web Analytics y la
  clave pública de Turnstile vacíos; CI los llena desde variables del repo. Vacío = no se carga.
- **Certificados**: la placa suma GTS Root R1/R4 y SSL.com TLS 2022 (RSA/ECC) a las raíces, por
  si Cloudflare cambia de emisor (falta verificar con la placa).
- Limpieza diaria (cron) de conteos con más de 400 días.

### 3.3 Pruebas de la fase 3

- Host: número de instalación, versiones, `version.json`, cuerpos JSON, dominios, calendario de envíos.
- Simulador: con la configuración de fábrica **no sale nada** en 25 h de reloj; con las opciones
  encendidas, aviso diario y consulta de versión contra un Worker de mentira; reportes con opción
  apagada (409) y encendida; "borrar" cambia el número y apaga todo.
- Worker: `vitest` con `@cloudflare/vitest-pool-workers` y D1 local (umbrales 3 y 5, uno por
  número, Turnstile, Access, límites, correo sin HTML). Página: `node --test`.

## Fase 5 en detalle

Fuente: §3.1 (ideas 1 a 5) y §3.5 (ideas 3, 4, 5, 6 y 9). Solo lo que es software: la cajita 3D
(§3.2-12) y el video (§3.5-2) quedan pendientes en las notas. Lo que no viene de las notas está
marcado como *decisión del diseño* (el autor puede cambiarlo). Plan:
[`docs/superpowers/plans/2026-11-21-siblock-fase-5.md`](../plans/2026-11-21-siblock-fase-5.md).

### 5.1 Decisiones del autor (tal cual, de las notas)

- **Nombres a los aparatos** ("Tele sala", "Cel de mamá") en vez de IPs; se guardan en la placa.
- **Pausar solo un aparato**, no toda la casa.
- **Lista opcional de estafas y virus** (phishing y malware) además de la de anuncios; revisar que
  quepa en `MAX_DOMAINS`.
- **Horarios para los niños**: bloquear grupos de sitios (redes sociales, juegos) en ciertos
  aparatos y a ciertas horas, con la hora de NTP.
- **LED de estado** (GPIO2): fijo = todo bien; parpadeo = actualizando; apagado o parpadeo rápido =
  problema. Se explica en la guía con un dibujo.
- **Cuánto te ahorra**, estimado por consulta bloqueada.
- **Lista de anuncios en español** además de la general; revisar que quepa en `MAX_DOMAINS`.
- **Página en español e inglés.**
- **Respaldo de la configuración**: bajar y subir un archivo con reglas, nombres de aparatos y
  horarios.
- **Botón opcional de "invítame un café"**.

### 5.2 Decisiones del diseño

- **Cómo se reconoce un aparato**: por su MAC, que la placa saca de su tabla ARP (lwIP) a partir
  de la IP que pregunta. **No se guarda la MAC ni la IP**: se guarda una *huella* = primeros 8 bytes
  de `SHA-256(sal ‖ MAC)` (16 hex); la sal son 16 bytes al azar de cada placa. En pantalla el
  aparato se ve con su nombre y su IP de ese momento (solo en RAM). Si la tabla ARP no tiene la IP,
  el aparato no se reconoce todavía y no se le aplica nada (falla abierto). Falta verificar con la
  placa que la tabla ARP (10 entradas por defecto) alcanza.
- **Sin sugerencia por marca (OUI)**: la tabla de fabricantes necesita fuente (igual que en la
  fase 3).
- **Topes**: 32 aparatos con nombre (1 a 32 bytes UTF-8, sin caracteres de control) y 8 horarios.
  Todo vive en `/familia.txt` (LittleFS, texto por líneas, el mismo formato del respaldo).
  "Borrar la configuración" también borra `/familia.txt` (son datos de la familia); las reglas
  propias se quedan, como hasta ahora.
- **Pausa por aparato**: de 1 a 1440 minutos, solo en RAM (un reinicio la quita, igual que la
  pausa de toda la casa). Pausa quiere decir "sin bloqueo de anuncios"; **los horarios siguen
  valiendo** durante cualquier pausa.
- **Horarios**: cada uno tiene aparatos, grupos, días (lunes a domingo) y hora de inicio y fin en
  la hora de la casa; si el fin es antes que el inicio, cruza la medianoche (cuenta el día en que
  empieza). Grupos fijos: **Redes sociales**, **Juegos** y **Todo internet** (para la hora de
  dormir). Las listas de dominios de cada grupo son cortas y escritas a mano; falta verificar que
  cubran las apps (a veces usan otros nombres). Un horario gana sobre "Permitir". Lo bloqueado por
  horario contesta igual que un anuncio (0.0.0.0) y cuenta como bloqueado.
- **Hora de la casa**: el navegador manda su diferencia con UTC en minutos cada vez que se guardan
  los horarios; la placa la guarda (−720 a 840). Si el navegador tiene otra diferencia (cambio de
  horario de verano), el panel avisa "Guarda otra vez para corregir la hora". **Sin hora de
  internet, los horarios no bloquean** (nada que asuste) y el panel lo dice.
- **Listas**: catálogo fijo en el firmware: general (siempre), **estafas y tiendas falsas**
  (HaGeZi "Fake"; **no cubre virus**) y **anuncios en español**, las dos opcionales y **apagadas de
  fábrica**. La del plan para estafas y virus (HaGeZi TIF mini, ~245 000 dominios) se dejó porque no
  cabe en `MAX_DOMAINS` junto a la general. **Pendiente (lo decide el autor):** elegir una lista de
  virus que quepa. Orden de descarga: estafas, español, general: si se llega al tope (cuenta los
  repetidos entre listas), lo que se deja fuera es el final de la general (anuncios), nunca estafas
  ni la lista en español. `MAX_DOMAINS`
  no cambia (180 000). Al cambiar la selección se baja la lista otra vez y se permite que quede
  más chica. Las URLs y sus tamaños están marcados "falta verificar" en el código.
- **LED**: fijo = todo bien; parpadeo lento (1 s) = bajando la lista; parpadeo rápido (200 ms) =
  problema (sin WiFi de la casa, modo de configuración, sin lista o el DNS de internet no
  contesta). Mientras se aprieta BOOT manda lo que ya hacía. Sin opción para apagarlo.
- **Ahorro**: la placa suma las bloqueadas de los últimos 7 días y multiplica por **30 KB por
  consulta bloqueada** (estimado; falta verificar con fuente). El panel dice "Aproximado: en
  7 días tu casa se ahorró unos N MB de anuncios". No hay "este mes": la historia guarda 7 días.
- **Respaldo**: un archivo de texto `siblock-respaldo-AAAA-MM-DD.txt` con cabecera
  `SIBLOCK-RESPALDO 1`: reglas propias, listas elegidas, Private Relay, sal, aparatos, horarios y
  hora de la casa. **No lleva** WiFi, contraseñas, número de instalación ni las opciones de la nube
  (la placa nueva empieza con la nube apagada). Subir un respaldo **reemplaza** todo eso, después
  de que el panel muestre un resumen y la persona diga "Sí". Se sube en pedazos de hasta 3 KB (el
  servidor web no acepta cuerpos de más de 4096 bytes) a un archivo temporal, se revisa entero y se
  aplica todo o nada; tope de 24 KB.
- **Página en inglés**: `site/en/` con la página de inicio (instalar y qué comprar) y privacidad;
  todas las páginas enlazan al otro idioma. Las guías siguen solo en español y la versión en inglés
  lo dice. El panel de la placa sigue solo en español.
- **Botón de apoyo**: solo en la página, en el pie; aparece si CI pone un enlace `https://` en
  `js/config.js` (variable `SIBLOCK_APOYO_URL` del repo). Vacío = no hay botón. Sin ligas de
  afiliado ni contadores.

### 5.3 Pruebas de la fase 5

- Host: huella, nombres, horarios (días, medianoche, zona), grupos, pausas, catálogo de listas,
  LED, ahorro, formato del respaldo (lectura con basura, topes, todo o nada).
- Simulador: nombrar y pausar un aparato con IPs `127.0.0.x`; horario activo e inactivo; listas
  elegidas que cambian el orden de descarga; respaldo bajado y vuelto a subir en otra placa;
  "borrar" quita `/familia.txt`; el LED en la salida `[sim] led`.
- Página: `node --test` (enlaces entre idiomas, textos en los dos idiomas, botón de apoyo oculto
  sin enlace). CI: `test_make_site_files.py` con el enlace de apoyo.

## Lo que se deja a quien construye

- Librería Improv propia o de referencia; estructura interna de `settings`.
- Diseño visual de la página y del portal (sobrio, legible en el celular, modo oscuro).
- Número de iteraciones de PBKDF2 (medido), detalles del portal cautivo.
- Cómo partir la fase 1 en tareas y en qué orden, respetando: firmware un agente a la vez.

## Agentes y skills sugeridos para la fase 1

| Frente | Skill o agente |
|---|---|
| Firmware (1.1–1.6) | `esp32`, `superpowers:test-driven-development`, revisión `ecc:cpp-reviewer` |
| CI (1.7) | `ecc:github-ops` |
| Página (1.8) | `frontend-design`, `break-ui`, `web-design-guidelines`, `cloudflare:*` para Pages |
| Seguridad (1.3, 1.4, 1.5, 1.9) | `security-suite` |
| Documentación | `engineering:documentation` (actualizar `docs/` y README) |

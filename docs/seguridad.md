# Seguridad

## Modelo de amenazas

| Quién | Qué puede hacer | Qué se protege |
|---|---|---|
| Una **página de internet** abierta en un navegador de la casa | Mandar peticiones al panel (CSRF) y reapuntar su dominio a la IP de SiBlock (DNS rebinding) | El historial DNS, las reglas y la pausa |
| Un **aparato de la red** (IoT, invitado) | Probar contraseñas, mandar peticiones enormes o lentas, mandar paquetes DNS armados a mano, **crear la contraseña del panel antes que el dueño**, reapuntar el WiFi de la placa | La contraseña, la disponibilidad del DNS y del panel, la caché, la configuración |
| Un **vecino al alcance del WiFi** | Conectarse al AP del modo de configuración y darle a la placa su propio WiFi (y así ver el DNS de la casa) | El WiFi guardado |
| **Internet** (respuestas DNS falsas, listas manipuladas) | Envenenar la caché o meter basura en la lista | Lo que resuelven los aparatos |
| Quien **espía la WiFi** | Leer el tráfico HTTP del panel | Riesgo aceptado (ver abajo) |
| Quien tiene la placa **en la mano** (USB y botón) | Borrar la configuración, leer la contraseña de respaldo por USB, leer la flash | Fuera de alcance: ya tiene el poder del botón BOOT |
| *(Fase 3)* **Cualquiera en internet** contra el Worker `siblock-api` | Mandar reportes falsos o en masa, llenar D1, usar Contáctanos para mandar correo a otra persona (spam) o meter cabeceras de correo, leer el resumen del dueño, sacar IPs de la base | Las estadísticas públicas, la guía que arman los reportes, la reputación de `s-dev.win`, la privacidad de quien reporta |
| *(Fase 3)* Un **aparato de la casa** contra las rutas nuevas del panel | Encender "Compartir" sin permiso, mandar reportes con el número de la casa | Que la placa hable con internet sin que el dueño quiera |

Fuera de alcance: alguien con acceso físico a la placa (puede leer la flash) y el router.

## Defensas, una por una

| Defensa | Dónde | Contra qué |
|---|---|---|
| Solo contesta DNS a la subred local | `DnsServer::isLocalClient` | Usarlo como DNS abierto o para amplificar ataques |
| Parser acotado; etiquetas sin `.` y solo con bytes 0x21–0x7e | `adblock::parseQuestion` | Desbordes y envenenamiento de caché con nombres tramposos |
| ID al azar y 4 sockets; la respuesta tiene que coincidir en IP, puerto, ID, socket, nombre y tipo | `dns_server.cpp` | Respuestas falsas desde internet |
| Anti-rebinding: REFUSED si internet responde con una IP privada | `guardRebind`, `REBIND_PROTECTION` | Que una página llegue al panel o al router |
| Chequeo del Host (IP, `siblock`, `siblock.local`, `adblock`, `adblock.local`, `PANEL_EXTRA_HOST`) | `web.cpp: hostOk` | DNS rebinding contra el panel |
| `X-Requested-With: adblock` obligatorio en lo privado y en `/api/setup` y `/api/wifi*` | `authorize` | CSRF (un `<form>` o `<img>` no puede ponerlo) |
| Token de sesión de 128 bits, 12 h, comparado en tiempo constante | `newToken`, `sameToken` | Que la contraseña quede guardada en el navegador |
| Sin el diálogo Basic del navegador (401 sin `WWW-Authenticate`) | `authorize` | Que el navegador mande la contraseña solo |
| Bloqueo por IP: 5 fallos → 30 s, doblando hasta 16 min. Si hay 16 IPs bloqueadas, un turno compartido de 5 s; las IPs que entraron bien no esperan (ese pase dura 1 h y se pierde con la primera contraseña mala) | `authorize`, `lockSlotFor` | Adivinar la contraseña, que una IP deje fuera a otra y que cambiar de IP sirva para probar en masa |
| Contraseña de 12 a 64 bytes; `CAMBIA_ESTA_CLAVE` no vale | `siblock::validPanelPassword` | Contraseñas débiles o la de ejemplo |
| Contraseña del panel guardada solo como PBKDF2-HMAC-SHA256 (ver abajo) | `settings.cpp`, `lib/siblock_core/siblock_crypto.*` | Que leer la NVS dé la contraseña |
| Cambiar la contraseña escribe la nueva aparte y solo después reemplaza a la vieja | `settings.cpp` (`commitNewSet`) | Que un corte de luz a mitad deje el panel sin contraseña y otro la cree con `/api/setup` |
| Ventana de creación de 30 min + BOOT (ver abajo) | `SetupWindow` en `lib/siblock_core/modes.*`, `/api/setup` | Que otro aparato cree la contraseña antes que el dueño |
| AP con WPA2 y `ap_pass` al azar, en una red que no choca con la de la casa; `/api/wifi` solo desde la red del AP (ver abajo) | `portal.cpp`, `web.cpp`, `pickApAddress` | Que un vecino o un aparato de la casa reapunte el WiFi |
| Cuerpo de 512 B en modo raw → 413 | `onBodyChunk` | Agotar la RAM |
| WebServer parcheado: líneas ≤ 1024 B, cuerpo ≤ 4 KB, sin multipart, 8 s por petición, Host limpio en cada petición | `lib/WebServer/src/Parsing.cpp` | Agotar la RAM y trabar el panel con un cliente lento |
| Todo lo que viene de la red se pinta con `textContent`; el JSON va escapado; CSP `default-src 'none'` | `web_page.h`, `portal_page.h`, `jsonEscape` | XSS con nombres de dominio o de redes WiFi maliciosos |
| `/api/stats` público solo trae contadores y el estado | `handleStats` | Que cualquiera vea el historial |
| Descargas por HTTPS con CAs fijas; URLs fijas en `config.h` | `certs.h`, `blocklist.cpp` | Listas manipuladas y SSRF |
| Lista y reglas se cambian con `rename` atómico; la lista se valida al cargar | `replaceFile`, `loadFromFs` | Corrupción por cortes de luz |
| Vigilante de 30 s en la tarea DNS | `udpTask` | Que la casa se quede sin DNS para siempre |
| *(Fase 2)* Excepción de `Host` solo para `/siblock-prueba.gif` y solo con `<etiquetas>.prueba.siblock.internal[:puerto]` | `siblock::isProbeHost`, `web.cpp` | Que el `Host` de prueba sirva para llegar a otra ruta (rebinding) |
| *(Fase 2)* `/api/diagnose` pública con anti-CSRF y solo sobre el aparato que pregunta | `web.cpp`, `siblock::diagnose` | Que alguien de la casa vea qué hace otro aparato |
| *(Fase 2)* `/api/history` y `/api/options` con sesión; `relayBlock` solo `0` o `1` | `web.cpp` | Ver cuándo hay gente en casa o qué se visita; cambiar la opción sin permiso |
| *(Fase 2)* Aparatos que se saltan, top 10 y "últimas consultas" solo en RAM; dominios del top cortados a 63 B y escapados | `BypassTable`, `TopBlocked`, `appendJsonString` | Guardar IPs en la flash; XSS o JSON roto con nombres raros |
| *(Fase 3)* Todo lo que la placa manda a internet (aparte de la lista) está **apagado de fábrica**; solo el valor `1` en la NVS lo enciende | `settings::cloudShare`, `settings::versionCheck`, `watchCloud` | Que la placa hable con internet sin permiso (NVS vacía, rota o con `2`) |
| *(Fase 3)* `/api/cloud` y `/api/report/*` con sesión y anti-CSRF; solo `0`/`1` y valores de listas fijas; cuerpo armado en la placa | `web.cpp`, `siblock::modemReportJson`, `blockedReportJson` | Encender la nube o mandar texto libre desde otro aparato o desde una página |
| *(Fase 3)* HTTPS a `siblock.s-dev.win` con raíces fijas, sin redirecciones, 8 s, ≤ 1024 B al leer `version.json` | `src/cloud.cpp`, `certs.h` | Que alguien en el camino cambie la respuesta o trabe la placa |
| *(Fase 3)* Worker: topes de cuerpo, listas fijas, Turnstile en la página, límite por IP sin guardarla, número pasado por SHA-256 con pimienta, umbrales de 3 y 5 | `cloud/src/*.ts` | Spam, basura en D1, reconocer una casa en las estadísticas |
| *(Fase 3)* Contáctanos: asunto fijo, correo validado sin saltos, texto sin HTML, respuesta automática fija, 200 al día | `cloud/src/contacto.ts`, `correo.ts` | Usar el formulario para mandar spam o meter cabeceras de correo |
| *(Fase 3)* Resumen del dueño: Cloudflare Access **y** revisión del JWT en el Worker | `cloud/src/access.ts` | Que alguien lea el resumen si Access quedó mal configurado |


## Amenazas nuevas de la fase 1 y cómo se cubren

### Crear la contraseña antes que el dueño

Una placa recién instalada no tiene contraseña. Sin más, cualquier aparato de la red podría
llamar a `POST /api/setup` primero y quedarse con el panel.

- `/api/setup` solo funciona durante una **ventana de 30 min**: desde el arranque, o desde una
  pulsación corta (< 2 s) del botón BOOT. Fuera de ella responde 403 "Pulsa el botón BOOT de tu
  SiBlock una vez y vuelve a intentar". Abrir la ventana pide tocar la placa.
- Si ya hay contraseña responde 409: nadie la pisa por esa ruta.
- Pide `Host` válido y `X-Requested-With`, igual que lo privado (contra CSRF y rebinding).
- Mientras no hay contraseña, toda ruta privada responde 403: no hay "modo abierto".
- Riesgo que queda (aceptado en el diseño, 1.4): la ventana se abre **sola en cada arranque**
  mientras no hay contraseña, para que el primer uso no pida tocar el botón. `/api/setup` no tiene
  tope de intentos propio (solo hace falta uno). Un aparato de la casa que esté mirando justo en
  esos 30 min puede ganar la carrera y quedarse con el panel. El dueño lo nota porque su
  contraseña no entra, y lo arregla con BOOT ≥ 10 s. Una vez creada, la ventana ya no sirve de
  nada (409).
- Los tiempos usan restas sin signo, así que la vuelta de `millis()` a los 49 días no abre la
  ventana por error (probado en `test_siblock`).

### Vecino y modo de configuración por WiFi

Cuando no hay WiFi que funcione, SiBlock abre la red `SiBlock-XXXX`. Si fuera abierta, un vecino
podría conectarse, darle su propio WiFi y ver todo el DNS de la casa.

- La red es **WPA2**. Su contraseña (`ap_pass`) son 10 caracteres al azar (alfabeto de 31, unos
  49 bits), se genera la primera vez con `esp_random` y queda en la NVS. No sale por la red de la
  casa sin sesión: solo por USB (Improv `0xA0`), por la serie al arrancar y en el panel con sesión
  (`/api/backup-ap`). El mensaje `[portal] abierto: …` no la imprime.
- `POST /api/wifi` responde **403** si la petición no viene de la red del AP: un aparato de la
  casa no puede reapuntar la placa aunque el portal esté abierto. "Viene de la red del AP" es:
  la conexión llegó a la IP del AP **y** el origen está en su /24. Para que eso no se pueda
  confundir, el AP nunca comparte rango con la casa: es 192.168.4.1, u otra /24 libre si la casa
  también usa 192.168.4.x (`pickApAddress`). Si aun así llegan a chocar (la placa se conectó
  después a otra red), no se confía en nadie: `/api/wifi` da 403 y el DNS no hace de portal.
- `/api/wifi*` no existen fuera del portal (404).
- El portal se cierra solo a los 15 min si nadie se conecta.

### USB físico (Improv)

Por USB se puede dar un WiFi nuevo y leer la contraseña de respaldo (`0xA0`) **sin contraseña**.
Es a propósito: quien tiene la placa conectada ya puede borrarlo todo con BOOT ≥ 10 s o
reinstalar el firmware. Improv no da más poder que el botón.

- El WiFi nuevo solo se guarda si conecta de verdad; si no, se vuelve al guardado.
- El parser no se traba con basura ni con paquetes cortados (probado en `test_siblock`).

### Hash de la contraseña del panel

- PBKDF2-HMAC-SHA256 con **sal de 16 B** al azar (`esp_random`) por contraseña.
- Las **iteraciones se guardan junto al hash** (`panel_iter`). Hoy son 10 000, a medir en la
  placa (meta: 0,3–0,5 s); si se suben más adelante, los hashes viejos siguen sirviendo.
- La comparación del hash es en tiempo constante (`equal32`).
- En la placa se usa mbedtls; en la PC y el simulador, una copia portátil probada con los vectores
  del RFC 7914. Al arrancar se corre una autoprueba con un vector conocido (`[clave] autoprueba ok`).
- La contraseña nunca se escribe en la NVS ni en la serie (`test_sim.py` comprueba que el texto no
  aparece en `nvs.json`).

### Corte de luz a mitad de guardar

Al crear o cambiar la contraseña, la vieja no se toca hasta que la nueva está completa:

1. Se escribe un juego aparte, `panel_nsalt` y `panel_niter`, y **al final** `panel_nhash`.
2. Un juego sin hash no vale. Si la luz se corta antes de escribirlo, ese juego se tira y la
   contraseña de antes sigue funcionando (o sigue "sin contraseña" si era la primera).
3. Con el juego nuevo completo, se copia a `panel_salt`, `panel_iter` y `panel_hash` (el hash
   otra vez al final) y luego se borran las claves `panel_n*`.
4. Mientras existe un juego nuevo completo, es el que vale. Si la copia quedó sin terminar, se
   termina al arrancar (`settings::begin()`) o antes del próximo cambio.

Así nunca queda una contraseña a medias que nadie conoce.

### Borrar la configuración

BOOT ≥ 10 s borra el espacio NVS `siblock` (WiFi, contraseña del panel, contraseña del AP) y,
desde la fase 2, la historia de las gráficas (`/history.bin` y `/history.tmp`). Desde la fase 3
también apaga "Compartir datos anónimos" y "Avisarme de versiones nuevas" y cambia el número de
instalación (están en el mismo espacio NVS). No borra la lista ni las reglas propias. Pide tener
la placa en la mano.

## Amenazas nuevas de la fase 2 y cómo se cubren

Diseño: 2.1 a 2.4 de [`superpowers/specs/2026-11-21-siblock-design.md`](superpowers/specs/2026-11-21-siblock-design.md).
Las pruebas de cada punto están en `sim/test_sim.py` (5b y 5c) y en `test_host`.

### La excepción de `Host` de la imagen de prueba

El chequeo de `Host` es lo que frena el DNS rebinding. La comprobación "¿este aparato usa
SiBlock?" necesita que el navegador cargue algo desde `<n>.prueba.siblock.internal`, que no es un
`Host` permitido. Por qué abrir esa puerta es seguro:

- Vale **solo** para `GET /siblock-prueba.gif`. Cualquier otra ruta con un `Host` de prueba sigue
  dando 403 (lo prueba el simulador con `/api/stats`).
- Esa ruta devuelve siempre los **mismos 43 bytes** (un GIF de 1×1), sin cookies, sin datos de la
  placa ni de la casa, con `no-store`. Leerla no enseña nada más allá de "llegué a una SiBlock".
- El `Host` se valida entero, sin `std::regex`: etiquetas `[a-z0-9-]` de 1 a 63, terminado en
  `.prueba.siblock.internal`, puerto solo con dígitos. `prueba.siblock.internal.evil.com`,
  `xprueba.siblock.internal`, punto final, `_` y etiquetas vacías no valen (`test_host`).
- El nombre solo lo resuelve SiBlock y solo para la red local; `.internal` no existe en internet,
  así que una página de afuera no puede reapuntar ese nombre a otro lado.
- La CSP del panel suma únicamente `img-src *.prueba.siblock.internal:*`: imágenes, nada de
  scripts ni conexiones.

### `/api/diagnose` es pública

Para que cualquiera de la casa use "Algo no funciona" sin la contraseña.

- Pide `Host` válido y `X-Requested-With: adblock` (contra CSRF y rebinding: una página de internet
  no puede leer la respuesta).
- Solo mira la IP **de quien pregunta**: si preguntó algo en los últimos 10 min y si usa DNS
  cifrado o Private Relay. El resto es estado general que ya da `/api/stats` (pausa, lista,
  internet, router que hace de DNS).
- La respuesta son **códigos fijos y niveles**, sin dominios, IPs ni contadores por aparato (el
  simulador comprueba que ningún valor trae un punto).
- Lo que se puede deducir: alguien de la casa puede saber si **su propio** aparato usa DNS cifrado,
  y si en la casa hay un router que hace de DNS. Nada de otros aparatos.
- La IP de origen se puede falsificar en la LAN (ver "Riesgos aceptados"). Quien lo haga solo
  obtiene los mismos códigos sobre otra IP: si ese aparato preguntó en 10 min y si usa DNS
  cifrado o Private Relay. Se acepta: es poco y no dice qué se visitó.

### `/api/history` y `/api/options` piden sesión

- Las horas de uso dicen cuándo hay gente en casa y el top 10 dice qué se visita: por eso
  `/api/history` es privada (🔒), igual que `/api/detail`.
- `POST /api/options` solo acepta `relayBlock` = `0` o `1`; otro valor o nada → 400 sin tocar la
  NVS. Sin sesión → 401; sin anti-CSRF → 403. Cuerpo con el tope de 512 B de siempre.
- La opción de Private Relay vive en la NVS y "borrar la configuración" la apaga.

### Datos de los aparatos

- La tabla de aparatos que se saltan (16, se olvidan a las 24 h), el top 10 y las últimas consultas
  viven **solo en RAM**. Nada de eso se escribe en la flash ni sale de la placa.
- `/history.bin` solo guarda números por hora (consultas y bloqueadas de toda la casa), sin IPs ni
  dominios. Si el archivo está roto (largo o cabecera `SBH1` que no cuadran, última hora fuera de
  2023-2100, casillas con datos sin última hora o más bloqueadas que consultas), se ignora.
  Como dice a qué horas hay gente en casa, "borrar la configuración" (BOOT 10 s) también lo borra
  (y `/history.tmp`): quien regala o vende la placa no deja ese rastro.
- Los dominios raros (253 caracteres, bytes UTF-8 inválidos, comillas) se cortan a 63 bytes y salen
  escapados en JSON; el panel los pinta con `textContent` (sin `innerHTML`).
- Nada nuevo sale de la placa a internet en esta fase.

## Amenazas nuevas de la fase 3 y cómo se cubren

Diseño: "Fase 3 en detalle" de [`superpowers/specs/2026-11-21-siblock-design.md`](superpowers/specs/2026-11-21-siblock-design.md);
plan: [`superpowers/plans/2026-11-21-siblock-fase-3.md`](superpowers/plans/2026-11-21-siblock-fase-3.md).
Pruebas: `sim/test_sim.py` (18 a 18f), `test_host` y `cd cloud && npm test`. Rutas en
[api.md](api.md#worker-siblock-api-fase-3).

### La placa habla con internet solo si el dueño quiere

Es lo que más puede morder: SiBlock ve todo lo que se visita en la casa.

- Fuera de bajar la lista y pedir la hora, la placa solo sale a internet con **"Compartir datos
  anónimos"** (aviso diario y reportes), **"Avisarme de versiones nuevas"** (consulta de
  `version.json`) o **"Usar los permitidos de la comunidad"** (consulta de `/api/v1/permitidos`).
  Las tres están **apagadas de fábrica**.
- Solo el valor `1` guardado en la NVS las enciende. Si la clave falta, vale `2` o la NVS no se
  puede leer → apagada. El simulador lo prueba: 25 h de reloj con la NVS vacía, y otras 25 h con
  `cloud_share=2` y `ver_check=2` escritos a mano, sin **ninguna** petición al Worker de mentira.
- Encenderlas pide sesión y anti-CSRF (`POST /api/cloud`); un valor que no es `0` o `1` → 400 sin
  cambiar nada.
- Qué sale, exacto: el aviso diario es `{"i","v","b"}` (número, versión, bloqueadas en 24 h); la
  consulta de versión es un `GET` **sin cuerpo ni número**, a un archivo fijo. Los reportes del
  panel llevan solo valores de listas fijas (y el dominio reportado, validado). Nunca la IP de un
  aparato, la MAC, nombres ni qué se visita.
- "Borrar la configuración" apaga las tres, borra `/comunidad.bin` y cambia el número (prueba 18d).

### Permitidos de la comunidad

Una lista que llega de afuera y **deja pasar** dominios: si alguien la controlara, podría hacer que
todas las casas que la usan vuelvan a ver un sitio de anuncios o de estafas. Cómo se cubre:

- **Qué sale de la placa: nada.** Es un `GET https://siblock.s-dev.win/api/v1/permitidos` sin cuerpo,
  sin número de instalación y sin cookies, igual para todas las placas (prueba 18f). El Worker no
  guarda nada de quien la pide y la sirve desde la caché del borde.
- **Revisión del dueño.** Un reporte «No debió bloquearse» nunca entra solo a la lista: el dueño lo
  aprueba a mano en `dueno.html`, detrás de Cloudflare Access **y** del JWT revisado en el Worker
  (`POST /api/v1/dueno/permitidos` y `/quitar`; 403 sin JWT válido, 403 con `Origin` de otra página).
  Muchos reportes falsos de un sitio de anuncios solo lo suben en la lista para revisar.
- **Topes y validación, todo o nada.** El Worker acepta solo dominios con la misma regla que los
  reportes (`esDominio`) y como mucho 500 (409 `tope`). La placa vuelve a revisar todo: cuerpo de
  32 KB como mucho, leído de a pedazos (nunca entero en la RAM), JSON estricto, versión entera,
  cada dominio con `validReportDomain` y 500 como mucho. Si algo no cuadra se rechaza la lista
  entera y se queda la anterior (pruebas en `test_host` y 18f).
- **Lo tuyo gana.** Tus reglas «Bloquear» se revisan antes que la comunidad (también si bloqueas
  el dominio de arriba). La comunidad **no** salta la protección anti-rebinding: eso solo lo hace tu
  «Permitir». En las consultas recientes sale como `comunidad`, nunca como `permitido` (lo tuyo).
- **Aparte de tus reglas.** En la placa solo hay huellas de 64 bits (FNV-1a) en RAM y en
  `/comunidad.bin` (≤ 4016 B, guardado atómico); no salen en «Mis reglas» ni en el respaldo. Apagar
  la opción o borrar la configuración la borra.
- Riesgo que queda: si alguien entra a la cuenta de Cloudflare del dueño o a su correo (Access),
  puede aprobar sitios. Lo limita el tope de 500 y que cada casa decide si usa la lista.

### Número de instalación

- 16 bytes de `esp_random` (32 hex), en la NVS `install_id`. No sale de una cuenta ni de la MAC: no
  está ligado a la persona ni a la placa. "Borrar la configuración" lo cambia.
- Solo sale con "Compartir" encendido, y en el enlace `reporte.html#i=…` (en el fragmento, que el
  navegador no manda al servidor de la página).
- El Worker guarda `SHA-256(ID_PEPPER ‖ número)` (32 hex), nunca el número. Sin la pimienta
  (`ID_PEPPER`, secreto de Cloudflare, 32 bytes al azar) no se puede ir de lo guardado al número.
  Si la pimienta falta o es corta, el Worker rechaza todo POST (500) en vez de guardar sin ella.
  Si se pierde o se cambia, el "uno por instalación" empieza de cero (no se rompe nada más).

### Reportes del panel sin firma

Las notas (§2.2) pedían "firma del reporte con el número de instalación" desde el panel. Se
descartó (decisión del diseño, 3.2): el firmware es público, así que **cualquier secreto dentro de
él lo conoce cualquiera**; una firma así no prueba que el reporte venga de una SiBlock. Y el número
de instalación lo inventa la propia placa, así que tampoco prueba nada.

Lo que sí frena los reportes falsos:

- **Límite por IP** en el Worker (5 por minuto por ruta), sin guardar la IP. En IPv6 cuenta el
  **/64** entero (una casa recibe uno y puede cambiar de dirección en cada petición).
- **Uno por número**: un segundo reporte de módem con el mismo número reemplaza al anterior; un
  bloqueo cuenta uno por número y dominio.
- **Umbrales**: un grupo de módem sale en las estadísticas solo con 3 reportes iguales; un país,
  con 5 placas. `menu=otro` y `fabricante=no-se` nunca se publican.
- **Solo opciones fijas**: no hay texto libre que publicar ni que pueda llevar HTML o enlaces.
- **Tope diario de bloqueadas**: el aviso de la placa acepta `b` hasta 100 000 000, pero guarda
  como mucho 1 000 000 por día y número; un número inventado no puede inflar solo la gráfica pública.
- **Estadísticas en la caché del borde**: `GET /api/v1/estadisticas` se guarda 5 min en
  `caches.default` con una llave fija (cambiar `?x=` no la salta), así un bucle de pedidos no
  gasta las lecturas diarias de D1 ni tumba el Worker.

`origen` lo escribe quien manda el reporte: poner `"panel"` se salta Turnstile. Es parte de la
misma decisión (el panel no puede resolver Turnstile y una firma no probaría nada), así que esa
ruta queda con las mismas defensas de arriba.

Riesgo aceptado: alguien con muchas IPs y números inventados puede ensuciar los conteos. Lo peor
que logra es una sugerencia equivocada en la tabla de módems; nada se ejecuta, no hay texto suyo
en la página y el dueño ve los dominios reportados antes de tocar la lista.

La página publicada manda `Strict-Transport-Security` (1 año, con subdominios) y cierra cámara,
micrófono, ubicación y pagos en `Permissions-Policy`. El instalador (ESP Web Tools 10.4.0,
licencia Apache-2.0) va copiado en `site/vendor/esp-web-tools/10.4.0/`: se bajó de npm, se comparó
archivo por archivo con unpkg y el de entrada coincide con su SRI. Una prueba revisa cada archivo
contra `SHA256SUMS`, así que la página ya no depende de ningún sitio externo para instalar y la CSP
no deja cargar scripts de unpkg.

En la página, los reportes llevan además **Turnstile** (validado en el Worker con el `hostname`
de la página; un token de otro sitio no vale).

### Respuesta automática usada para mandar spam

Contáctanos manda una respuesta automática **al correo que escribe la persona**. Un atacante podría
poner el correo de otra persona para que SiBlock le mande correos.

- La respuesta es **texto fijo** (enlaces a las guías). No lleva el nombre, el mensaje ni nada que
  escribió quien llenó el formulario: no sirve para mandar un mensaje propio a un tercero.
- Solo sale después de **Turnstile**, del **límite por IP** (2 por minuto; en IPv6, por /64) y por
  debajo del **tope de 200 mensajes al día** de todo el Worker. El lugar del tope se reserva en un
  solo paso (`INSERT … ON CONFLICT … WHERE n < 200 RETURNING n`) antes de mandar: dos pedidos a la
  vez no lo pasan.
- **Una respuesta por dirección y día**, y un **tope propio de 50 respuestas al día**
  (`TOPE_RESPUESTAS`, tabla `respuestas_dia`). Llegado el tope, el mensaje al dueño igual sale; solo
  se deja de contestar solo. La tabla guarda `SHA-256(ID_PEPPER ‖ correo)` (32 hex), nunca el correo,
  y el cron la vacía cada día.
- Se puede apagar (`RESPUESTA_AUTOMATICA="0"`); hace falta si la cuenta no tiene Email Sending.

### Cabeceras de correo inyectadas y HTML

- `correo` con `\r\n` (`<correo>\r\nBcc: <otro correo>`), espacios, `<>",;()[]\` o más de un `@` → 400 sin
  mandar nada. Se usa solo como `Reply-To` y como destino de la respuesta automática.
- El asunto es **fijo** (`[SiBlock] ` + nombre del tema de la lista). `nombre` no puede llevar saltos.
- El texto al dueño va **sin HTML**: se quitan las etiquetas, los caracteres de control y los `\r`.
- El mensaje no se guarda en D1 (solo se cuentan el día y el tema).

### Cloudflare Access y el JWT

- Access protege `siblock.s-dev.win/dueno*` y `/api/v1/dueno/*`: solo el correo del dueño (código
  por correo), sesión de 30 días.
- Por si Access queda mal puesto (otra ruta, política abierta), el Worker **también** revisa el
  JWT: RS256 (rechaza `alg:none` y otros), llave del equipo por `kid`, `aud` = `ACCESS_AUD`,
  `iss` = `ACCESS_TEAM_DOMAIN`, `exp` y `nbf` con 60 s de holgura. Si faltan las variables → 403
  siempre (falla cerrado).
- El resumen solo trae conteos, los dominios reportados (y si ya están aprobados) y la lista de
  permitidos; nada de IPs ni números.
- Aprobar y quitar permitidos son POST: además de Access y el JWT, piden `Origin` de la página (o
  ninguno), así otra página no puede usar la sesión del dueño.

### Límites sin guardar la IP

- La clave del límite es `SHA-256(ID_PEPPER ‖ IP)` cortado a 32 hex, con la ruta delante. Vive
  solo en el servicio de límites de Cloudflare (ventana de 60 s). Ninguna tabla de D1 tiene una
  columna de IP (lo prueba `cloud/test/reportes.test.ts` con `PRAGMA table_info`).
- Si el servicio de límites falla, se rechaza (429), no se deja pasar.
- Cloudflare sí ve la IP de cada petición (como cualquier servidor). El aviso de privacidad de la
  página lo dice.

### Otras rutas del Worker

- `Origin` distinto de la página en un POST → 403: otra página no puede usar el navegador de la
  persona para mandar reportes. Sin `Origin` (la placa) pasa; lo que protege es lo de arriba.
- Topes de cuerpo por ruta (128 B a 8 KB), leídos sin confiar en `Content-Length`.
- Errores con un código corto, sin detalles internos.
- Fechas solo como día; los conteos se borran a los 400 días.
- La página pinta todo lo que viene del Worker (dominios, proveedores) con `textContent`, y el
  `#diag=` y el `#i=` del fragmento se validan y se ignoran si están rotos.

### Lo que el atacante de la red local puede hacer con las rutas nuevas del panel

- Sin sesión: nada (401). Sin anti-CSRF: nada (403). Una página de internet no puede leer ni
  mandar (Host + anti-CSRF, igual que el resto del panel).
- Con la contraseña del panel: encender "Compartir" y mandar reportes con el número de la casa.
  Es el mismo poder que ya tiene sobre las reglas y la pausa.
- `GET /api/cloud` da el número de instalación solo con sesión.
- Un reporte a la vez (429 `ocupado`): no se puede usar la placa para mandar muchos seguidos (y el
  Worker tiene su propio límite).

## Amenazas nuevas de la fase 5 y cómo se cubren

Diseño: "Fase 5 en detalle" (5.2) de [`superpowers/specs/2026-11-21-siblock-design.md`](superpowers/specs/2026-11-21-siblock-design.md);
plan: [`superpowers/plans/2026-11-21-siblock-fase-5.md`](superpowers/plans/2026-11-21-siblock-fase-5.md).
Pruebas: `sim/test_sim.py` (19 a 23) y `test_host` (`testFamily`, `testBackup`). Rutas en
[api.md](api.md#aparatos-fase-5).

### Aparatos con nombre y su huella

- La placa saca la MAC de su tabla ARP a partir de la IP que pregunta, pero **no guarda ni la MAC
  ni la IP**. Guarda una huella: los primeros 8 bytes de `SHA-256(sal ‖ MAC)` (16 hex). La sal son
  16 bytes de `esp_random` propios de cada placa, guardados en `/familia.txt`.
- Con la huella sola no se saca la MAC sin la sal. **Con la sal** (está en `/familia.txt` y en el
  respaldo) se podría probar MAC por MAC: el espacio de MACs es chico. Por eso el respaldo se trata
  como un dato de la casa (abajo). Lo que se gana: ninguna MAC queda escrita tal cual en la flash
  ni en un archivo que se comparte, y la misma MAC da otra huella en otra placa.
- La IP de ahora, la caché de 16 IPs → huella (5 min) y las pausas por aparato viven **solo en
  RAM**.
- `GET /api/devices` pide sesión: los nombres ("Cel de mamá") y cuánto pregunta cada aparato dicen
  quién vive en la casa y cuándo usa internet.
- Un nombre se guarda tal cual si es UTF-8 válido de 1 a 32 bytes, sin caracteres de control ni
  espacios en las puntas. Sale escapado en JSON y el panel lo pinta con `textContent`: un nombre
  como `<img src=x onerror=…>` se ve como texto (prueba de la sección 20).
- `action=name` solo acepta huellas que la placa ya vio (en su tabla o en las IPs de los últimos
  5 min): no se pueden llenar las 32 casillas con aparatos inventados.

### Lo que el atacante de la red local puede hacer con los aparatos y horarios

- Sin la contraseña del panel: nada (401/403, igual que el resto del panel). Una página de
  internet no puede leer ni mandar (Host + anti-CSRF).
- **Falsificar la IP de origen** (riesgo aceptado de siempre): un aparato de la casa que se haga
  pasar por la IP de otro recibe la decisión de **ese** otro (su horario o su pausa) solo para sus
  propias consultas. Para "robar" una pausa además tendría que tener la misma MAC en la tabla ARP.
  No da acceso al panel ni a datos.
- **Cambiar su MAC** (o la "dirección privada" de los celulares): el aparato sale como uno nuevo
  sin nombre y **se libra de su horario**. Un horario es una ayuda para la familia, no un candado:
  quien sabe cambiar su MAC, poner DNS cifrado o una VPN se lo salta igual. Lo dicen el panel y
  [operacion.md](operacion.md#horarios-para-los-niños-fase-5).
- **IP que pasa a otro aparato** (riesgo aceptado): la placa recuerda 5 min qué huella tenía cada
  IP (16 casillas, `kCacheMs` en `family_store.cpp`). Si el router le da esa IP a otro aparato en
  ese rato, el nuevo hereda unos minutos el horario, la pausa o el nombre del anterior; luego se
  vuelve a mirar la tabla ARP y se corrige solo. No da acceso al panel ni a datos.
- **Sin la tabla ARP** (aparato que no está, red llena, mesh que hace de DNS): no se aplica nada.
  Falla abierto a propósito: la casa nunca se queda sin internet por un horario.
- **Reloj**: sin hora de internet creíble (`epoch` < 1 700 000 000) los horarios no bloquean. Un
  NTP falso en la LAN podría mover la hora y prender o apagar un horario; no da más que eso.
- Alguien de la casa **con la contraseña del panel** ve los nombres y puede quitar los horarios. Es
  el mismo poder que ya tiene sobre las reglas y la pausa: la contraseña es de los adultos.

### Respaldo: subirlo y bajarlo

- `GET /api/backup` pide sesión y anti-CSRF y sale con `no-store`. **No lleva** WiFi, contraseñas,
  hash de la contraseña, contraseña de respaldo, número de instalación, opciones de la nube, IPs
  ni MACs escritas tal cual (lo comprueba la sección 23 del simulador). **Sí lleva** los nombres de
  los aparatos, los horarios, las reglas propias, **la sal y la huella de cada aparato**. Con la sal
  y la huella juntas se puede sacar la MAC probando una por una (2^48 posibilidades, muchas menos si
  se sabe la marca del aparato): **el archivo es sensible**. El panel lo dice al lado del botón y al
  bajarlo ("es un archivo privado: no lo compartas"). Riesgo aceptado (ver abajo): la huella evita
  dejar la MAC escrita, no la esconde de quien tiene el respaldo.
- Si el respaldo armado pasa de 24 KB (el tope que acepta la restauración; pasa con muchas reglas
  de nombres muy largos), `GET /api/backup` contesta 500 con un mensaje claro en vez de entregar un
  archivo que después no se podría restaurar. El texto se manda sin copiarlo a un `String`.
- La subida va en pedazos de hasta 3 KB (el servidor no acepta cuerpos de más de 4 KB). Cada
  pedazo revisa sesión y anti-CSRF **antes** de escribir: sin ellos no se toca `/restore.tmp`.
  Los pedazos van en orden: el número va en el header `X-Parte` (0 = empezar de nuevo, después el
  siguiente; en modo raw el servidor no lee la URL), con tope de 3072 B por pedazo y 24 KB en total
  (413 y se borra el temporal). Así no se puede llenar la flash.
- `check` y `apply` leen el archivo línea por línea con `BackupReader`: 300 bytes por línea, 200
  reglas, palabras fijas, números en rango, dominios normalizados, nombres válidos, horarios que
  apuntan a aparatos que existen y `fin` obligatorio. El primer error para todo y dice la línea.
- **Se revisa entero antes de tocar nada**: `apply` vuelve a validar y solo entonces cambia
  reglas, familia, listas y Private Relay; reglas y familia se guardan cada una con archivo nuevo +
  `rename`, como `/custom.txt`. Un archivo cortado o editado mal no cambia nada (prueba de la
  sección 23). Si la placa tiene poca memoria seguida, contesta 503 antes de tocar nada. **No es
  todo o nada al escribir**: si la flash falla a medias, contesta 500 y dice qué no se guardó
  ("se guardaron las reglas, pero no los aparatos" o "se guardó casi todo, pero no las opciones").
- Una página de internet no puede subir un respaldo: le faltan el `Host`, el header anti-CSRF y la
  sesión.

### Listas nuevas

- Las dos listas nuevas (estafas y español) vienen **apagadas de fábrica**; encenderlas solo hace
  que la placa **baje** otra lista, por HTTPS con las CAs de `certs.h`. No se manda nada.
- Al cambiar la selección se permite que la lista nueva quede más chica que la mitad (si no, quitar
  una lista se tomaría como una descarga rota). Solo pasa con un cambio hecho desde el panel con
  sesión.
- Las direcciones, tamaños y licencias de las listas nuevas están marcados "falta verificar"
  ([lista.md](lista.md#listas-opcionales-fase-5)).

### Borrar la configuración

BOOT 10 s también borra `/familia.txt` (nombres, horarios, sal) y deja una sal nueva: quien regala o
vende la placa no deja los nombres de su familia. Las reglas propias se quedan, como hasta ahora.

## Riesgos aceptados

- **El panel es HTTP.** Quien ya espía la WiFi puede ver la contraseña al entrar o al crearla;
  después solo viaja el token. TLS gastaría ~45 KB de RAM por conexión en un chip sin PSRAM. No
  reutilices una contraseña importante.
- **El portal tampoco tiene HTTPS.** La contraseña del WiFi de la casa viaja en texto dentro de la
  red `SiBlock-XXXX`, que sí va cifrada con WPA2. Quien conozca la contraseña del AP y esté al
  alcance podría verla.
- **Si el portal se abrió porque no hay WiFi, la casa no tiene DNS** en ese rato (la placa no está
  en la red). Si se abrió con BOOT y el WiFi seguía conectado, la casa sigue funcionando igual.
- **La IP de origen se puede falsificar dentro de la LAN.** El bloqueo por IP frena a un aparato
  normal, no a uno que controla su pila de red. Lo que sostiene la defensa es la contraseña larga.
  El chequeo de "viene de la red del AP" de `/api/wifi` usa la IP de origen y a qué IP llegó la
  conexión; como la red del AP nunca comparte rango con la casa, una IP falsa de esa red no
  recibe las respuestas TCP. Lo principal sigue siendo WPA2 en la red del AP.
- **Falsos positivos del hash de la lista** (~1 en 6 millones): se arreglan con "Permitir".
- *(Fase 2)* **Una página web HTTP puede saber si el visitante está detrás de una SiBlock**: le
  basta con intentar cargar `http://x.prueba.siblock.internal/siblock-prueba.gif`. No obtiene nada
  más (ni la IP de la placa ni datos de la casa). Desde una página HTTPS los navegadores actuales
  suelen bloquear esa imagen HTTP o pasarla a HTTPS, y la prueba falla (falta verificar en cada
  navegador). Riesgo bajo, aceptado a cambio de la comprobación.
- *(Fase 2)* **La tabla de aparatos que se saltan se puede falsificar desde la LAN.** Un aparato de
  la casa que finja la IP de origen (UDP) puede llenar las 16 casillas con consultas a, por
  ejemplo, `dns.google`: saca de la tabla a otro aparato o le pone un aviso falso de "DNS cifrado".
  Solo cambia lo que dice "Algo no funciona"; no da acceso ni saca datos. Riesgo bajo, aceptado.
- *(Fase 2)* **Una hora equivocada no congela la historia.** Si la hora de internet salta una vez
  hacia adelante, las horas reales siguientes parecen "viejas"; tras 64 seguidas la historia
  empieza de nuevo (se pierde lo guardado, pero no queda congelada ni hace falta borrar la
  configuración).
- *(Fase 3)* **Reportes falsos del panel**: van sin firma (ver arriba). Pueden ensuciar conteos,
  frenados por el límite por IP, uno por número y los umbrales de 3 y 5.
- *(Fase 3)* **Los reportes de módem y de bloqueos no se borran con el tiempo** (los conteos sí, a
  los 400 días): son la guía de la comunidad. Llevan país, proveedor, opciones fijas, el día y el
  número pasado por SHA-256; nada que identifique una casa sin la pimienta.
- **Una lista de permitidos aprobada por error** deja pasar ese sitio en las casas que usan la
  opción hasta que el dueño lo quite (un día como mucho para que las placas lo vean). Se acepta:
  la opción está apagada de fábrica y «Bloquear» de cada casa gana.
- *(Fase 3)* **Los certificados de Cloudflare en el ESP32 están sin probar**: la placa suma GTS
  Root R1/R4 y SSL.com TLS 2022 (RSA/ECC) por si Cloudflare cambia de emisor (falta verificar con
  la placa). Si fallan, la placa da `sin_internet`/`lastPing:"fail"`; no afecta al DNS.
- *(Fase 5)* **Los horarios se saltan** cambiando la MAC, con DNS cifrado o con una VPN. Son una
  ayuda para la familia, no un control parental a prueba de todo. Lo dice el panel.
- *(Fase 5)* **La huella con la sal se puede revertir probando MACs**: la sal y las huellas viajan
  en el respaldo, así que quien tenga el archivo puede sacar las MACs. El panel avisa que es un
  archivo privado y que no hay que compartirlo.
  Se acepta: el respaldo ya es un archivo privado de la casa (lleva nombres y horarios) y la huella
  sirve para no dejar la MAC escrita tal cual, no para esconderla de quien tiene el respaldo.
- *(Fase 5)* **El ahorro es un estimado** (30 KB por consulta bloqueada, sin fuente): el panel dice
  "Aproximado".
- *(Fase 2)* **El canario de Firefox es siempre NXDOMAIN.** Quien quiera DoH en Firefox dentro de la
  casa puede activarlo a mano en Firefox (el canario solo apaga el automático).

## Revisiones hechas

Las auditorías con security-suite y dos revisiones de código independientes están fuera del repo,
en la carpeta de reportes del autor. Cada hallazgo arreglado tiene su prueba en
`sim/test_sim.py` o en `test_host/`. Antes de hacer público el repo se corre security-suite en
modo completo sobre todo el historial de git (diseño, 1.9).

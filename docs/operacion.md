# Operación

## Instalar y actualizar

La forma normal es la página https://siblock.s-dev.win (Chrome o Edge, placa por USB). Instala el
firmware y pide el WiFi por USB (Improv). Al terminar, se cierra el instalador y se pulsa
"Ver mi contraseña de respaldo": la página la lee por USB y la muestra. Recién recibido el WiFi,
la placa pasa uno o dos minutos bajando su primera lista y no contesta por USB ni en el panel;
la página espera hasta 3 minutos, vuelve a preguntar cada 20 s y avisa que se está preparando.

**Actualizar** es lo mismo: abrir la página con la placa conectada y pulsar "Instalar SiBlock".
La página escribe solo el arranque (0x1000), la tabla de particiones (0x8000) y el programa
(0x10000), así que **no borra el WiFi ni las contraseñas** (viven en la NVS, en 0x9000) ni la
lista. Si el instalador ofrece "borrar todo", eso sí borra todo, incluida la lista y las reglas.

Para quien compila:

```bash
cp src/secrets.example.h src/secrets.h     # opcional: WiFi 2.4 GHz + WEB_PASSWORD (12–64) de fábrica
pio run -t upload && pio device monitor
```

`secrets.h` solo siembra lo que la NVS no tenga; si ya hay WiFi o contraseña guardados, no los
cambia. Si la subida no arranca, mantén apretado **BOOT** hasta que empiece a escribir. Para
borrar todo, incluida la lista: `pio run -t erase`.

Para instalar de cero con `esptool`, cada Release trae `siblock-vX.Y.Z.bin`, que se escribe desde
0x0. Ojo: ese archivo sí rellena el hueco de la NVS y **borra el WiFi y las contraseñas**.

## Primer arranque

1. Imprime `=== SiBlock <versión> ===`, abre la NVS, corre la autoprueba de la clave y muestra
   `[ap] respaldo: SiBlock-XXXX / <contraseña>`.
2. Si hay WiFi guardado, empieza a conectar **sin esperar**: el DNS y el panel arrancan igual.
3. Monta LittleFS (si no puede, lo formatea), abre mDNS (`siblock.local`) y carga `/list.bin`.
4. El DNS empieza a contestar **de inmediato**: sin lista, reenvía todo.
5. Si no había lista, la baja: unos 1–2 min en la placa. Mientras tanto el panel no responde.
6. Sin contraseña del panel, el panel muestra "Crea tu contraseña" durante 30 min.

Sin WiFi guardado espera el WiFi por USB (Improv). Si en 2 min no llega, abre el modo de
configuración por WiFi (la red `SiBlock-XXXX`, ver "Si cambias tu WiFi").

Después, en el router:

1. Reserva la IP de SiBlock en el DHCP.
2. Pon esa IP como **único** DNS de la casa.
3. Apaga el DNS por IPv6 que reparte el router.

La guía por marca y proveedor está en https://siblock.s-dev.win/guia-router.html (hoy es la guía
general para cada marca, marcada "falta verificar"); la de FortiGate en
https://siblock.s-dev.win/fortigate.html. Para comprobar que funcionó, ver
[¿Este aparato usa SiBlock?](#este-aparato-usa-siblock-fase-2).

## ¿Este aparato usa SiBlock? *(fase 2)*

Abre el panel **desde el aparato que quieres revisar**. Arriba, sin contraseña, sale:

- "✅ Este aparato está usando SiBlock.", o
- "❌ Este aparato todavía no usa SiBlock." y el botón **Algo no funciona**.

Cómo lo sabe: el panel carga una imagen chiquita desde un nombre que solo SiBlock contesta
(`<12 al azar>.prueba.siblock.internal`). Si carga, las preguntas de ese aparato llegan a SiBlock.
"Comprobar otra vez" repite la prueba (espera hasta 4 s). La página pública
(https://siblock.s-dev.win) no puede hacer esta prueba: solo lleva al panel.

En el simulador la imagen siempre falla (la computadora no usa SiBlock como DNS): es lo esperado.

## Algo no funciona *(fase 2)*

El botón (público, en el panel) repite la prueba y pide a SiBlock `GET /api/diagnose`. Muestra una
lista con lo que encontró, lo grave primero, y qué hacer en palabras simples:

| Lo que ves | Qué hacer |
|---|---|
| Este aparato pregunta a SiBlock, pero esta revisión no pasó por SiBlock | Apagar "DNS seguro" en los ajustes del navegador (cómo, en "Ver qué hacer"); o apagar el DNS por IPv6 del módem o router |
| Este aparato no pregunta a SiBlock | Revisar que el módem o router dé la dirección de SiBlock como DNS (guía) y desconectar y volver a conectar el WiFi del aparato; apagar el DNS por IPv6 del módem o router |
| No ha preguntado nada en los últimos 10 minutos | Usar el aparato un momento y volver a probar |
| Usa un DNS cifrado y se salta SiBlock | Apagar "DNS privado" (Android) o "DNS seguro" (navegador, Windows) en ese aparato |
| Usa iCloud Private Relay | Apagarlo en el iPhone/Mac, o (con la contraseña del panel) activar "Apagar iCloud Private Relay en esta casa" |
| SiBlock está en pausa | Esperar o reanudar desde el panel |
| Todavía no tiene su lista | Revisar que tenga internet y esperar unos minutos |
| No logra hablar con internet | Revisar el módem |
| Todo llega desde tu módem o router | El router (mesh) hace de DNS: que reparta la dirección de SiBlock directo (guía) |
| No encontré nada raro | — |

Solo habla del aparato que pregunta y del estado general de SiBlock; nunca dice qué visitó nadie.
Detalle de los códigos en [api.md](api.md#get-apidiagnoseprobeokfail-fase-2).

## Opción de iCloud Private Relay *(fase 2)*

En el panel (con sesión), en "Bloqueo": casilla **Apagar iCloud Private Relay en esta casa**.

- **Apagada de fábrica.** Encendida, SiBlock contesta "no existe" a `mask.icloud.com` y
  `mask-h2.icloud.com`: Safari en iPhone y Mac deja de usar Private Relay en tu WiFi y avisa a la
  persona. SiBlock puede bloquear sus anuncios, pero se pierde la privacidad extra de Private
  Relay. El panel lo advierte antes de activarla.
- Se guarda en la placa (`relay_block` en la NVS) y se aplica al momento, **también durante la
  pausa**.
- Apagada, SiBlock solo **detecta** los aparatos que usan Private Relay y lo avisa en "Aparatos que
  se saltan SiBlock" y en "Algo no funciona".
- "Borrar la configuración" (BOOT ≥ 10 s) la vuelve a apagar.

El canario de Firefox (`use-application-dns.net`) no es opción: SiBlock siempre contesta "no
existe", y así Firefox apaga su DNS cifrado automático en tu casa.

## Gráficas y más bloqueados *(fase 2)*

Con sesión, el panel muestra:

- **Uso**: barras de consultas (gris) y bloqueadas (color) por hora en las últimas **24 horas**, o
  por día en los últimos **7 días**, en la hora de tu aparato. Sin la hora de internet dice
  "Todavía no hay datos".
- **Más bloqueados**: los 10 dominios con más bloqueos de estos días (cuenta aproximada; se parte
  a la mitad cada 24 h). Vive solo en RAM: se pierde al reiniciar.
- **Aparatos que se saltan SiBlock**: IP, qué usa (DNS cifrado o Private Relay) y hace cuánto. Solo
  en RAM, se olvidan a las 24 h. Si no hay ninguno, no se muestra.

La placa guarda **168 horas** (7 días × 24) de pares consultas/bloqueadas por hora UTC. Se
escriben en `/history.bin` (LittleFS, 1352 B, vía `/history.tmp` y `rename`) **cada 60 min**; al
reiniciar se pierde como mucho la última hora. Si el reloj salta 7 días o más (p. ej. la placa
estuvo apagada), la historia vieja se limpia; una hora atrasada dentro de la semana suma en su
casilla.

En el celular el panel usa letra de 16 px, botones de 44 px de alto y tablas que se deslizan de
lado.

## Compartir con SiBlock *(fase 3)*

En el panel (con sesión), tarjeta **Compartir con SiBlock**. Tiene dos opciones, las dos
**apagadas de fábrica**. Con las dos apagadas, SiBlock solo se conecta a internet para bajar la
lista y pedir la hora.

**Compartir datos anónimos.** Encendida:

- una vez al día SiBlock manda a `siblock.s-dev.win` un número al azar de esta instalación (no dice
  quién eres), la versión de SiBlock y cuántos anuncios quitó ese día. El primero sale al minuto de
  encenderla, y solo si la placa ya tiene la hora de internet;
- se pueden mandar, **solo cuando tú lo pides**, dos reportes:
  - **tu módem**: la marca, si pudiste cambiar el DNS (Sí/No) y en qué menú (Internet o WAN /
    Red local, LAN o DHCP / Avanzado / Otro o no lo encontré). Botón «Mandar»;
  - **«No debió bloquearse»**: en la lista de bloqueados, eliges el motivo (Una página no abre /
    Una app no funciona / Un juego no funciona / Otro) y se manda ese sitio. Con la opción
    apagada, el botón explica que hay que activarla antes.
- Nunca manda tu IP, la MAC, nombres de aparatos ni qué sitios visitas. El servidor ve tu país y tu
  compañía de internet por la conexión, pero no guarda tu IP.

**Avisarme de versiones nuevas.** Encendida, una vez al día SiBlock pide el archivo público
`https://siblock.s-dev.win/version.json` (no manda nada tuyo). Si hay una más nueva, el panel
muestra: «Hay una versión nueva de SiBlock (vX.Y.Z). Se instala desde la página con el cable
USB.» con el enlace a la página. Con la versión `dev` (compilada a mano) nunca avisa.

**Cómo apagarlas.** Quita la marca en el panel: se guarda al momento y no sale nada más. Apagar el
aviso de versión también borra el aviso que estuviera en pantalla. "Borrar la configuración"
(BOOT ≥ 10 s) apaga las dos y cambia el número de instalación: los reportes de antes ya no se
pueden ligar a la placa.

Si un reporte falla, el panel dice:

| Mensaje | Qué pasó |
|---|---|
| «SiBlock no pudo llegar a internet. Prueba más tarde.» | Sin WiFi, sin memoria para la conexión segura, o el servidor no contestó en 8 s (`sin_internet`) |
| «Mandaste muchos seguidos. Espera un minuto.» | El servidor dijo que llegaron muchos desde tu casa (`limite`) |
| «No se pudo guardar. Prueba más tarde.» | El servidor no lo aceptó (`rechazado`) |

Desde el panel también hay dos enlaces a la página (se abren en otra pestaña):

- **Reportar desde la página** (`reporte.html#i=<número>`): el mismo reporte, con el mismo número,
  para que no cuente dos veces. La página pide además la casilla de Turnstile.
- **Escríbenos** (`contacto.html#diag=…`): Contáctanos con el diagnóstico de la placa (versión,
  memoria libre, último error de la lista y lo que encontró "Algo no funciona"). Va después del `#`,
  así que no llega a ningún servidor hasta que la persona marca «Adjuntar el diagnóstico de mi
  SiBlock» y manda el mensaje. La placa nunca manda correos.

Se guarda en la NVS: `install_id` (32 hex), `cloud_share` y `ver_check` (solo `1` = encendido;
falta, `0`, `2` o ilegible = apagado). La última versión vista y cómo salió el último aviso
(`lastPing`) viven solo en RAM. Para ver el estado: `GET /api/cloud` ([api.md](api.md#get-apicloud--fase-3)).

## Aparatos de la casa *(fase 5)*

En el panel (con sesión), sección **Aparatos**, debajo de "Bloqueo". Cada aparato sale con su
nombre (o "Aparato sin nombre" y su IP), cuántas consultas hizo y cuántas se bloquearon. "Este
aparato" marca el que tiene abierto el panel.

- **Ponerle nombre**: "Tele sala", "Cel de mamá 📱"… De 1 a 32 bytes (letras con acento y emoji
  cuentan más de uno). Caben 32 aparatos con nombre.
- **Pausar 1 h / Reanudar**: quita el bloqueo de anuncios **solo en ese aparato**, por ejemplo
  cuando a alguien no le abre una página. Un reinicio quita la pausa (igual que la de toda la casa).
- **Olvidar** (pide confirmar): borra el nombre y lo saca de los horarios.
- "Todavía no lo reconozco; vuelve a intentar en un minuto": la placa aún no ve ese aparato en su
  tabla de la red. Mientras tanto no se le aplica nada.

Cómo lo reconoce: por la MAC que ve en la red, pero **no la guarda**. Guarda una huella hecha con
la MAC y un número al azar propio de la placa (ver [seguridad.md](seguridad.md#aparatos-con-nombre-y-su-huella)).
Ojo: muchos celulares cambian su MAC ("dirección privada" o "MAC aleatoria" en el WiFi). Si la
cambian, SiBlock lo ve como un aparato nuevo sin nombre: ponle nombre otra vez y olvida el viejo.
Para que no pase, en el celular se puede apagar la dirección privada **solo para el WiFi de la
casa** (el menú cambia en cada teléfono: falta verificar por marca).

Todo se guarda en `/familia.txt` (LittleFS). La pausa por aparato y las IPs viven solo en RAM.

## Horarios para los niños *(fase 5)*

Sección **Horarios** del panel. Cada horario tiene:

- **Aparatos**: los que tienen nombre (hay que nombrarlos antes).
- **Qué bloquear**: "Redes sociales", "Juegos" y/o "Todo internet" (para la hora de dormir).
- **Días** (L M M J V S D) y **horas** de inicio y fin. Si el fin es antes que el inicio (de
  21:00 a 07:00), cruza la medianoche y cuenta el día en que empieza.

Caben 8 horarios. Lo bloqueado por un horario contesta igual que un anuncio y cuenta como
bloqueado. Un horario **gana sobre "Permitir"** y sigue valiendo aunque la casa o el aparato estén
en pausa.

La hora: el navegador manda la hora de tu casa cada vez que guardas un horario.

- "Los horarios empiezan cuando SiBlock tenga la hora de internet": sin hora, **no bloquean**
  (nada que asuste). Si sigue, revisa que el router deje salir NTP (UDP 123).
- "La hora de tu casa cambió. Guarda otra vez un horario para corregirla": pasa con el cambio de
  horario de verano o si abres el panel desde otro país.

Las listas de redes sociales y juegos son cortas y escritas a mano (Facebook, Instagram, TikTok,
Snapchat, X, Threads, Pinterest, Reddit, Discord; Roblox, Fortnite, Minecraft, Xbox, PlayStation,
Steam, Supercell). Falta verificar que cubran todas las apps: algunas usan otros nombres.
Un aparato con DNS cifrado o VPN se salta los horarios igual que se salta el bloqueo.

## Listas que puedes sumar *(fase 5)*

En "Bloqueo" hay dos casillas, **apagadas de fábrica**:

- **Cuidar de estafas y tiendas falsas (recomendado para abuelos y niños)**. No cubre virus: falta
  elegir una lista de virus que quepa (falta verificar).
- **Quitar algunos anuncios más de sitios en español (por ahora son pocos)**. Hoy la lista usada
  (EasyList Spanish) trae solo ~23 dominios que el DNS puede usar; falta buscar una mejor.

Al cambiarlas, SiBlock baja la lista otra vez (el panel no responde en ese rato). Se bajan en este
orden: **estafas, español, general**: si no cabe todo (tope de 180 000 dominios), lo que queda
fuera es el final de la general (anuncios), nunca estafas. Con estafas prendida puede pasar: se
suman unos 182 000 contando repetidos (falta verificar en la placa), y el panel dice "No cupieron N dominios de anuncios; las estafas
sí están todas". Detalle de cada lista en [lista.md](lista.md#listas-opcionales-fase-5).

## La luz de la placa *(fase 5)*

El LED azul (GPIO2) dice cómo está SiBlock. Ojo: no todas las placas lo traen (por ejemplo, la
ESP32-DevKitC de Espressif solo trae la luz roja de corriente; falta verificar qué modelos sí). Sin
esa luz, mira el panel; lo de "Apagada" de la tabla vale solo para placas que sí la tienen:

| Luz | Qué quiere decir | Qué hacer |
|---|---|---|
| **Fija** | Todo bien | Nada |
| **Parpadea despacio** (1 s) | Está bajando la lista | Espera unos minutos |
| **Parpadea rápido** (0,2 s) | Algo falla: sin WiFi de la casa, modo de configuración abierto, sin lista o el DNS de internet no contesta | Abre el panel; si no abre, busca la red `SiBlock-XXXX` ("Si cambias tu WiFi") |
| Apagada (en una placa que trae la luz azul) | La placa no tiene corriente o se trabó | Desconéctala y vuelve a conectarla |

Mientras aprietas BOOT, la luz hace lo de la tabla de [Botón BOOT](#botón-boot). No se puede
apagar. La página lo explica con un dibujo: `https://siblock.s-dev.win/#luz` (el panel enlaza ahí
con "¿Qué dice la luz?").

## Cuánto te ahorra *(fase 5)*

Debajo de la gráfica: "Aproximado: en 7 días tu casa se ahorró unos N MB de anuncios". Es la suma
de las bloqueadas de los últimos 7 días por **30 KB** cada una. Los 30 KB son un estimado **sin
fuente** (falta verificar): tómalo como una idea, no como una medida. Con 0 no se muestra. No hay
"este mes": la placa guarda 7 días.

## Respaldo *(fase 5)*

Sección **Respaldo** del panel. "El respaldo guarda tus reglas, los nombres de los aparatos, los
horarios, las listas elegidas y la opción de Private Relay. No guarda el WiFi ni la contraseña."

- **Bajar respaldo**: descarga `siblock-respaldo-AAAA-MM-DD.txt`.
- **Subir respaldo**: eliges el archivo; el panel lo revisa y muestra un resumen (cuántas reglas,
  aparatos y horarios). Solo con **"Sí, reemplazar"** cambia algo. Se revisa entero antes de tocar nada: si el
  archivo está cortado o editado mal, no se toca nada y el panel dice qué línea falla. Si la placa
  falla al guardar a medias (raro), el panel dice qué no se guardó.

Sirve para cambiar de placa o tener una de repuesto. Es un archivo privado: guárdalo con cuidado y
no lo compartas. Lleva los nombres de los aparatos, los horarios de la casa y datos que sirven para
reconocer a cada aparato. Los nombres siguen valiendo en otra placa porque el
respaldo lleva el número al azar con que se hacen las huellas. Las opciones de "Compartir con
SiBlock" no van en el respaldo: la placa nueva empieza con todo apagado.

## Botón BOOT

GPIO0 se lee solo después del arranque. Mientras lo mantienes, el **LED azul** (GPIO2) te dice
cuándo soltar:

| Mantienes | LED | Al soltar |
|---|---|---|
| Menos de 2 s | Parpadeo rápido | Abre la **ventana de 30 min** para crear la contraseña del panel |
| Entre 2 y 3 s | Parpadeo rápido | Nada |
| Entre 3 y 8 s | **Fijo** | Abre el **modo de configuración** por WiFi |
| Entre 8 y 10 s | Parpadeo rápido | Nada |
| 10 s o más | Se apaga a los 10 s | **Borra la configuración** y reinicia (no hace falta soltar) |

Un toque de menos de 50 ms no cuenta (rebote del botón).

Por la serie sale qué hizo: `[boot] ventana de 30 min para crear la contraseña`,
`[boot] modo de configuración por WiFi` o `[boot] borrando la configuración`.

## Si cambias tu WiFi

Opción 1, con la computadora: conecta SiBlock por USB, abre la página y vuelve a instalar; el
instalador pide el WiFi nuevo.

Opción 2, con el celular:

1. Mantén BOOT entre 3 y 8 s (suelta cuando el LED quede fijo). También se abre solo si el WiFi
   guardado falla 10 min seguidos.
2. En el celular, conéctate a la red `SiBlock-XXXX` con la **contraseña de respaldo** (la que
   mostró la página al instalar; también está en el panel y sale por la serie al arrancar).
3. Se abre la página "Configura el WiFi de SiBlock" (si no, entra a `http://192.168.4.1/`; si tu
   casa también usa direcciones 192.168.4.x, SiBlock usa otra: la ves en el aviso de "iniciar
   sesión en la red" del celular).
   Elige tu red, escribe la contraseña y pulsa "Conectar".
4. SiBlock cierra su red y se conecta a la tuya.

Qué pasa con la casa mientras está abierto:

- Si se abrió porque **no hay WiFi** (no hay uno guardado o se cayó 10 min), la placa no está en
  tu red: **la casa no tiene DNS** en ese rato. Si nadie se conecta en 15 min, se cierra solo,
  vuelve a probar el WiFi guardado 10 min y, si sigue sin conectar, se abre otra vez.
- Si lo abriste con **BOOT y el WiFi seguía conectado**, la casa sigue funcionando igual (DNS y
  panel); solo los aparatos conectados a `SiBlock-XXXX` ven la página para elegir red. Se cierra
  al guardar un WiFi nuevo o tras 15 min sin nadie conectado.

## Borrar la configuración

BOOT 10 s o más. El LED se apaga, imprime `[boot] borrando la configuración` y reinicia.

| Se borra | Se queda |
|---|---|
| WiFi guardado | La lista (`/list.bin`) |
| Contraseña del panel | Tus reglas (`/custom.txt`) |
| Contraseña de respaldo (al arrancar se crea otra nueva) | El firmware |
| Opción de Private Relay (vuelve a quedar apagada) *(fase 2)* | |
| La historia de las gráficas (`/history.bin`: a qué horas hay gente en casa) *(fase 2)* | |
| "Compartir datos anónimos" y "Avisarme de versiones nuevas" (vuelven a quedar apagadas) *(fase 3)* | |
| El número de instalación (`install_id`): se crea otro nuevo *(fase 3)* | |
| Nombres de los aparatos, horarios y hora de la casa (`/familia.txt`) *(fase 5)* | |
| Las listas elegidas (clave `lists` de la NVS: vuelve a quedar solo la general) *(fase 5)* | |

Después del borrado, al arrancar no hay WiFi: dalo por USB desde la página o por el modo de
configuración (con la **nueva** contraseña de respaldo, que sale por la serie y en la página).
Para borrar también la lista y las reglas: "borrar todo" en el instalador o `pio run -t erase`.

## Salida por serie (115200)

| Línea | Significado |
|---|---|
| `=== SiBlock <versión> ===` | Arranque |
| `[clave] autoprueba ok` / `[clave] AUTOPRUEBA FALLÓ` | Autoprueba del PBKDF2 |
| `[ap] respaldo: SiBlock-XXXX / <contraseña>` | Nombre y contraseña del modo de configuración |
| `[wifi] sin WiFi guardado: dalo desde la página de instalación` | No hay WiFi en la NVS: espera Improv |
| `[wifi] conectando a "<red>"` | Empezó a conectar (sin bloquear el arranque) |
| `[wifi] conectado. IP: …` | IP que recibió |
| `[listo] DNS en …:53 · panel en http://…/` | Todo arrancó |
| `[lista] 1/2 <url> (RAM libre N KB)` | Bajando una lista (*fase 5*: hasta `1/4` … `4/4` con las listas opcionales; la de estafas va primero) |
| `[lista] lista nueva: N dominios en T s` | Actualización correcta |
| `[lista] reintento en N min` | Falló; sigue la lista anterior |
| `[wifi] se perdió la conexión` / `reconectado. IP: …` | Corte de WiFi |
| `[wifi] sigue sin conexión: reconectando a mano` | A los 2 min sin WiFi reintenta a mano (ya no reinicia) |
| `[portal] abierto: SiBlock-XXXX` / `[portal] cerrado` | Se abrió o cerró el modo de configuración (la contraseña no sale aquí) |
| `[portal] WiFi guardado: "<red>"` / `[improv] WiFi guardado: "<red>"` | WiFi nuevo desde el celular o desde la página por USB |
| `[boot] …` | Acción del botón BOOT (ventana de 30 min, modo de configuración o borrado) |
| `[web] contraseña del panel creada` / `cambiada; sesiones cerradas` | Primer uso o cambio de contraseña |
| `[dns] autoprueba del puerto 53: ok` | La autoprueba funciona en esta placa (al minuto de arrancar) |
| `[hora] sin hora de NTP todavía` | NTP bloqueado o lento; la descarga se intenta igual |
| `[lista] RAM seguida N KB: no se baja` | Memoria fragmentada; a la tercera reinicia solo |
| `[web] <ip> bloqueada N s por contraseñas malas` | Bloqueo por intentos |
| `[nube] aviso diario: ok` / `falló` *(fase 3)* | Salió el aviso diario (solo con "Compartir datos anónimos") |
| `[nube] versión publicada: vX.Y.Z` / `no se pudo leer` *(fase 3)* | Consulta de versión (solo con "Avisarme de versiones nuevas") |
| `STATS,<consultas>,<bloqueadas>,<dominios>,<ram_libre>` | Cada 5 s, para leerlo desde otra placa |
| Bytes que empiezan con `IMPROV` | Respuestas de Improv para el instalador; se pueden ignorar |

## Problemas comunes

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| Un sitio no carga | Está en la lista | Panel → Pausar 5 min para confirmarlo, y luego "Permitir" el dominio |
| Algo de la casa (p. ej. `plex.direct`) deja de andar y el contador `rebind` sube | Anti-rebinding | "Permitir" ese dominio |
| Los anuncios siguen en un aparato | DNS seguro o DoH en el aparato, o DNS por IPv6 | Abrir el panel en ese aparato y pulsar **Algo no funciona**; desactivarlo en el aparato o en el router |
| Safari en iPhone/Mac se salta SiBlock | iCloud Private Relay | Activar "Apagar iCloud Private Relay en esta casa" en el panel |
| El panel dice "Todo llega desde tu módem o router" | Router mesh que hace de DNS | Que el router reparta la dirección de SiBlock directo (guía del router) |
| Las gráficas dicen "Todavía no hay datos" | La placa aún no tiene la hora de internet (NTP) o acaba de instalarse | Esperar; si sigue, revisar que el router deje salir NTP (UDP 123) |
| El panel dice "entra por la IP de SiBlock o por http://siblock.local" | Entraste por un nombre que reparte el router | Usa la IP, `siblock.local`, o ponlo en `PANEL_EXTRA_HOST` |
| El panel dice "Pulsa el botón BOOT de tu SiBlock una vez…" | Pasaron los 30 min sin crear la contraseña | Un toque corto a BOOT y vuelve a cargar el panel |
| El panel dice "error: …" | Falló la última descarga | Mira la serie; se reintenta sola (1, 5 y 30 min) |
| "la lista no cupo completa" | Se pasó de `MAX_DOMAINS` | Quita una lista o súbelo con cuidado (≤ 200 k) |
| La casa se quedó sin internet | SiBlock apagado, colgado o en modo de configuración | Si la tarea DNS se trabó, el vigilante lo reinicia en 30 s. Si cambió el WiFi, ver "Si cambias tu WiFi". Como plan B, pon un segundo DNS en el router (pero así se salta el bloqueo a ratos) |
| Olvidaste la contraseña del panel | — | Baja antes un respaldo si puedes. Luego BOOT 10 s o más (borra WiFi, contraseñas, nombres de aparatos, horarios y listas elegidas; no las reglas) y vuelve a configurar |
| Olvidaste la contraseña de respaldo | — | Está en el panel (con sesión); también sale por la serie al arrancar y la página la lee por USB |
| Un reporte dice «SiBlock no pudo llegar a internet» *(fase 3)* | Sin WiFi, poca memoria o el servidor no contesta | Prueba más tarde, o manda el reporte con "Reportar desde la página" |
| Un reporte dice que hay que activar "Compartir" *(fase 3)* | "Compartir datos anónimos" está apagado (así viene) | Actívalo en "Compartir con SiBlock" o usa "Reportar desde la página" |
| Un aparato con nombre sale otra vez "sin nombre" *(fase 5)* | El celular cambió su MAC (dirección privada) | Ponle nombre otra vez y "Olvidar" el viejo; o apaga la dirección privada solo en el WiFi de la casa |
| Un horario no bloquea *(fase 5)* | Sin hora de internet, el aparato no se reconoce todavía, usa DNS cifrado o VPN, o la app usa otro nombre | Mira los avisos de la sección Horarios y "Algo no funciona" en ese aparato |
| La luz parpadea rápido *(fase 5)* | Sin WiFi, modo de configuración, sin lista o sin DNS de internet | Abre el panel; si no abre, ver "Si cambias tu WiFi" |
| Subir un respaldo dice "línea N: …" *(fase 5)* | El archivo se editó a mano o está cortado | Baja un respaldo nuevo de la placa vieja; no se cambió nada |
| El aviso de versión no aparece *(fase 3)* | La opción está apagada, la placa es `dev` o no hay versión nueva | Activa "Avisarme de versiones nuevas"; la consulta sale al minuto y luego una vez al día |

## Cambiar listas o ajustes

Todo está en `src/config.h`. Cada cambio requiere volver a subir el firmware:

- *(Fase 5)* Las listas ya no están en `config.h`: el catálogo (general, estafas y español) está
  en `lib/siblock_core/lists.cpp`, y cuáles se bajan se elige en el panel. Tienen que ser HTTPS
  desde un dominio cuya CA esté en `certs.h`.
- `MAX_DOMAINS`, `REFRESH_HOURS`, `UPSTREAM_DNS_1/2`.
- `REBIND_PROTECTION`, `DEVICE_HOSTNAME` (`siblock`), `LEGACY_HOSTNAME` (`adblock`),
  `PANEL_EXTRA_HOST`. Por mDNS solo se anuncia `siblock.local`: después de actualizar,
  `http://adblock.local` deja de abrir salvo que el router o el archivo hosts lo resuelvan; usa
  `http://siblock.local` o la IP.
- IP fija (`USE_STATIC_IP`). Es mejor reservar la IP en el router.
- `ALLOWLIST` — dominios que nunca se bloquean.

El WiFi y las contraseñas ya no están en `config.h` ni hace falta `secrets.h`: se guardan en la
NVS.

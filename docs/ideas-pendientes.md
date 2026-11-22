# Ideas pendientes

Notas para armar después el plan de trabajo con agentes (ECC / superpowers). Lo de la fase 1
(§1, instalación sin saber programar) ya está hecho: ver
`docs/superpowers/specs/2026-11-21-siblock-design.md`, `docs/operacion.md` y `docs/api.md`. La
fase 2 cubrió §2 (guía general), §2.1, §2.3, §2.5, §3, §3.2-6/7, parte de §3.3 y §3.5-7; la
fase 3 ("Cloudflare") cubrió §2.2 (sin modelo ni OUI), §3.2-8, §3.2-10, §3.3 (página pública y
panel del dueño) y §3.4; la fase 5 ("Extras") cubrió §3.1 (ideas 1 a 5) y §3.5 (ideas 3, 4, 5, 6
y 9), sin la sugerencia de nombre por marca (OUI). Lo marcado ✅ está en el código; el Worker
ya está publicado. La cajita 3D (§3.2-12) está en `hardware/caja/` y falta probarla con la placa
real; el video (§3.5-2) espera a la placa. El resto sigue pendiente. Cada sección dice qué se quiere,
por qué y qué falta decidir.

## Para quién es

Es para la casa del autor y su familia, y **quien venga de fuera recibe el mismo trato**: que
sienta que alguien de confianza se lo instaló y lo cuida, aunque no lo conozca. De ahí salen las
reglas para todo lo de abajo:

- Palabras simples, sin tecnicismos. Español primero.
- Nadie se queda atorado: cada problema común tiene un "qué hacer" en la pantalla.
- **Cada persona consigue su placa y la carga sola.** No se reparten placas ya cargadas. Por eso
  hacen falta la guía de compra (sección 0), la página de instalación y el acompañamiento.
- Nada que asuste: la casa nunca debe quedarse sin internet por culpa del bloqueador.

## Nombre y dirección

- Nombre ante la gente: **SiBlock**. Juega con "sí, bloquea" y con el apellido del autor. Revisado
  el 2026-11-21: no hay otro bloqueador ni producto conocido con ese nombre; solo un repo chico sin
  relación en GitHub.
- Dirección propuesta: **siblock.s-dev.win** para empezar. Ya está en Cloudflare y usa el mismo
  dominio que `contacto@s-dev.win` y el inicio de sesión de Access. Sale gratis.
- Si crece: `siblock.com` y `siblock.net` están en venta (Afternic). `siblock.app` y
  `siblock.dev` no tenían DNS ese día; puede que estén libres, falta confirmarlo en un registrador.
- Pendiente: logo, y si el nombre del repo y del firmware cambian de `esp32-adblock` a `siblock`.

## 0. Guía de compra

- Qué placa comprar: ESP32 clásico (WROOM-32, 4 MB), con fotos para reconocerla, y cuáles **no**
  sirven o no conviene (ESP8266, placas sin USB). Hoy `site/index.html` trae un dibujo propio
  (SVG, sin licencia de terceros) con el módulo, el USB y los botones EN/BOOT. **Pendiente:** una
  foto real con licencia clara (propia, cuando llegue la placa, §5).
- El cable USB tiene que ser **de datos**, no solo de carga. Es el error más común.
- Chip USB de la placa (CH340 o CP2102) y si hace falta driver en cada sistema.
- Cargador de 5 V cualquiera para dejarla prendida.
- Precio de referencia y en qué tipo de tienda se consigue (sin ligas de afiliado).

## Decisiones abiertas

- [x] **Repo público con licencia GPLv3** (decidido el 2026-11-21). Por qué: el bloqueador ve todo
  lo que visita la casa, y con el código abierto cualquiera puede comprobar que no manda nada; la
  comunidad puede sumar guías de módems; GPLv3 obliga a que las copias modificadas sigan abiertas,
  así nadie lo cierra y lo vende. El nombre y el logo de SiBlock no los cubre la licencia.
  **Antes de hacerlo público:**
  - revisión completa con `security-suite`, incluido **todo el historial** de git (secretos,
    correos personales, referencias a otros equipos de la casa);
  - confirmar que los reportes de seguridad siguen fuera del repo;
  - ✅ agregar `LICENSE` (GPLv3) y una línea de licencia en el README;
  - tener presente que el riesgo bajo aceptado del panel por HTTP quedará a la vista.
- [x] **La página va en Cloudflare Pages** (decidido el 2026-11-21), en `siblock.s-dev.win`. Ahí
  mismo viven el Worker de reportes y contacto, D1, Access del panel del dueño y el correo.
  GitHub Actions compila el firmware y publica los `.bin` + `manifest.json` junto a la página.
- [x] **DHCP en el ESP32: espera a tener la placa** (decidido el 2026-11-21). Primero se mide la
  RAM real y se ve cuántos módems de verdad no dejan cambiar el DNS (con los reportes de 2.2).
  Mientras tanto, el plan A para esos módems es cambiar el DNS en cada aparato.

## 1. Instalación sin saber programar ✅ (fase 1)

**Objetivo:** que alguien sin conocimientos conecte el ESP32 por USB, abra una página y quede
funcionando. Debe servir en Windows, Mac, Linux y ChromeOS.

**Cómo:** con **ESP Web Tools**, que carga el firmware desde el navegador (WebSerial, solo Chrome
y Edge). No hay que instalar nada. Se descartaron los scripts por sistema y los ejecutables: piden
Python y esptool, y Windows y Mac muestran avisos de seguridad.

Flujo:
1. Conectar la placa y pulsar **Instalar** en la página.
2. La página pide el WiFi y se lo pasa a la placa (protocolo Improv Serial).
3. La primera vez que se abre el panel, pide crear la contraseña.
4. Guía para poner la IP del ESP32 como DNS en el router (sección 2).
5. Comprobación de que funciona (sección 3).

**Estado:** ✅ hecho en la fase 1 (ver `docs/operacion.md` y `docs/api.md`). Queda pendiente solo
lo que necesita la placa de verdad: la foto real de la placa, la guía de precios (§0) y probar
en la placa los tiempos y los nombres de botones del instalador (`docs/pruebas.md`).

Partes del trabajo (todas hechas):
- ✅ **Firmware: configuración en la placa (NVS) en vez de `secrets.h`.** Guardar ahí el WiFi y la
  contraseña del panel. `secrets.h` queda como opción para quien compila.
- ✅ **Firmware: Improv Serial** para recibir el WiFi por USB desde la página.
- ✅ **Firmware: modo de configuración por WiFi** (AP + portal cautivo) por si no hay WiFi guardado
  o no conecta. Ojo con la seguridad: el AP debe tener contraseña y apagarse al terminar.
- ✅ **Firmware: crear la contraseña en el primer uso.** Mientras no haya contraseña, cualquiera en la
  red podría crearla primero. Hay que acotar ese momento, por ejemplo aceptándola solo en los
  primeros N minutos tras el arranque, o solo desde el portal.
- ✅ **Firmware: borrar la configuración** manteniendo BOOT 10 s, por si se olvida la contraseña.
- ✅ **CI (GitHub Actions):** compilar en cada versión (tag), generar `manifest.json` y los `.bin`
  (bootloader, particiones, app) y publicarlos.
- ✅ **Página de instalación:** botón de ESP Web Tools, aviso de navegador no compatible, ayuda con
  los drivers USB (CH340 / CP2102) y enlace a la guía del router.
- ✅ **Seguridad:** revisar con security-suite el portal, el primer uso y el borrado. Tocan auth.

## 2. Guía del router por marca ✅ (fase 2, guía general)

**Por qué:** cambiar el DNS en el router es el paso más difícil para quien no sabe, y cada marca
tiene el menú en otro lugar.

**Estado:** ✅ en la fase 2 está la página `site/guia-router.html` con el selector de marca y de
proveedor por país, los pasos generales, el plan A (DNS en cada aparato: Windows 11, Mac, iPhone y
Android) y el aviso de IPv6. Pendiente:
- [ ] **Fichas por marca con fuente** (IP de entrada, usuario de fábrica, menú del DNS). Hoy todas
  las marcas muestran la guía general con "falta verificar"; cada ficha entra en
  `site/js/guias-datos.js` (`FICHAS`) solo cuando tenga fuente.
- [ ] **Capturas propias** (no hay ninguna hasta tenerlas).
- [ ] Menú de Android por marca de teléfono (falta verificar).
- [ ] Plan B (DHCP en el ESP32): espera a la placa (ver "Decisiones abiertas").

- Selector "¿Qué módem tienes?": TP-Link, Huawei, ZTE, Technicolor, Arris, Netgear, ASUS,
  Mikrotik y los módems del proveedor de internet.
- Por marca: IP de entrada (`192.168.1.1` o `192.168.0.1`), usuario de fábrica habitual, menú del
  DNS y capturas.
- Opción "No sé / otra": guía general.
- Caso **módem que no deja cambiar el DNS** (común en los del proveedor):
  - plan A: cambiar el DNS en cada aparato (guía por sistema);
  - plan B: **DHCP en el ESP32**. Se apaga el DHCP del módem y el ESP32 reparte las IPs con su
    propio DNS, como hace Pi-hole. Medir la RAM que gasta.
- Recordar en todas las guías que hay que apagar el DNS por IPv6 del router.

### 2.1 Por proveedor de internet ✅ (fase 2, solo la lista)

**Estado:** ✅ en la fase 2 el selector pregunta el país (adivinado por el idioma del navegador,
`es-MX` → México, y se puede cambiar) y el proveedor (México, Colombia, España y Estados Unidos,
más "Otro"). Pendiente:
- [ ] **Qué módem entrega cada proveedor, si deja cambiar el DNS y dónde**: falta verificar con
  fuente; hoy se muestra la guía general.
- [ ] Detectar el país por la conexión: llega con el Worker de la fase 3 (2.2).

La gente sabe quién le da el internet más que la marca del módem. Por eso el selector también
pregunta por el proveedor, y de ahí se sugiere el módem más común.

- Lista por país (con el país detectado y la opción de cambiarlo). Se arranca con los
  proveedores grandes de cada país y se completa con lo que se aprenda (2.2).
- Por cada proveedor: qué módems entrega, si deja cambiar el DNS, dónde, y si conviene ir directo
  al plan B (DHCP en el ESP32).
- Algunos proveedores bloquean el menú, pero lo abren si se llama a soporte. Anotarlo en la guía.

### 2.2 Que el sistema aprenda qué módem usa cada proveedor ✅ (fase 3, sin modelo ni OUI)

**Estado:** ✅ en la fase 3: reportes de módem desde la página (`site/reporte.html`, con Turnstile)
y desde el panel ("Compartir datos anónimos", apagado de fábrica), con el mismo número de
instalación al azar (uno por número en D1, guardado como SHA-256 con pimienta); proveedor y país
por `request.cf`; solo opciones fijas (fabricante, si funcionó, menú genérico: `internet_wan`,
`red_local`, `avanzado`, `otro`); tabla pública con 3 reportes iguales o más
(`site/estadisticas.html`); "otro / no lo encontré" solo suma a "guías que faltan" del panel
del dueño. Ver `docs/api.md` y `docs/seguridad.md`. Cambios respecto a la idea de abajo:
- el reporte del panel va **sin firma**: un secreto en un firmware público no prueba nada; lo
  protegen el límite por IP, uno por número y el umbral de 3 (`docs/seguridad.md`);
- el menú es una lista genérica igual para todas las marcas (no hay menús por marca con fuente).

Pendiente (fase 4):
- [ ] **Marca del módem por la MAC (OUI)** en el panel: necesita una tabla de OUI con fuente y
  licencia clara.
- [ ] **Modelo del módem** (columna `modelo` de la idea): no hay lista de modelos con fuente.
- [ ] Menús por marca, cuando haya fichas con fuente (§2).
- [ ] Publicar el Worker y probarlo con la placa (`cloud/DEPLOY.md`, `docs/pruebas.md` §3).

La idea original:

**Idea:** cada instalación puede mandar, **solo si la persona acepta**, un reporte anónimo:
proveedor + módem + si se pudo cambiar el DNS + en qué menú. Con muchos reportes, la página sabe
"con el proveedor X el módem suele ser el Y, y el DNS se cambia en Z" y lo muestra primero.

Detección automática para no preguntar lo que se puede saber:
- **Proveedor:** desde la página, con un Cloudflare Worker que lee el sistema autónomo (ASN) y el
  país de la conexión (`request.cf.asOrganization`, `request.cf.country`). No hace falta un
  servicio de terceros. La IP pública no se guarda.
- **Marca del módem:** el ESP32 conoce la IP de la puerta de enlace y su MAC (por ARP). Los
  primeros 3 bytes de la MAC (OUI) dicen el fabricante (Huawei, ZTE, TP-Link…). Se muestra en el
  panel como "parece un módem Huawei". La MAC completa no sale de la placa.
- **Modelo (opcional, para después):** leer el título de la página de entrada del módem. Es más
  invasivo; solo si la persona lo pide.

Dónde se guarda y cómo se usa:
- Cloudflare Worker + D1: una tabla `reportes (instalacion, origen, pais, asn, proveedor,
  fabricante, modelo, dns_ok, menu, fecha)`, donde `origen` es `pagina` o `panel`. No se guardan IPs, MACs completas ni nada que identifique a la persona
  o su casa.
- Una sugerencia aparece en la guía solo cuando la confirman varios reportes iguales (p. ej. 3 o
  más). Así un reporte falso o equivocado no ensucia la guía.
- Protección contra spam: Cloudflare Turnstile en el envío (en la página; desde el panel, firma
  del reporte con el número de instalación) y límite por IP en el Worker, sin guardarla.
- Página pública con la tabla agregada ("proveedor → módem → ¿se puede?"), útil también para
  quien no use el bloqueador.

Decidido:
- [x] **El reporte se manda desde los dos lados**, la página de instalación y el panel del ESP32,
  para que uno respalde al otro si el otro falla. En el panel va **apagado por defecto** y se
  explica que es lo único, aparte de la lista, por lo que la placa habla con internet.
  - Para no contar dos veces la misma casa: cada instalación genera un número al azar (no ligado a
    la persona ni a la placa) que viaja en los dos reportes. El Worker guarda uno solo por número.
- [x] **Solo opciones fijas**, nada de texto libre:
  - menús posibles por marca (lista cerrada que se arma con las guías) + "otro / no lo encontré";
  - "otro" no se publica; solo suma a un contador para saber qué guías faltan.
  - Así ya no hace falta revisar textos a mano.
- [x] **Aviso de privacidad** corto antes de enviar, en la página y en el panel: qué se manda
  (país, proveedor, marca y modelo del módem, si funcionó, el menú), qué no (IP, MAC completa,
  nombre, ubicación) y que se puede no mandar nada.

### 2.3 Caso de laboratorio: SiBlock detrás de un FortiGate ✅ (fase 2, página)

**Estado:** ✅ los 9 pasos están en `site/fortigate.html` (sin la nota del laboratorio). Pendiente:
probarlos contra el FortiGate del laboratorio cuando llegue la placa (§5).

No se salta el FortiGate: se configura para que reparta el ESP32 como DNS. Pasos en FortiOS 7.x:

1. **Reservar la IP del ESP32**: Dashboard → Network → DHCP → clic derecho → *Create reservation*.
2. **DHCP con el ESP32 como único DNS**: interfaz interna → DHCP Server → DNS server *Specify* →
   IP del ESP32, sin segundo DNS (si hay otro, los aparatos lo usan a ratos y se saltan el
   bloqueo).
   ```
   config system dhcp server
       edit <id>
           set dns-service specify
           set dns-server1 <IP-del-ESP32>
           unset dns-server2
       next
   end
   ```
3. **El DNS propio del FortiGate no se toca** (Network → DNS sigue con FortiGuard o un DNS
   público). Si se apunta al ESP32 y la placa se apaga, el FortiGate pierde sus servicios.
4. **Que nada intercepte el puerto 53**: sin VIP/DNAT al 53 y sin *DNS Service* en la interfaz
   interna. El perfil *DNS Filter* de la política puede quedarse.
5. **Dejar salir al ESP32** si las políticas son estrictas: DNS (UDP/TCP 53) a 1.1.1.1 y 9.9.9.9,
   HTTPS a GitHub (listas) y NTP (UDP 123).
6. **Opcional, que nadie se salte el bloqueador**: permitir DNS (53 y 853) solo desde el ESP32
   hacia internet y negarlo al resto de la LAN.
7. **IPv6**: apagar el DNS que anuncia la interfaz (RDNSS / DHCPv6).
8. **Probar**: renovar el DHCP de un aparato, ver que su DNS es el ESP32, `nslookup
   doubleclick.net` → `0.0.0.0`, y que el aparato aparezca en el panel.
9. **Deshacer**: `set dns-service default` en el mismo servidor DHCP.

Ojo: si el FortiGate ya tiene reglas para otros equipos de la red, esta configuración no las
necesita y no se tocan.

### 2.4 Que SiBlock configure el router solo

**Idea:** que la persona no tenga que entrar al router ni escribir comandos. Desde el panel de la
placa (o el último paso de la página de instalación), un asistente hace lo de 2.3 por su cuenta.

Cómo se vería:
1. SiBlock detecta el router (fabricante por la MAC, 2.2) y dice "Encontré un FortiGate / MikroTik
   / …. Puedo configurarlo por ti."
2. Pide **una sola vez** el acceso al router: una llave de API (FortiGate, MikroTik, pfSense,
   OPNsense) o el usuario y contraseña de administrador.
3. Muestra en palabras simples **qué va a cambiar**: "Los aparatos de tu casa van a usar SiBlock
   como DNS. Voy a apagar el DNS por IPv6. Nada más." Y un botón **Aplicar**.
4. Lo hace por la API del router, **sin comandos a la vista**.
5. Comprueba que funcionó (un aparato de prueba ya pregunta al ESP32). Si algo sale mal, **deshace
   solo** los cambios en 60 s, como cuando una tele pregunta "¿mantener esta resolución?".
6. Deja un botón **Deshacer** en el panel por si algún día se quiere volver atrás.

Reglas para que sea de confianza:
- **Siempre se avisa y se pide un "Sí"** antes de tocar el router. Lo hace solo, pero nunca a
  escondidas: es el router de la persona, y así nadie puede usar SiBlock para cambiar un router
  ajeno sin que el dueño se entere.
- **La contraseña o llave del router no se guarda**: vive en RAM mientras dura el asistente y se
  borra al terminar. Para deshacer se vuelve a pedir.
- Antes de cambiar, se guarda una copia de cómo estaba (solo lo que se va a tocar), para poder
  deshacerlo exacto.
- Solo cambia lo mínimo: el DNS que reparte el DHCP y el DNS de IPv6. Nunca firewall, WiFi,
  contraseñas ni puertos abiertos.
- Usa HTTPS con el router cuando lo permite. Muchos routers tienen certificado propio: se acepta
  ese certificado la primera vez y se fija (como SSH) para las siguientes.

Qué routers se pueden automatizar:

| Router | Cómo | Dificultad |
|---|---|---|
| FortiGate | API REST de FortiOS con llave de API | Media: la llave se crea en el FortiGate |
| MikroTik | API REST de RouterOS 7 | Baja |
| OpenWrt | `ubus` / RPC de LuCI | Baja |
| pfSense / OPNsense | API | Media |
| FRITZ!Box | TR-064 | Media |
| TP-Link, ASUS, Huawei, ZTE y módems del proveedor | **Sin API estable**; imitar la página web es frágil y se rompe con cada actualización | Alta: guía con capturas (2) |

Plan propuesto: empezar con FortiGate (se puede probar en el laboratorio) y MikroTik/OpenWrt; el
resto sigue con la guía por marca. Los reportes de 2.2 dirán qué marcas conviene sumar después.

Preguntas abiertas:
- [ ] ¿El asistente vive en el panel de la placa (está en la red de la casa y puede hablar con el
  router) o también en la página? La página no puede hablar con el router por las reglas del
  navegador (CORS), así que en la práctica es la placa.
- [ ] ¿Cabe el TLS hacia el router en la RAM de la placa junto con todo lo demás? Medirlo con la
  placa real.

### 2.5 Otros obstáculos conocidos (como el FortiGate)

**Estado (fase 2):** ✅ canario de Firefox (siempre), ✅ opción de iCloud Private Relay (apagada de
fábrica), ✅ detección de DNS cifrado y de Private Relay por aparato (solo en RAM), ✅ detección de
router que hace de DNS (mesh) y ✅ página `site/obstaculos.html` con estas tablas en palabras
simples y "Lo que un DNS no puede bloquear". Pendiente: proveedor que intercepta el 53, red de
invitados, VLANs, detectar antivirus (por ahora la página lo marca "Solo con guía": buscar "DNS
seguro" o "protección web" en el antivirus y apagarlo), detectar Windows 11 con DNS cifrado y
los perfiles de DNS del iPhone (por ahora la página los marca "Solo con guía": usan una dirección
fija y nunca le preguntan a SiBlock, así que el botón solo puede decir que el aparato no usa
SiBlock), DNS fijo de fábrica y el modo estricto (fase 4).

Cosas que hacen que un aparato **no pase por SiBlock** o que SiBlock no pueda hacer su trabajo.
"Automático" = SiBlock lo resuelve solo; "Detecta" = el botón "algo no funciona" lo encuentra y
explica qué hacer; "Guía" = solo se puede explicar.

**En la red**

| Obstáculo | Qué pasa | Qué hace SiBlock |
|---|---|---|
| Firewalls con DNS propio o redirección del 53: FortiGate, Sophos, SonicWall, WatchGuard, Palo Alto, Meraki (Umbrella), pfSense/OPNsense con regla NAT al 53, MikroTik con dst-nat | El firewall contesta o desvía las consultas y nunca llegan al ESP32 | Guía por equipo; asistente (2.4) en los que tienen API |
| Mesh con DNS propio: eero, Google/Nest WiFi, Orbi (Armor), Deco (HomeShield), UniFi (ad blocking / content filtering) | Ellos hacen de DNS y reenvían. Si se encadenan al ESP32, funciona, pero **todo llega con la IP del router**: se pierden las estadísticas por aparato, la pausa por aparato y los horarios | Detecta (todas las consultas vienen de una sola IP) y explica cómo repartir el ESP32 directo por DHCP |
| Proveedor que intercepta el puerto 53 (DNS hijacking) | Las consultas del ESP32 hacia 1.1.1.1 las contesta el proveedor | Detecta (comparar respuesta con un nombre de prueba). Arreglo futuro: DNS cifrado hacia arriba (DoT), si cabe en la RAM |
| Doble NAT (módem del proveedor + router propio) | Se configura el router equivocado | Guía: se configura el router de adentro |
| Red de invitados o aislamiento de clientes (AP isolation) | Los aparatos no pueden hablar con el ESP32 | Detecta en la guía; explicar que la red de invitados queda sin bloqueo o se pone el ESP32 en ella |
| VLANs (red de IoT aparte) | Los aparatos de la otra VLAN no llegan al ESP32 | Guía: regla entre VLANs solo para el puerto 53 del ESP32 |
| IPv6 con DNS anunciado por el router | Los aparatos preguntan por IPv6 a otro DNS | Detecta; asistente lo apaga donde hay API |

**En los aparatos**

| Obstáculo | Qué pasa | Qué hace SiBlock |
|---|---|---|
| Firefox con DNS cifrado (DoH) | Se salta el ESP32 | ✅ (fase 2) **Automático**: contestar NXDOMAIN a `use-application-dns.net` lo apaga |
| iCloud Private Relay (Safari en iPhone/Mac) | Safari se salta el ESP32 | ✅ (fase 2) **Automático**: contestar NXDOMAIN a `mask.icloud.com` y `mask-h2.icloud.com` (Apple lo apaga en esa red y avisa al usuario). **Decidido: es una opción que la persona activa**, apagada de fábrica, con un aviso de que también quita la privacidad de Private Relay. Mientras esté apagada, el botón "algo no funciona" detecta los iPhone/Mac que se saltan y sugiere activarla |
| Chrome / Edge "DNS seguro" | Solo se activa solo si el DNS configurado lo soporta; con el ESP32 no pasa. Si alguien eligió un proveedor a mano, sí se salta | Detecta (consultas a `dns.google`, `cloudflare-dns.com`…) y explica dónde apagarlo |
| Android "DNS privado" (DoT, puerto 853) | Se salta el ESP32 | Detecta y explica; modo estricto lo corta |
| Windows 11 DNS cifrado, perfiles de DNS en iPhone | Igual | Detecta y explica (pendiente; en fase 2: **Guía**, ver Estado arriba) |
| DNS fijo de fábrica: Chromecast / Google TV / Nest (8.8.8.8), algunas teles Samsung/LG, Roku | Ignoran el DHCP | Solo se arregla en el router: **modo estricto** (abajo) |
| VPN en el aparato (trabajo, NordVPN, etc.) | Usa el DNS de la VPN | Guía: es normal y no se toca; las computadoras del trabajo no se deben forzar |
| Antivirus con "DNS seguro" o protección web (Avast, Kaspersky, ESET…) | Hacen de proxy de DNS | Detecta y explica (pendiente; en fase 2: **Guía**, ver Estado arriba) |

**Lo que un bloqueador por DNS no puede quitar** (✅ fase 2: en `site/obstaculos.html#no-puede`;
decirlo claro en la página para no prometer de más): anuncios de YouTube, Twitch, Instagram, Facebook y TikTok, porque vienen del mismo dominio
que el video o las fotos. Bloquearlos rompería la app.

**Modo estricto (opcional, aparte del asistente):** que el router **solo deje salir DNS (53 y 853)
desde el ESP32** y lo niegue al resto. Es lo único que obliga a los aparatos con DNS fijo. Como sí
toca el firewall, va **separado** de 2.4 (que nunca toca el firewall), con su propio aviso, su
propio "Sí" y su propio deshacer. Advertir que puede romper aparatos del trabajo o con VPN.

## 3. Comprobación "¿este aparato usa el bloqueador?" ✅ (fase 2)

- En el panel o la página: "✅ este aparato está usando el bloqueador" / "❌ todavía no".
- Idea: un nombre de prueba (p. ej. `prueba.adblock`) que solo el ESP32 contesta con una IP
  especial. El navegador intenta cargarlo y la página sabe si la respuesta vino del ESP32.

**Estado:** ✅ en el panel, con `<12 al azar>.prueba.siblock.internal` y una imagen de 1×1
(`docs/operacion.md`, `docs/api.md`). La página pública no puede probarlo (es HTTPS y no llega a la
casa): explica y lleva al panel. Falta probarlo en un celular real (`docs/pruebas.md`).

## 3.1 Uso diario en casa ✅ (fase 5)

**Estado:** ✅ en la fase 5 (ver `docs/operacion.md`, `docs/api.md` y `docs/seguridad.md`). Falta
verificar con la placa real: la tabla ARP con más de 10 aparatos, el LED, el tamaño de las listas
nuevas y la RAM al bajarlas (`docs/pruebas.md`, "Fase 5").

1. ✅ (fase 5) **Nombres a los aparatos** ("Tele sala", "Cel de mamá") en vez de IPs. Se guardan en la placa.
   Ayuda la marca por la MAC (OUI) para sugerir el nombre.
   Hecho: sección "Aparatos" del panel; 32 nombres de 1 a 32 bytes en `/familia.txt`; el aparato
   se reconoce por una huella `SHA-256(sal ‖ MAC)` sin guardar la MAC ni la IP; "borrar la
   configuración" los borra. **Pendiente:** la sugerencia por marca (OUI) necesita una tabla con
   fuente y licencia; los celulares con **MAC que rota** ("dirección privada") salen como aparato
   nuevo y hay que nombrarlos otra vez (falta verificar el menú para apagarla en cada marca).
2. ✅ (fase 5) **Pausar solo un aparato**, no toda la casa, cuando a alguien no le carga una página.
   Hecho: "Pausar 1 h" / "Reanudar" por aparato (1 a 1440 min, solo en RAM); los horarios siguen
   valiendo durante la pausa.
3. ✅ parcial (fase 5) **Protección contra estafas y virus**: lista opcional de phishing y malware además de la de
   anuncios. Es lo que más cuida a los abuelos. Revisar que quepa en `MAX_DOMAINS`.
   Hecho: casilla "Cuidar de estafas y tiendas falsas" (el texto ya no promete virus), apagada de fábrica, que se baja **antes** que la
   general (si no cabe todo, se quedan fuera anuncios, nunca estafas). **Falta la parte de virus.** **Pendiente (falta
   verificar):** la lista elegida (HaGeZi "Fake", ~10 800 dominios) cubre estafas pero **no virus**;
   la del plan (HaGeZi TIF mini, ~245 000) no cabe en `MAX_DOMAINS`. Falta una lista de malware que
   quepa, y revisar dirección, tamaño y licencia (`docs/lista.md`).
4. ✅ (fase 5) **Horarios para los niños**: bloquear grupos de sitios (redes sociales, juegos) en ciertos
   aparatos y a ciertas horas. Necesita la hora de NTP, que ya está.
   Hecho: 8 horarios con aparatos, grupos ("Redes sociales", "Juegos", "Todo internet"), días y
   horas, con cruce de medianoche; ganan sobre "Permitir"; sin hora de internet no bloquean; el
   panel avisa si cambió la hora de la casa. **Pendiente:** las listas de cada grupo son cortas y
   a mano (falta verificar que cubran las apps); quien cambia su MAC, usa DNS cifrado o VPN se los
   salta.
5. ✅ (fase 5) **LED de estado** con el LED azul de la placa (GPIO2): fijo = todo bien, parpadeo = actualizando,
   apagado o parpadeo rápido = problema. Explicarlo en la guía con un dibujo.
   Hecho: fijo / parpadeo de 1 s (bajando la lista) / parpadeo de 200 ms (sin WiFi, modo de
   configuración, sin lista o sin DNS de internet); sección "¿Qué dice la luz?" con dibujo en la
   página (`#luz`, español e inglés) y enlace desde el panel. **Pendiente:** verlo en la placa real.

## 3.2 Acompañamiento para quien viene de fuera

6. ✅ (fase 2) **Botón "algo no funciona"**: revisa lo común (¿este aparato usa el ESP32?, DNS por IPv6, DNS
   privado o DoH) y dice qué hacer en palabras simples.
7. ✅ (fase 2) **Avisar cuando un aparato se salta el bloqueador** porque usa DNS cifrado. La lista completa
   de casos y qué se hace en cada uno está en 2.5.
8. ✅ (fase 3) **"Esto no debió bloquearse" con un toque**: reporte con opciones fijas, igual que los de módems
   (2.2), para armar una lista de permitidos de la comunidad. Mismo cuidado de privacidad.
   Hecho: botón «No debió bloquearse» en el panel (con "Compartir" encendido) y formulario en
   `site/reporte.html`; motivos fijos; uno por número y dominio; el dueño ve los 50 más
   reportados.
   ✅ **Lista de permitidos de la comunidad:** el dueño revisa los reportes en `dueno.html` y
   aprueba o quita cada sitio (tope de 500); el Worker la publica en `GET /api/v1/permitidos`. La
   placa la baja una vez al día solo con «Usar los permitidos de la comunidad» encendido (apagado de
   fábrica), sin mandar nada. Funciona como «Permitir», pero tus reglas «Bloquear» ganan y no se
   mezcla con «Mis reglas» ni con el respaldo.
9. [ ] **Avisos por Telegram** al dueño (pendiente, no entró en la fase 3), opcional: "la placa se reinició", "la lista no se pudo
   actualizar", resumen semanal. Cada quien usa su propio bot; el token se guarda en la placa y
   nunca se muestra en el panel.
10. ✅ (fase 3) **Aviso de versión nueva** en el panel con liga a la página para actualizar por USB (no hay
    OTA). La placa consulta un `version.json` público; apagable.
    Hecho: opción "Avisarme de versiones nuevas", **apagada de fábrica**; un `GET` al día sin
    mandar nada; `version.json` lo escribe CI en cada versión. Las placas activas se cuentan con
    otro aviso aparte ("Compartir datos anónimos"), no con esta consulta.
11. **Plan B con dos placas**: guía para tener una de respaldo como segundo DNS, y que las reglas
    propias se copien de una a otra. Así la casa no se queda sin internet si una falla.
12. **Cajita imprimible en 3D** para la placa (FreeCAD), con hueco para ver el LED y el botón BOOT. **Hecha:** `hardware/caja/` (falta probarla con una placa real).
    **Pendiente (fuera de la fase 5):** no es software; se hace con FreeCAD cuando llegue la placa
    real (§5), para medir el hueco del LED y del botón BOOT.

## 3.3 Gráficas de uso y visitas ✅ (fase 3, página pública y panel del dueño)

**Estado:** ✅ en la fase 3: `site/estadisticas.html` (placas activas en 7 días, versiones, países
con 5 placas o más en una tabla con barras, anuncios bloqueados por semana, tabla de módems) y
`site/dueno.html` detrás de Cloudflare Access (embudo, temas de Contáctanos, motivos, dominios
más reportados, guías que faltan). Web Analytics preparada sin cookies (se activa cuando CI pone
el token). Cambio: las placas activas se cuentan con el aviso diario de "Compartir datos
anónimos", no con `version.json` (un archivo fijo no puede contar). "Qué falla más" = temas de
Contáctanos + motivos de "no debió bloquearse" (la placa no manda su diagnóstico). Pendiente:
el último paso del embudo ("el aparato ya usa el bloqueador") no se cuenta: la comprobación
vive en el panel de la casa, que no manda nada; y la gráfica por aparato (abajo).

Misma regla de privacidad que los reportes: nada que identifique a una persona o una casa, sin
cookies (así no hace falta el banner de cookies) y todo lo de las placas es **opcional y apagado
por defecto**.

**En la página pública (lo ve cualquiera):**
- **Visitas**: con Cloudflare Web Analytics (gratis, sin cookies): visitas por día, países,
  navegador y sistema. El navegador importa: dice cuánta gente llega con Firefox o Safari y no
  puede instalar.
- **Placas activas**: cuántas placas consultaron `version.json` (idea 10) en los últimos 7 días, y
  qué versión tienen. Se cuenta por el número de instalación al azar; no se guarda la IP.
- **Anuncios bloqueados entre todos**: suma semanal de lo que mandan las placas que lo activaron.
- **Mapa por país**, mostrando un país solo si tiene 5 placas o más, para que nadie sea
  reconocible.
- La tabla de proveedores y módems (2.2).

**Solo para el dueño (detrás de Cloudflare Access):**
- Decidido: se protege con **Cloudflare Access**, solo con el correo del dueño (código por correo,
  sin contraseña nueva). Duración de la sesión: **30 días**.
- **Dónde se atora la gente** (embudo): entró a la página → conectó la placa → instaló → dio el
  WiFi → abrió el panel → el aparato ya usa el bloqueador. Cada paso es un contador sin cookies.
  Es la gráfica más útil para cumplir "nadie se queda atorado".
- **Qué falla más** en el botón "algo no funciona" (6).
- **Dominios más reportados** como "no debió bloquearse" (8), para revisar la lista común.
- Qué guías de módem faltan (los "otro / no lo encontré" de 2.2).

**En el panel de cada placa (privado, no sale de la casa):**
- ✅ (fase 2) Gráfica de consultas y bloqueadas por hora en las últimas 24 h y por día en los
  últimos 7 días. Se guardan 168 horas UTC (1,3 KB) y el navegador las agrupa en la hora local;
  se escriben en `/history.bin` cada hora.
- ✅ (fase 2) Los 10 dominios más bloqueados (conteo aproximado con una tabla chica fija).
- [ ] Por aparato, con los nombres de 3.1 (los nombres ya están desde la fase 5; falta la
  gráfica: guardar historia por aparato gasta RAM y flash).
- Las gráficas se dibujan en el navegador con SVG a mano, sin librerías, para que la página siga
  cabiendo en la placa y pase la CSP actual.

## 3.4 Contáctanos ✅ (fase 3)

**Estado:** ✅ en la fase 3: `site/contacto.html` → Worker → correo a `contacto@s-dev.win` con
`Reply-To`, asunto fijo y texto sin HTML; respuesta automática fija (apagable); Turnstile, límite
por IP sin guardarla, 200 mensajes al día; diagnóstico del panel en el fragmento `#diag=`, solo si
la persona lo adjunta. Pendiente: configurar Email Routing, Email Sending, SPF, DKIM y DMARC en
Cloudflare (pasos a mano del autor en `cloud/DEPLOY.md`; falta verificar si se puede mandar a una
dirección de Email Routing).

Para cuando alguien se atora con algo que la página y el botón "algo no funciona" no resuelven.

- Formulario **"Contáctanos"** en la página y una liga desde el panel de la placa.
- Los mensajes llegan a **contacto@s-dev.win**, que reenvía al correo personal del dueño con
  Cloudflare Email Routing. El correo personal **no va en el repo ni en la página**; solo se
  configura en Cloudflare.
- Flujo: formulario → Cloudflare Worker → correo a `contacto@s-dev.win` (binding `send_email` o
  Cloudflare Email Service) con `Reply-To` = correo de la persona, para contestarle con "Responder".
- **Respuesta automática** a la persona desde `contacto@s-dev.win`: "Recibimos tu mensaje, te
  contestamos pronto", con las ligas a las guías más comunes. Va con el trato de familia.
- Campos: nombre (opcional), correo, tema con opciones fijas (no instala, no bloquea, se bloqueó
  algo que no debía, mi módem, otra cosa) y mensaje. Aquí sí hay texto libre, porque no se
  publica: solo lo lee el dueño.
- **Diagnóstico adjunto, si la persona acepta**: desde el panel, el resultado de "algo no
  funciona", la versión del firmware, la RAM libre y el último error de la lista. Ahorra el ida y
  vuelta de "¿qué versión tienes?". Nada de IPs, nombres de aparatos ni dominios visitados.
- Protección: Turnstile, límite de mensajes por IP en el Worker (sin guardarla), tamaño máximo del
  mensaje y quitar HTML del texto antes de mandarlo.
- Configurar SPF, DKIM y DMARC de `s-dev.win` para que las respuestas automáticas no caigan en spam.
- Los temas de los mensajes alimentan la gráfica "qué falla más" del panel del dueño (3.3), solo
  como conteo.

## 3.5 Más ideas

1. **Panel de muestra** en la página, con datos de ejemplo, para ver qué tendrás antes de comprar.
2. **Video corto** (~2 min) de la instalación, de conectar la placa al primer anuncio bloqueado.
   **Pendiente (fuera de la fase 5):** necesita la placa real y grabar la instalación (§5).
3. ✅ (fase 5) **Cuánto te ahorra**: "este mes tu casa se ahorró ~800 MB de anuncios" (estimado por consulta
   bloqueada).
   Hecho: "Aproximado: en 7 días tu casa se ahorró unos N MB de anuncios" (`ahorroMb` de
   `/api/history`). **Pendiente:** los **30 KB por anuncio no tienen fuente** (falta verificar);
   no hay "este mes" porque la historia guarda 7 días.
4. ✅ (fase 5) **Lista de anuncios en español** además de la general (p. ej. la regional de Hagezi o EasyList
   Spanish), para los sitios latinos. Revisar que quepa en `MAX_DOMAINS`.
   Hecho: casilla "Quitar algunos anuncios más de sitios en español (por ahora son pocos)" (EasyList Spanish; el texto avisa que suma poco), apagada de
   fábrica, que se baja después de la de estafas y antes de la general. **Pendiente (falta verificar):** de sus ~2700 líneas solo 23 son
   dominios que un DNS puede usar; conviene buscar una lista regional en formato de dominios, y
   revisar dirección, tamaño y licencia.
5. ✅ (fase 5) **Página en español e inglés.**
   Hecho: `site/en/` (inicio y privacidad) y textos del instalador en los dos idiomas; cada
   página enlaza al otro idioma. **Pendiente:** las guías (router, FortiGate, obstáculos) siguen
   **solo en español** (la versión en inglés lo dice); el panel de la placa también.
6. ✅ (fase 5) **Respaldo de la configuración**: bajar y subir un archivo con reglas, nombres de aparatos y
   horarios. Sirve para cambiar de placa y para la placa de respaldo (idea 11).
   Hecho: "Bajar respaldo" / "Subir respaldo" en el panel; texto `SIBLOCK-RESPALDO 1` sin WiFi ni
   contraseñas; se sube en pedazos de 3 KB (tope 24 KB), se revisa entero antes de tocar nada y se
   aplica tras un "Sí, reemplazar" (si la flash falla a medias, el panel dice qué no se guardó). **Pendiente:** la copia automática entre dos placas (idea 11).
7. ✅ (fase 2) **Panel cómodo en el celular**: letra grande, modo oscuro, botones grandes.
8. **Licencia**, si el código se hace público.
9. ✅ (fase 5) **Botón opcional de "invítame un café".**
   Hecho: "Invítame un café" / "Buy me a coffee" en el pie de la página, oculto salvo que CI ponga
   un enlace `https://` (variable `SIBLOCK_APOYO_URL` del repo). Sin ligas de afiliado ni
   contadores. **Pendiente:** que el autor elija el sitio y ponga la variable.

## 4. Lo que quedó de la revisión de robustez

- Mientras se actualiza la lista (1 a 2 min) el panel no responde. Se podría mover la descarga a
  su propia tarea.
- Los contadores entre tareas pueden perder alguna cuenta. Es cosmético.
- `/list.bin` no tiene suma de comprobación (CRC).
- Panel por HTTP. Riesgo bajo aceptado (ver `seguridad.md`).

## 5. Cuando llegue la placa

- Lista de comprobación de `pruebas.md`, sección 3 (incluida la de la fase 2: comprobación en un
  celular real, Firefox, iPhone con Private Relay, Android con DNS privado, gráficas tras un
  reinicio).
- Medir la RAM real, la duración de la descarga y si la autoprueba del puerto 53 funciona.
- Decidir el DHCP en el ESP32 con la RAM libre real medida.
- Probar el asistente de router contra el FortiGate del laboratorio (2.4) y medir la RAM del TLS.
- Probar la instalación por la página en Windows, Mac y Linux, con placas de chip USB CH340 y
  CP2102.
- *(Fase 5)* Tabla ARP con más de 10 aparatos, LED real, tamaño de las listas nuevas, RAM al
  bajar las tres listas y respaldo entre dos placas (`docs/pruebas.md`, "Fase 5").
- *(Fase 3)* Probar los certificados de Cloudflare en el ESP32 (aviso diario y consulta de
  versión) y la RAM durante el TLS, con el Worker ya publicado (`docs/pruebas.md` §3).

## Frentes de trabajo para los agentes

Así se podría repartir después:

| Frente | Qué incluye | Skill o agente sugerido |
|---|---|---|
| Firmware | NVS, Improv, portal, primer uso, borrado, DHCP | `esp32`, `ecc:cpp-reviewer` |
| Simulador | Pruebas nuevas para portal, NVS y primer uso | `superpowers:test-driven-development` |
| CI y publicación | GitHub Actions, `manifest.json`, releases | `ecc:github-ops` |
| Página y hosting | Página de instalación, guías por marca y proveedor, Cloudflare o GitHub Pages | `frontend-design`, `cloudflare:*` |
| Aprendizaje de módems | Worker + D1 de reportes, detección de ASN, OUI en el firmware, tabla pública | `cloudflare:workers-best-practices`, `esp32` |
| Uso diario | Nombres, pausa por aparato, listas de estafas, horarios, LED | `esp32`, `superpowers:test-driven-development` |
| Acompañamiento | "Algo no funciona", aviso de DoH, reportes de bloqueos, Telegram, versión, dos placas | `esp32`, `cloudflare:*` |
| Gráficas | Web Analytics, contadores del Worker, embudo, panel de dueño con Access, gráficas del panel | `cloudflare:*`, `dataviz`, `esp32` |
| Router automático | Asistente en el panel, API de FortiGate/MikroTik/OpenWrt, deshacer solo, pruebas en el laboratorio | `esp32`, `security-suite` |
| Contáctanos | Formulario, Worker, Email Routing, respuesta automática, SPF/DKIM/DMARC | `cloudflare:cloudflare-email-service`, `security-suite` |
| Cajita 3D | Modelo imprimible | FreeCAD (MCP) |
| Seguridad | Portal, primer uso, borrado, Worker de reportes, token de Telegram | `security-suite` |
| Documentación | Guía para no programadores, actualizar `docs/` | `engineering:documentation` |

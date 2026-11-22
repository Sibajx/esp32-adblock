# API HTTP del panel (y del Worker)

Base: `http://<ip-de-siblock>/` (o `http://siblock.local/`), en el puerto 80. Las respuestas son
JSON con `Cache-Control: no-store`.

*(Fase 3)* Hay una segunda API, la del Worker `siblock-api` en internet
(`https://siblock.s-dev.win/api/v1/…`), que recibe los reportes y da las estadísticas. Está al
final: [Worker `siblock-api`](#worker-siblock-api-fase-3).

> **Estado (fase 1):** todo lo de esta página está revisado contra el código (`src/web.cpp`,
> `src/portal.cpp`, `src/improv_serial.cpp`) y lo prueban `sim/test_sim.py` (secciones 12 a 15)
> y `test_host/test_siblock.cpp`.
>
> **Estado (fase 2):** lo marcado *(fase 2)* está revisado contra el código (`src/web.cpp`:
> `handleStats`, `handleDetail`, `handleProbeGif`, `handleDiagnose`, `handleHistory`,
> `handleOptionsGet`, `handleOptionsPost`) y lo prueban `sim/test_sim.py` (secciones 5b y 5c) y
> `test_host`. El plan de origen es
> [`superpowers/plans/2026-11-21-siblock-fase-2.md`](superpowers/plans/2026-11-21-siblock-fase-2.md).
>
> **Estado (fase 3):** lo marcado *(fase 3)* sale del contrato del plan
> [`superpowers/plans/2026-11-21-siblock-fase-3.md`](superpowers/plans/2026-11-21-siblock-fase-3.md)
> y está revisado contra el código (`src/web.cpp`: `handleCloudGet`, `handleCloudPost`,
> `handleReportModem`, `handleReportBlocked`; `src/main.cpp`: `watchCloud`; `cloud/src/*.ts`). Lo
> prueban `sim/test_sim.py` (sección 18 a 18d, con un Worker de mentira), `test_host` y
> `cd cloud && npm test`. El Worker todavía **no está publicado** (lo publica el autor con
> [`cloud/DEPLOY.md`](../cloud/DEPLOY.md)).
>
> **Estado (fase 5):** lo marcado *(fase 5)* sale del contrato del plan
> [`superpowers/plans/2026-11-21-siblock-fase-5.md`](superpowers/plans/2026-11-21-siblock-fase-5.md)
> (tareas 5 a 8). Lo prueban `sim/test_sim.py` (secciones 19 a 23) y `test_host` (`testFamily`,
> `testBackup`, `testLists`, `testStatusLed`, `testSavings`). Los mensajes de error exactos están
> en `src/web.cpp`; aquí van los códigos y el sentido. Falta verificar con la placa la tabla ARP con
> más de 10 aparatos y la RAM al bajar las listas nuevas ([pruebas.md](pruebas.md#fase-5-diseño-53)).

## Reglas comunes

- **Host**: todas las rutas exigen que el header `Host` sea la IP, `siblock`, `siblock.local`,
  `adblock`, `adblock.local` (los dos últimos, nombres de antes: se aceptan si llegan, pero por
  mDNS la placa solo anuncia `siblock.local`; `adblock.local` sirve solo si el router o el
  archivo hosts lo resuelven) o
  `PANEL_EXTRA_HOST` (se ignora el puerto). Si no, responde **403**. En el modo de configuración
  (portal) la regla cambia: ver [Modo de configuración](#modo-de-configuración-portal). Única
  excepción *(fase 2)*: la imagen de prueba
  [`/siblock-prueba.gif`](#get-siblock-pruebagif-fase-2) también acepta un `Host` de prueba.
- **Privadas** (🔒) piden, en este orden:
  1. el header `X-Requested-With: adblock` (si no, **403**). El valor no cambia con la marca
     SiBlock, para no romper scripts;
  2. que ya exista la contraseña del panel en la placa. Si no, **403**
     `{"ok":false,"error":"primero crea tu contraseña"}`;
  3. un token válido en `X-Token`, **o** `Authorization: Basic base64(admin:<contraseña>)`.
     Sin ninguno de los dos responde **401** "inicia sesión".
- **Login Basic**: se decodifica el base64 y se parte en el **primer** `:`. El usuario es `admin`
  y todo lo que sigue es la contraseña (puede llevar `:` y acentos). La contraseña se compara
  contra su hash PBKDF2 guardado en la placa, nunca en texto.
- **Bloqueo por IP**: 5 contraseñas malas seguidas bloquean esa IP 30 s, y la espera se dobla con
  cada bloqueo hasta 16 min. Mientras dure, responde **429**. Si las 16 casillas de la tabla están
  ocupadas por IPs bloqueadas, las IPs sin casilla comparten un turno de un intento cada 5 s
  (**429** "espera unos segundos"). Las 4 últimas IPs que entraron bien no esperan ese turno.
  `POST /api/password` con la contraseña actual mala también cuenta.
- **POST**: el cuerpo va como `application/x-www-form-urlencoded`. Se guardan como mucho 512 B; si
  llega más, responde **413**. Un carácter nulo (crudo o como `%00`) responde **400**: cortaría
  el valor al guardarlo. El servidor cierra la conexión sin contestar en estos casos:
  multipart, `Content-Length` de más de 4 KB, líneas de más de 1024 B, o una petición que tarda
  más de 8 s.
- **Contraseña del panel**: de 12 a 64 **bytes** (una `ñ` cuenta 2) y distinta de
  `CAMBIA_ESTA_CLAVE`.
- Los errores tienen la forma `{"ok":false,"error":"<mensaje en español>"}`.

## Rutas

### `GET /`
La página del panel. Lleva estos headers: CSP `default-src 'none'` (solo inline),
`frame-ancestors 'none'`, `nosniff` y `Referrer-Policy: no-referrer`. *(Fase 2)* La CSP suma
`img-src *.prueba.siblock.internal:*`, lo justo para cargar la imagen de prueba.

Según `/api/stats` muestra:

| `passwordSet` | `setupOpen` | La página muestra |
|---|---|---|
| `false` | `true` | "Crea tu contraseña" (dos campos y "Guardar") |
| `false` | `false` | "Pulsa el botón BOOT de tu SiBlock una vez y vuelve a intentar" |
| `true` | — | Contadores y el login de siempre; con sesión, "Cambiar contraseña" y la contraseña de respaldo |

### `GET /api/stats` (pública)
Solo contadores y el estado de la placa; no dice qué visitó cada aparato.

```json
{"version":"v1.0.0","queries":3031,"blocked":1133,"forwarded":1700,"answered":1690,"dropped":2,
 "timeouts":1,"cached":190,"tcp":3,"rebind":1,"cacheEntries":42,"domains":161266,"uptime":3600,
 "heap":182000,"updating":false,"paused":0,"skipped":0,"passwordSet":true,"setupOpen":false,
 "portal":false,"upstream":"1.1.1.1","error":""}
```

| Campo | Qué es |
|---|---|
| `version` | `SIBLOCK_VERSION`: `vX.Y.Z` en lo que publica CI, `dev` al compilar a mano |
| `passwordSet` | Ya hay contraseña del panel (sal, iteraciones y hash completos) |
| `setupOpen` | No hay contraseña **y** la ventana de creación está abierta |
| `portal` | El modo de configuración por WiFi está abierto |
| `paused` | Segundos que le quedan a la pausa |
| `skipped` | Dominios que quedaron fuera por el tope |
| `error` | Último error de actualización (vacío si no hubo) |
| `canary` *(fase 2)* | Veces que se contestó NXDOMAIN a `use-application-dns.net` (canario de Firefox) |
| `relay` *(fase 2)* | Veces que se contestó NXDOMAIN a `mask.icloud.com` / `mask-h2.icloud.com` (opción de Private Relay encendida) |
| `blocked` | Bloqueadas por la lista o por tus reglas (anuncios). No incluye lo que corta un horario |
| `schedule` *(fase 5)* | Consultas que cortó un horario (van aparte de `blocked`) |

### `POST /api/setup` (crear la contraseña en el primer uso)
Pide `Host` válido y `X-Requested-With: adblock`, pero no sesión. Campo: `password`.

| Respuesta | Cuándo |
|---|---|
| **200** `{"ok":true,"token":"<32 hex>"}` | Se guardó; el token sirve igual que el de `/api/login` |
| **409** `"ya hay contraseña"` | Ya existe una (para cambiarla, `/api/password`) |
| **403** `"Pulsa el botón BOOT de tu SiBlock una vez y vuelve a intentar"` | La ventana de creación está cerrada |
| **400** `"la contraseña debe tener entre 12 y 64 caracteres"` | Largo fuera de rango o la de ejemplo |
| **403** `"petición no permitida"` | Sin `X-Requested-With` |
| **500** `"no se pudo guardar; prueba otra vez"` | La memoria de la placa no aceptó la escritura |

La **ventana de creación** dura 30 min desde el arranque, o 30 min desde una pulsación corta
(< 2 s) del botón BOOT. Ver [seguridad.md](seguridad.md).

### `POST /api/password` 🔒 (cambiar la contraseña)
Campos: `current`, `new`.

- **401** `"la contraseña actual no es correcta"` si `current` está mal (cuenta para el bloqueo
  por IP).
- **400** si `new` no es válida.
- **500** si no se pudo guardar. La contraseña de antes sigue valiendo: la nueva se escribe
  aparte y solo reemplaza a la vieja cuando está completa (un corte de luz a mitad no deja el
  panel sin contraseña).
- **200** `{"ok":true,"token":"<32 hex>"}`: guarda la nueva, **cierra todas las sesiones** y
  devuelve un token nuevo.

### `GET /api/backup-ap` 🔒 (contraseña de respaldo)
`{"ssid":"SiBlock-0C3F","pass":"<10 caracteres>"}`: nombre y contraseña de la red del modo de
configuración. La contraseña usa el alfabeto `abcdefghjkmnpqrstuvwxyz23456789` (sin `i l o 0 1`).

### `GET /api/detail` 🔒
```json
{"recent":[{"d":"ads.example.com","v":"lista","c":"192.168.1.23"}],
 "clients":[{"ip":"192.168.1.23","q":812,"b":301,"ago":4}]}
```
`v` puede ser `lista`, `tuyo` (regla propia), `permitido`, `pausa` u `ok`; *(fase 5)* también
`horario` (lo bloqueó un horario) y `pausa-aparato` (la lista lo habría bloqueado, pero ese
aparato está en pausa). `recent` trae hasta 20
consultas, de la más nueva a la más vieja. `clients` trae los 10 aparatos que más preguntan;
`ago` son los segundos desde su última consulta. Viven solo en RAM.

*(Fase 2)* Campo nuevo `bypass`: aparatos que se saltan SiBlock (hasta 16, el más reciente
primero, se olvidan a las 24 h, solo en RAM):

```json
"bypass":[{"ip":"192.168.1.20","k":["doh","relay"],"ago":120}]
```

`k` dice qué usa: `doh` (preguntó por un DNS cifrado conocido, ver
[dns.md](dns.md#2b-nombres-especiales-fase-2)) y/o `relay` (preguntó por iCloud Private Relay con
la opción apagada). `ago` son los segundos desde que se vio por última vez.

### `GET /api/rules` 🔒
`{"blocked":["example.org"],"allowed":["plex.direct"]}`

### `POST /api/rules` 🔒
Campos: `action` = `block` | `allow` | `remove`, y `domain`.
- 200 `{"ok":true}`; además vacía la caché DNS.
- 400 si el dominio no es válido, si la regla no existía, si ya hay 200 reglas o si la acción no
  existe.

### `POST /api/pause` 🔒
Campo: `minutes`, de 0 a 1440. Con 0 se reanuda. Responde 200, o 400 si está fuera de rango.

### `POST /api/update` 🔒
Pide bajar la lista ahora. Responde **202**. El progreso se ve en `updating` de `/api/stats`.
Mientras dura, el panel no responde.

### `POST /api/login` 🔒 (con Basic)
Cambia la contraseña por un token: `{"ok":true,"token":"<32 hex>"}`. El token es de 128 bits
(`esp_random`), vence a las 12 h y hay 4 sesiones a la vez; si se abre una quinta, sale la más
vieja.

### `POST /api/logout` 🔒
Invalida el token que va en `X-Token`.

### `GET /siblock-prueba.gif` *(fase 2)*
La imagen de la comprobación "¿este aparato usa SiBlock?". El panel la carga desde
`http://<n>.prueba.siblock.internal[:puerto]/siblock-prueba.gif`, con `n` = 12 letras o números
al azar; ese nombre solo lo contesta SiBlock (con su propia IP en la red de la casa; si todavía
no tiene, con la del AP; si no tiene ninguna, contesta SERVFAIL, la imagen no carga y el panel
muestra "❌ Este aparato todavía no usa SiBlock."), así que si la imagen carga, el aparato está
usando SiBlock.

- No pide sesión ni `X-Requested-With`.
- Responde **200** con un GIF transparente de 1×1 fijo (43 B), `Content-Type: image/gif`,
  `Cache-Control: no-store` y `X-Content-Type-Options: nosniff`, si el `Host` es uno de prueba o uno de los permitidos de siempre.
- `Host` de prueba = `<etiquetas>.prueba.siblock.internal` con `:puerto` opcional (solo dígitos),
  sin importar mayúsculas; cada etiqueta de 1 a 63 caracteres `[a-z0-9-]`. No valen, por
  ejemplo, `prueba.siblock.internal` (sin etiqueta), `x.prueba.siblock.internal.evil.com`,
  `xprueba.siblock.internal`, `a.prueba.siblock.internal.` (punto final) ni `a_b.…`.
- Con otro `Host` → **403**. Cualquier **otra** ruta con un `Host` de prueba también → **403**.

### `GET /api/diagnose?probe=ok|fail` *(fase 2)*
El botón "Algo no funciona". **Pública** (sin contraseña, para que la use cualquiera de la casa),
pero pide `Host` válido y `X-Requested-With: adblock` (si falta → **403**). `probe` es el
resultado de la imagen de prueba en ese aparato: `ok`, `fail`, u otro valor / nada (no se probó).

Habla **solo del aparato que pregunta** (por su IP) y del estado general que ya muestra
`/api/stats`. No trae dominios, IPs ni nada de otros aparatos.

```json
{"findings":[{"code":"no_usa_siblock","level":"bad"},{"code":"ipv6","level":"info"}]}
```

`level` es `bad`, `warn`, `info` u `ok`, y la lista viene en ese orden (los más graves primero).
Nunca viene vacía. Códigos (lista cerrada; el panel pone el texto):

| `code` | `level` | Cuándo | Lo que dice el panel |
|---|---|---|---|
| `usa_siblock` | ok | La imagen de prueba cargó | Este aparato usa SiBlock. |
| `navegador_dns_propio` | warn | La imagen falló, pero el aparato sí preguntó a SiBlock en los últimos 10 min | Puede ser que el navegador use su propio DNS ("DNS seguro": apágalo) o que el módem o router reparta otro DNS por IPv6 |
| `no_usa_siblock` | bad | La imagen falló y el aparato no preguntó nada a SiBlock | No pregunta a SiBlock: revisa que el módem o router dé la dirección de SiBlock como DNS y reconecta el WiFi |
| `ipv6` | info | Siempre junto con `no_usa_siblock` o `navegador_dns_propio` (la imagen falló) | Apaga el DNS por IPv6 del módem o router |
| `sin_consultas` | warn | No se probó la imagen y el aparato no preguntó nada en 10 min | No ha preguntado nada a SiBlock en 10 min |
| `dns_cifrado` | warn | El aparato preguntó por un DNS cifrado conocido (últimas 24 h) | Usa un DNS cifrado: apaga "DNS privado" o "DNS seguro" |
| `private_relay` | warn | El aparato preguntó por Private Relay y la opción está apagada | Usa iCloud Private Relay: apágalo en el aparato, o quien tenga la contraseña activa la opción |
| `en_pausa` | warn | La pausa está activa | SiBlock está en pausa |
| `aparato_en_pausa` *(fase 5)* | warn | Este aparato tiene su propia pausa activa | Tiene una pausa propia: ve anuncios hasta que termine; se quita en «Aparatos» |
| `horario_todo` *(fase 5)* | warn | Un horario activo ahora le corta «Todo internet» a este aparato | Un horario le corta todo internet; se cambia en «Horarios» |
| `horario_activo` *(fase 5)* | info | Un horario activo ahora le corta redes sociales o juegos (sin «Todo internet») | Si lo que no abre es de eso, es por el horario |
| `sin_lista` | bad | Todavía no hay lista cargada | SiBlock todavía no tiene su lista |
| `sin_internet` | bad | El DNS de arriba dejó de contestar 3 veces seguidas (la última hace menos de 5 min) y no ha vuelto | SiBlock no logra hablar con internet |
| `router_dns` | warn | La puerta de enlace hizo el 80 % o más de al menos 50 consultas | Todo llega desde tu módem o router (mesh o router que hace de DNS): que reparta la dirección de SiBlock directo |
| `todo_bien` | ok | No salió ningún `bad` ni `warn` | No encontré nada raro. |

Los textos completos están en `src/web_page.h`. `no_usa_siblock`, `ipv6` y `router_dns` llevan
el enlace "Ver la guía" a `https://siblock.s-dev.win/guia-router.html`; `navegador_dns_propio`,
`dns_cifrado` y `private_relay`, el enlace "Ver qué hacer" a `https://siblock.s-dev.win/obstaculos.html`
(que dice cómo apagar el "DNS seguro" de cada navegador y el "DNS privado" de Android).

### `GET /api/history` 🔒 *(fase 2)*
Datos de las gráficas y del top 10. Pide sesión porque las horas dicen cuándo hay gente en casa y
los dominios, qué se visita.

```json
{"clock":true,"hour":492000,
 "h":[[120,30],[98,22],"… 168 pares …"],
 "top":[{"d":"doubleclick.net","n":412},{"d":"ads.example.com","n":97}]}
```

| Campo | Qué es |
|---|---|
| `clock` | `true` si la placa ya tiene la hora de internet |
| `hour` | La hora actual en UTC, contada como `epoch / 3600` |
| `h` | 168 pares `[consultas, bloqueadas]`, uno por hora (7 días), del más viejo al más nuevo; el último es la hora `hour` |
| `top` | Hasta 10 dominios más bloqueados (`d`, cortado a 63 bytes y escapado en JSON) con su cuenta aproximada `n`, de mayor a menor |

Sin hora: `"clock":false`, `"hour":0` y `h` en ceros (el panel dice "Todavía no hay datos"). El
navegador arma "24 horas" (los últimos 24 pares) y "7 días" (agrupando por día en su hora local).

*(Fase 5)* Campo nuevo `ahorroMb`: entero ≥ 0, los MB que se ahorró la casa en las 168 horas de
`h`: `bloqueadas × 30 KB / 1 048 576`, redondeado hacia abajo (`siblock::savedBytes`). Los 30 KB
por consulta bloqueada son un **estimado sin fuente** (falta verificar). El panel dice
"Aproximado: en 7 días tu casa se ahorró unos N MB de anuncios" y no lo muestra con `0`.

### `GET /api/options` 🔒 *(fase 2)*
`{"relayBlock":false}`: si la opción "Apagar iCloud Private Relay en esta casa" está encendida.

*(Fase 5)* Suma las listas elegidas:

```json
{"relayBlock":false,"lists":1,"listsOffer":7,"skipped":0}
```

| Campo | Qué es |
|---|---|
| `lists` | Listas elegidas, en bits: `1` = general (siempre), `2` = estafas y tiendas falsas (no virus), `4` = algunos anuncios en español. Vale `1`, `3`, `5` o `7` (NVS `lists`; si falta o no es válido, `1`) |
| `listsOffer` | Las que este firmware puede ofrecer (`siblock::listsOffered()`); una lista sin dirección no se ofrece |
| `skipped` | Dominios que no cupieron en la última actualización (tope `MAX_DOMAINS`) |

### `POST /api/options` 🔒 *(fase 2)*
Campo: `relayBlock` = `0` o `1`. *(Fase 5)* O `lists` = `1`, `3`, `5` o `7`: se guarda en la NVS
y se pide bajar la lista otra vez (la nueva puede quedar más chica que la mitad: no la frena la
protección contra listas cortadas). `lists` par (`2`, sin la general), `0`, mayor que `7` o vacío →
**400** sin cambiar nada.

- **200** `{"ok":true,"relayBlock":true}` (o `false`): se guarda en la placa (clave `relay_block`
  de la NVS) y se aplica al momento. `relayBlock` es el valor que quedó.
- **400** `{"ok":false,"error":"valor inválido"}` con otro valor (`2`, vacío, nada); no cambia nada.
- **500** `{"ok":false,"error":"no se pudo guardar; prueba otra vez"}` si la NVS no lo guardó; no
  se aplica.
- Sin sesión → **401**; sin `X-Requested-With` → **403**.

Está **apagada de fábrica**, y "borrar la configuración" (BOOT ≥ 10 s) la vuelve a apagar.

### `GET /api/cloud` 🔒 *(fase 3)*
El estado de "Compartir con SiBlock". Las dos opciones están **apagadas de fábrica**.

```json
{"share":false,"versionCheck":false,"installId":"<32 hex>","current":"v1.2.0",
 "latest":"","update":false,"lastPing":"never"}
```

| Campo | Qué es |
|---|---|
| `share` | "Compartir datos anónimos": aviso diario y reportes desde el panel (NVS `cloud_share`) |
| `versionCheck` | "Avisarme de versiones nuevas" (NVS `ver_check`) |
| `installId` | Número de instalación: 16 bytes al azar (`esp_random`) en 32 hex minúsculas (NVS `install_id`). Se crea la primera vez que hace falta y cambia al borrar la configuración. No sale de la placa con `share` apagado |
| `current` | `SIBLOCK_VERSION` (`vX.Y.Z` o `dev`) |
| `latest` | La última versión publicada que vio la consulta de versión; `""` si todavía no hay o si `versionCheck` está apagado. Solo en RAM |
| `update` | `true` si `latest` y `current` son `vX.Y.Z` y `latest` es más nueva (con `dev` siempre `false`) |
| `lastPing` | Cómo salió el último aviso diario: `never`, `ok` o `fail`. Solo en RAM |

En la NVS solo cuenta como encendido el valor `1`: si la clave falta, vale `2` o no se puede leer,
la opción está apagada y no sale nada.

### `POST /api/cloud` 🔒 *(fase 3)*
Campos: `share` y/o `versionCheck`, cada uno `0` o `1`.

- **200** con el mismo JSON de `GET /api/cloud` (ya con lo que quedó).
- **400** `{"ok":false,"error":"datos"}` si no viene ninguno de los dos o alguno no es `0`/`1`
  (`share=2`, vacío): no cambia **nada**.
- **500** `{"ok":false,"error":"guardar"}` si la NVS no lo guardó.
- Sin sesión → **401**; sin `X-Requested-With` → **403**. Cuerpo con el tope de 512 B (413).
- Apagar `versionCheck` borra `latest` de la RAM (no se avisa de nada con la opción apagada).

Encender una opción arranca su calendario (`siblock::CloudSchedule`, en `main.cpp`):

| Opción | Qué sale | Cuándo |
|---|---|---|
| `share` | `POST https://siblock.s-dev.win/api/v1/placa` con `{"i":"<installId>","v":"<versión>","b":<bloqueadas en 24 h>}` y nada más | Al minuto de encenderla (o de arrancar con ella encendida) y luego cada 24 h + hasta 1 h al azar. Solo con la hora de internet (si no, espera). Si falla: a la 1, 2, 4… h, hasta 24 h |
| `versionCheck` | `GET https://siblock.s-dev.win/version.json`, **sin cuerpo ni número**; lee como mucho 1024 B | Igual: al minuto y luego cada 24 h + hasta 1 h al azar |

Nada sale mientras se baja la lista, sin WiFi, ni con la opción apagada. Las dos peticiones llevan
`User-Agent: SiBlock/<versión>`, tienen 8 s de tope, validan el certificado con las raíces de
`certs.h` y no siguen redirecciones. Por la serie sale `[nube] aviso diario: ok|falló` y
`[nube] versión publicada: vX.Y.Z|no se pudo leer`.

### `POST /api/report/modem` 🔒 *(fase 3)*
Campos: `fabricante`, `dnsOk` (`0` o `1`) y `menu`, con los valores de las
[listas fijas](#listas-fijas-fase-3). La placa arma el cuerpo y lo manda a
`POST /api/v1/reporte/modem` del Worker:

```json
{"i":"<installId>","origen":"panel","fabricante":"huawei","dns_ok":true,"menu":"internet_wan"}
```

### `POST /api/report/blocked` 🔒 *(fase 3)*
Campos: `domain` y `motivo`. El dominio se limpia (espacios al inicio y al final, minúsculas) y
tiene que ser válido: de 3 a 253 B, 2 etiquetas o más de 1 a 63 caracteres `[a-z0-9-]`, sin `-` al
inicio ni al final de una etiqueta y sin punto final. Va a `POST /api/v1/reporte/bloqueo`:

```json
{"i":"<installId>","origen":"panel","dominio":"ads.ejemplo.com","motivo":"pagina_no_abre"}
```

Respuestas de los dos reportes:

| Respuesta | Cuándo |
|---|---|
| **200** `{"ok":true}` | El Worker lo recibió |
| **409** `{"ok":false,"error":"apagado"}` | "Compartir datos anónimos" está apagado: **no se llama a internet** |
| **400** `{"ok":false,"error":"datos"}` | Algún valor fuera de las listas o dominio malo (`linksys`, `dnsOk=2`, `menu` vacío, `ejemplo`, `motivo=otra`); no sale nada |
| **413** | Cuerpo de más de 512 B; no sale nada |
| **429** `{"ok":false,"error":"ocupado"}` | Ya hay un reporte en camino (uno a la vez) |
| **502** `{"ok":false,"error":"sin_internet"}` | No se pudo llegar al Worker (sin WiFi, sin RAM para TLS, tiempo agotado) |
| **502** `{"ok":false,"error":"limite"}` | El Worker contestó 429 |
| **502** `{"ok":false,"error":"rechazado"}` | El Worker contestó otro 3xx, 4xx o 5xx |

Sin sesión → **401**; sin `X-Requested-With` → **403**.

### Del panel a la página *(fase 3)*
El panel no manda correos ni su diagnóstico a internet. Abre la página con los datos **en el
fragmento** (lo que va después de `#`, que el navegador no manda al servidor):

- «Reportar desde la página» → `https://siblock.s-dev.win/reporte.html#i=<installId>`: así el
  reporte de la página y el del panel cuentan como una sola casa. Si `i` no son 32 hex, la página
  lo ignora y usa un número nuevo.
- «Escríbenos» → `https://siblock.s-dev.win/contacto.html#diag=<base64url de JSON>`, con
  `{"v","heap","listErr","codes":[…]}` (versión, RAM libre, último error de la lista y los códigos
  de "Algo no funciona"); 600 caracteres como mucho (si no cabe, se quitan `codes`). La página
  solo lo adjunta si la persona marca «Adjuntar el diagnóstico de mi SiBlock». Un `#diag=` roto,
  enorme o con claves de más se ignora (o se le quitan esas claves) y nunca se pinta como HTML.

### Aparatos *(fase 5)*

Un aparato se reconoce por su **huella**: los primeros 8 bytes de `SHA-256(sal ‖ MAC)` en 16 hex
minúsculas. La placa saca la MAC de su tabla ARP a partir de la IP que pregunta; la sal son 16 bytes
al azar de cada placa. **Ni la MAC ni la IP se guardan**: la IP de ahora vive solo en RAM. Hay 32
casillas para aparatos con nombre.

#### `GET /api/devices` 🔒

```json
{"devices":[
  {"s":0,"h":"9d2cbeaf31115773","n":"Tele sala","ip":"192.168.1.23","q":812,"b":301,"ago":4,"pausa":0},
  {"s":-1,"h":"","n":"","ip":"192.168.1.40","q":12,"b":3,"ago":60,"pausa":0}],
 "tz":-360,"hora":true}
```

| Campo | Qué es |
|---|---|
| `s` | Casilla 0..31, o `-1` si el aparato no tiene nombre |
| `h` | Huella (16 hex), o `""` si la tabla ARP todavía no da su MAC ("Todavía no lo reconozco") |
| `n` | Nombre (1 a 32 bytes UTF-8, escapado en JSON), o `""` |
| `ip` | IP de ahora (solo RAM), o `""` si no se ha visto desde que arrancó |
| `q`, `b`, `ago` | Consultas, bloqueadas y segundos desde la última consulta (los mismos de `clients` en `/api/detail`). Un aparato con nombre que no se ha visto desde que arrancó viene con `ip:""`, `q:0`, `b:0` y `ago:-1` (el panel muestra "no se ve ahora") |
| `pausa` | Segundos que le quedan a la pausa de ese aparato (`0` = sin pausa) |
| `yo` | `true` solo en la fila del aparato que hizo la petición (el panel muestra "Este aparato"); en las demás filas no viene |
| `tz` | Diferencia con UTC en minutos guardada en la placa (la manda el navegador al guardar un horario) |
| `hora` | `true` si la placa tiene la hora de internet. Sin hora, **los horarios no bloquean** |

Salen los aparatos con nombre (aunque no se vean ahora) y los 10 que más preguntan. Si un aparato
con nombre tiene una IP de ahora, sale en una sola fila.

#### `POST /api/devices` 🔒

| Campos | Qué hace |
|---|---|
| `action=name&h=<16 hex>&name=<texto>` | Le pone nombre (o lo cambia). Ocupa una casilla la primera vez |
| `action=forget&h=<16 hex>` | Borra el nombre, libera la casilla y lo quita de todos los horarios (un horario que se queda sin aparatos se borra) |
| `action=pause&h=<16 hex>&minutes=<0..1440>` | Pausa el bloqueo de anuncios **solo en ese aparato**; `0` la quita. Solo aparatos con nombre. Vive en RAM: un reinicio la quita |

- **200** `{"ok":true}`; los cambios de nombre se guardan en `/familia.txt`.
- **400** (mensaje en español) si: la huella no son 16 hex; `name` con una huella que la placa no
  ha visto en su tabla ni en las IPs de los últimos 5 min (no se pueden inventar aparatos); el
  nombre está vacío, pasa de 32 bytes, no es UTF-8 válido, trae caracteres de control o espacio
  al inicio o al final; ya hay 32 aparatos con nombre; `minutes` fuera de rango; acción que no
  existe.
- Sin sesión → **401**; sin `X-Requested-With` → **403**; cuerpo de más de 512 B → **413**.

Una pausa del aparato quiere decir "sin bloqueo de anuncios": **los horarios siguen valiendo**.

#### `GET /api/schedules` 🔒

```json
{"items":[{"i":0,"dev":"00000001","g":4,"d":127,"ini":1260,"fin":420}],"tz":-360}
```

| Campo | Qué es |
|---|---|
| `i` | Número del horario (0..7; caben 8) |
| `dev` | Casillas de aparatos, en bits (8 hex): `00000001` = casilla 0 |
| `g` | Grupos, en bits: `1` = redes sociales, `2` = juegos, `4` = todo internet |
| `d` | Días, en bits: `1` = lunes … `64` = domingo (`127` = todos) |
| `ini`, `fin` | Minutos del día en la hora de la casa (0..1439). Si `fin` < `ini`, cruza la medianoche y cuenta el día en que empieza |

#### `POST /api/schedules` 🔒

- `action=save&i=<-1..7>&dev=<8 hex>&g=<1..7>&d=<1..127>&ini=<0..1439>&fin=<0..1439>&tz=<-720..840>`:
  `i=-1` crea uno nuevo. `tz` se guarda en cada `save` (el panel manda
  `-new Date().getTimezoneOffset()`).
- `action=delete&i=<0..7>`.
- **400** si: `ini == fin`, días o grupos en 0, `dev` en 0 o con una casilla sin aparato, ya hay 8
  horarios, `i` no existe, `tz` fuera de rango.
- Sin sesión → **401**; sin anti-CSRF → **403**.

Cómo decide el DNS (después de los nombres especiales y antes de la lista):

1. Si no hay horarios ni pausas, no hace nada más (ni mira la tabla ARP).
2. Busca la huella de la IP (caché de 16 IPs, válida 5 min; si no está, pregunta a la tabla ARP).
   **Si no la encuentra, no aplica nada** (falla abierto).
3. Un horario activo para esa casilla y ese grupo → contesta `0.0.0.0` (como un anuncio), `v` =
   `horario`. **No** cuenta en `blocked` (ni en la tarjeta, el %, la columna `b` por aparato, la
   gráfica, el ahorro o el top, que son de anuncios y reglas tuyas): suma en `schedule` de
   `/api/stats`, y el panel lo muestra aparte ("los horarios cortaron N consultas"). Gana sobre
   "Permitir" y vale aunque la casa o el aparato estén en pausa.
4. Si no, un aparato en pausa cuya consulta la lista bloquearía → se reenvía, `v` = `pausa-aparato`.

Grupos fijos (listas cortas escritas a mano en `lib/siblock_core/family.cpp`, falta verificar que
cubran las apps): **redes sociales** = Facebook, Instagram, TikTok, Snapchat, X/Twitter, Threads,
Pinterest, Reddit y Discord; **juegos** = Roblox, Epic Games/Fortnite, Minecraft, Xbox Live,
PlayStation Network, Steam y Supercell; **todo internet** = cualquier dominio. Cuenta el dominio y
sus padres (`www.instagram.com` sí; `instagram.com.otro.net` no).

### Respaldo *(fase 5)*

Un archivo de texto con las reglas propias, las listas elegidas, la opción de Private Relay, la
sal, los aparatos con nombre, los horarios y la hora de la casa. **No lleva** el WiFi, las
contraseñas, el número de instalación, las opciones de la nube, IPs ni MACs escritas tal cual.
**Es un archivo sensible**: con la sal y la huella de cada aparato se puede sacar su MAC probando
una por una ([seguridad.md](seguridad.md#respaldo-subirlo-y-bajarlo)); el panel pide no compartirlo.
Formato:

```
SIBLOCK-RESPALDO 1
listas 3
relay 0
zona -360
sal 000102030405060708090a0b0c0d0e0f
bloquear ads.ejemplo.com
permitir plex.direct
aparato 0 9d2cbeaf31115773 Tele sala
horario 00000001 4 127 1260 420
fin
```

Una entrada por línea (`\n`; se acepta `\r\n`), máximo 300 bytes por línea, 200 reglas y 24 KB
en total; sin líneas vacías ni palabras desconocidas. `/familia.txt` usa el mismo formato con la
cabecera `SIBLOCK-FAMILIA 1` y sin `listas`, `relay`, `bloquear` ni `permitir`
(`lib/siblock_core/backup.*`).

#### `GET /api/backup` 🔒

Contesta `text/plain; charset=utf-8` con `Content-Disposition: attachment;
filename="siblock-respaldo-AAAA-MM-DD.txt"` (la fecha de la placa; sin hora de internet,
`siblock-respaldo.txt`) y `Cache-Control: no-store`.

- **500** `"el respaldo quedó demasiado grande; borra algunas reglas propias y prueba otra vez"` si
  el texto pasa de 24 576 B (el tope que acepta la restauración; pasa con muchas reglas de nombres
  muy largos). Así nunca se baja un respaldo que después no se puede subir.

#### `POST /api/restore/part` 🔒 (header `X-Parte: <0..99>`)

El servidor web no acepta cuerpos de más de 4 KB, así que el archivo se sube **en pedazos** de
hasta 3072 B (`text/plain`), cortados en finales de línea, a `/restore.tmp`.

El número de pedazo va en el header **`X-Parte`** (1 o 2 cifras), no en la URL: esta ruta recibe
el cuerpo en modo raw y en ese modo el servidor no lee los argumentos de la URL.

```sh
curl -X POST http://siblock.local/api/restore/part -H 'X-Requested-With: adblock' \
     -H 'X-Token: <token>' -H 'Content-Type: text/plain' -H 'X-Parte: 0' --data-binary @pedazo0.txt
```

- `X-Parte: 0` empieza de cero; cada pedazo debe ser el siguiente al último (`X-Parte: 2` sin el
  `1`, o sin el header → **400** "falta un pedazo; sube el archivo otra vez").
- Pedazo de más de 3072 B o total de más de 24 576 B → **413** y se borra el temporal.
- Sin sesión o sin anti-CSRF se descarta todo y no se escribe nada (**401** / **403**).

#### `POST /api/restore/check` 🔒

Lee `/restore.tmp` entero sin aplicar nada:

```json
{"ok":true,"reglas":2,"aparatos":1,"horarios":1,"listas":3}
```

o, si algo falla, `{"ok":false,"error":"línea 12: no se entiende","linea":12}` (`linea` = `0` si
falla el archivo entero, por ejemplo `"el archivo está cortado"` cuando no termina en `fin`, o
`"el archivo es demasiado grande"`). Si el archivo no es válido, la respuesta es **400** con ese
JSON; sin archivo subido, **400** "primero sube el archivo".

#### `POST /api/restore/apply` 🔒

Campo `confirm=1`. Vuelve a leer y validar el temporal **entero antes de tocar nada** y después
guarda, en este orden: reglas, aparatos (con horarios y zona), listas y Private Relay; vacía la
caché DNS, borra el temporal y, si cambiaron las listas, pide bajar la lista otra vez. La
validación es todo o nada; **la escritura no**: si la flash falla a medias, contesta 500 y dice
qué no se guardó.

- **200** `{"ok":true}`.
- **400** "falta confirmar" sin `confirm=1`, o el mismo error que `check` si el archivo no es
  válido (la placa no cambia).
- **503** "Intenta otra vez en un momento" si la placa tiene poca memoria seguida (< 40 KB).
- **500** si falla la flash:
  - "no se pudo guardar; prueba otra vez": no cambió nada;
  - "se guardaron las reglas, pero no los aparatos; prueba otra vez";
  - "se guardó casi todo, pero no las opciones; revísalas en el panel" (listas o Private Relay).

## Modo de configuración (portal)

Mientras el portal está abierto, SiBlock también es un punto de acceso WPA2 `SiBlock-XXXX` con IP
`192.168.4.1`, y su DNS contesta **todo** con esa IP a los clientes de la red del AP (para que el
teléfono abra solo la página). Si la red de la casa también usa 192.168.4.x, el AP se abre en la
primera libre de `192.168.243.1`, `172.31.243.1` y `10.243.77.1` (lo elige
`siblock::pickApAddress`); en lo que sigue, "la IP del AP" es esa.

Un cliente cuenta como "de la red del AP" solo si la conexión llegó a la IP del AP y viene de su
/24. Si la placa se conecta después a una casa que choca con esa red, no se confía en nadie: todos
reciben el panel y el DNS normal, y `/api/wifi*` responde **403**.

- A los clientes de la red del AP: si el `Host` no es la IP del AP, `siblock.local` ni uno de los
  permitidos → **302** a `http://<IP del AP>/`. `GET /` → página "Configura el WiFi de SiBlock"
  (lista de redes, contraseña, "Conectar"), con el mismo CSP que el panel.
- A los aparatos de la casa (portal abierto con BOOT y el WiFi conectado): el panel de siempre.
- Fuera del portal, `/api/wifi` y `/api/wifi/scan` responden **404**.

### `GET /api/wifi/scan` (solo en el portal)
Pide `Host` válido y `X-Requested-With: adblock`. Hasta 20 redes, con el nombre escapado en JSON.
Escanea como mucho una vez cada 10 s (el escaneo corta unos segundos al teléfono conectado);
entre medias devuelve el último resultado.

```json
{"networks":[{"ssid":"CasaDemo","rssi":-50,"open":false},
             {"ssid":"Café \"Ñandú\"","rssi":-70,"open":true}]}
```

### `POST /api/wifi` (solo en el portal)
Pide `Host` válido y `X-Requested-With: adblock`. Campos: `ssid` (1 a 32 bytes) y `pass` (vacía
para red abierta, o de 8 a 63 bytes).

- **403** `"esto solo se puede hacer conectado a la red de SiBlock"` si la petición **no viene de
  la red del AP** (ver arriba): nadie en la red de la casa puede reapuntar la placa.
- **400** si el SSID o la contraseña no son válidos.
- **200** `{"ok":true}`: guarda el WiFi; a los 2 s cierra el portal y se conecta.

## Improv Serial (por USB)

Código: `src/improv_serial.cpp` (el parser y los paquetes, en `lib/siblock_core/improv.*`). Es el
protocolo abierto de ESP Web Tools
(https://www.improv-wifi.com/serial/), a 115200 baudios. Las líneas `STATS,…` y los mensajes de
siempre siguen saliendo: Improv tiene su propia cabecera y se ignora el resto.

Paquete: `"IMPROV"` + versión `0x01` + tipo + largo + datos + checksum (suma de todos los bytes
anteriores, mod 256); al enviar se agrega `\n`. Tipos: `0x01` estado, `0x02` error, `0x03`
comando RPC, `0x04` resultado RPC. Los textos van con un byte de largo delante.

| Comando | Datos | Respuesta |
|---|---|---|
| `0x01` WiFi | `[ssid, contraseña]` | Estado `0x03` (conectando); si conecta **a esa red** en ≤ 20 s (se espera al menos 0,5 s y se comprueba el nombre de la red, para no confundirla con la conexión de antes): lo guarda, estado `0x04` y resultado `["http://<ip>/"]`. Si no: error `0x03` y vuelve al WiFi guardado (no guarda los datos malos). Inválido: error `0x01` |
| `0x02` estado actual | — | Estado `0x04` si está conectado (y resultado `["http://<ip>/"]`), si no `0x02` (listo) |
| `0x03` info | — | `["SiBlock", "<versión>", "ESP32", "SiBlock-XXXX"]` |
| `0x04` redes | — | Un resultado por red `[ssid, rssi, "YES"/"NO"]` y uno vacío al final |
| `0xA0` contraseña de respaldo (propio de SiBlock) | — | `["SiBlock-XXXX", "<ap_pass>"]` |
| otro | — | Error `0x02` (comando desconocido) |

Un checksum malo da error `0x01`. Mientras se prueba un WiFi, otro `0x01` da error `0xFF`. Cualquier paquete Improv reinicia la espera de 2 min antes de
abrir el portal cuando no hay WiFi guardado.

Ejemplo: el comando `0xA0` es `49 4d 50 52 4f 56 01 03 02 a0 00 83 0a`.

## Ejemplo con curl

```bash
IP=192.168.1.50; read -rs PASS   # contraseña del panel, sin que quede en el historial
TK=$(curl -s -X POST http://$IP/api/login -H 'X-Requested-With: adblock' -u "admin:$PASS" | jq -r .token)
curl -s http://$IP/api/detail -H 'X-Requested-With: adblock' -H "X-Token: $TK"
curl -s -X POST http://$IP/api/rules -H 'X-Requested-With: adblock' -H "X-Token: $TK" \
     -d action=block -d domain=example.org
curl -s http://$IP/api/history -H 'X-Requested-With: adblock' -H "X-Token: $TK"
curl -s -X POST http://$IP/api/options -H 'X-Requested-With: adblock' -H "X-Token: $TK" -d relayBlock=1
curl -s "http://$IP/api/diagnose?probe=fail" -H 'X-Requested-With: adblock'   # sin contraseña
curl -s http://$IP/api/cloud -H 'X-Requested-With: adblock' -H "X-Token: $TK"   # fase 3: todo apagado de fábrica
curl -s http://$IP/api/devices -H 'X-Requested-With: adblock' -H "X-Token: $TK"  # fase 5
curl -s -X POST http://$IP/api/devices -H 'X-Requested-With: adblock' -H "X-Token: $TK" \
     -d action=name -d h=9d2cbeaf31115773 --data-urlencode 'name=Tele sala'
curl -s -OJ http://$IP/api/backup -H 'X-Requested-With: adblock' -H "X-Token: $TK"  # baja el respaldo
```

Primer uso (dentro de la ventana de 30 min):

```bash
read -rs PASS
curl -s -X POST http://siblock.local/api/setup -H 'X-Requested-With: adblock' \
     --data-urlencode "password=$PASS"
```

## Worker `siblock-api` *(fase 3)*

Código: `cloud/` (TypeScript, Cloudflare Workers + D1). Ruta
`siblock.s-dev.win/api/*`: la misma dirección que la página, así que no hace falta CORS y la CSP
de la página sigue en `'self'`. Pasos para publicarlo: [`cloud/DEPLOY.md`](../cloud/DEPLOY.md)
(los hace el autor a mano; todavía no está publicado).

### Reglas comunes del Worker

- Las respuestas son JSON (`application/json; charset=utf-8`) con `X-Content-Type-Options: nosniff`
  y `Cache-Control: no-store` (salvo `/estadisticas`). Ninguna pone cookies.
- **POST**: el cuerpo es un objeto JSON. Se lee sin pasar del tope de cada ruta (no se confía en
  `Content-Length`); más → **413**. Si no es JSON, no es UTF-8 válido o no es un objeto → **400**.
  Las claves de más se ignoran y no se guardan.
- **Origen**: un POST con header `Origin` distinto de `https://siblock.s-dev.win` → **403**
  `origen`. Sin `Origin` (la placa, `curl`) pasa: el origen no es la defensa principal, solo frena
  que otra página use el navegador de la persona.
- **Límite por IP sin guardarla**: la clave del límite es `<ruta>:` + los primeros 32 hex de
  `SHA-256(ID_PEPPER ‖ IP)`. La IP no se escribe en D1 ni en ningún lado. Si el servicio de
  límites falla, el Worker rechaza (429) en vez de dejar pasar.
- **Número de instalación**: en D1 se guarda `SHA-256(ID_PEPPER ‖ i)` (32 hex), nunca el número tal
  cual. Si falta `ID_PEPPER` o tiene menos de 16 caracteres, todo POST da **500** a propósito.
- **Fechas**: solo el día (días desde 1970 en UTC), nunca la hora.
- **País y proveedor** salen de Cloudflare (`request.cf.country`, `asn`, `asOrganization`, este
  último cortado a 100 caracteres y sin caracteres de control). No se le preguntan a la persona.

Errores (siempre `{"error":"<código>"}`):

| Código HTTP | `error` | Cuándo |
|---|---|---|
| 400 | `datos` | Falta algo, valor fuera de las listas, JSON roto |
| 403 | `origen` | `Origin` de otra página |
| 403 | `turnstile` | Falta Turnstile o no es válido (página y Contáctanos) |
| 403 | `acceso` | Resumen del dueño sin Access válido |
| 404 | `no_existe` | Ruta desconocida |
| 405 | `metodo` | Método que la ruta no acepta (p. ej. `GET /api/v1/contacto`) |
| 413 | `grande` | Cuerpo más grande que el tope de la ruta |
| 429 | `limite` | Límite por IP o tope diario de Contáctanos |
| 500 | `interno` | Error del Worker, de D1 o del correo (sin detalles) |

### Listas fijas *(fase 3)*

Iguales en la placa (`lib/siblock_core/cloud.cpp`), la página (`site/js/`) y el Worker
(`cloud/src/opciones.ts`). Nada de texto libre, salvo el mensaje de Contáctanos (que no se publica).

| Lista | Valores |
|---|---|
| Fabricantes (`fabricante`) | `tp-link` `huawei` `zte` `technicolor` `arris` `netgear` `asus` `mikrotik` `proveedor` (el módem que dio la compañía) `no-se` (mismos `id` que `MARCAS` de `site/js/guias-datos.js`) |
| Menús (`menu`) | `internet_wan` (Internet o WAN) `red_local` (Red local, LAN o DHCP) `avanzado` `otro` (Otro o no lo encontré) |
| Motivos "no debió bloquearse" (`motivo`) | `pagina_no_abre` `app_no_funciona` `juego_no_funciona` `otro` |
| Temas de Contáctanos (`tema`) | `no_instala` `no_bloquea` `bloqueo_de_mas` `mi_modem` `otra` |
| Pasos del embudo (`p`) | `entro` `conecto` `instalo` `wifi` `panel` |

Formatos: `i` = 32 caracteres `[0-9a-f]`; `v` = `vX.Y.Z` (cada parte de 0 a 9999, sin ceros a la izquierda) o `dev`;
`dominio` = la misma regla que [`/api/report/blocked`](#post-apireportblocked--fase-3), ya en
minúsculas (`ADS.ejemplo.com` → 400).

### `POST /api/v1/reporte/modem`
Tope 1024 B. Límite `LIMITE` (5 por minuto) con clave `reporte`.

```json
{"i":"<32 hex>","origen":"pagina","fabricante":"huawei","dns_ok":true,"menu":"internet_wan","turnstile":"<token>"}
```

- `origen` = `pagina` o `panel`. Con `pagina`, `turnstile` es obligatorio (**403** `turnstile`).
  El panel no lo manda (ver [seguridad.md](seguridad.md#reportes-del-panel-sin-firma)).
- `dns_ok` tiene que ser `true`/`false` (no `"sí"`).
- Guarda **una fila por instalación** (`reportes_modem`): un segundo reporte con el mismo `i`
  reemplaza al anterior. Junto se guardan país, ASN, proveedor y el día.
- **200** `{"ok":true}`.

### `POST /api/v1/reporte/bloqueo`
Igual que el de módem (tope, límite, Turnstile con `origen=pagina`).

```json
{"i":"<32 hex>","origen":"panel","dominio":"ads.ejemplo.com","motivo":"pagina_no_abre"}
```

Una fila por instalación **y dominio** (`reportes_bloqueo`). **200** `{"ok":true}`.

### `POST /api/v1/placa`
El aviso diario de la placa (solo con "Compartir datos anónimos" encendido). Tope 256 B. Límite
`LIMITE` con clave `placa`. Sin Turnstile.

```json
{"i":"<32 hex>","v":"v1.2.0","b":1234}
```

`b` = anuncios bloqueados en las últimas 24 h, entero de 0 a 100 000 000 (`-1`, `1.5` o más → 400).
Guarda en `placas` (una fila por instalación: día, versión y país) y en `bloqueadas` (una por día e
instalación; si llega otro aviso el mismo día, gana el último). **200** `{"ok":true}`.

### `POST /api/v1/paso`
El embudo de la página. Tope 128 B. Límite `LIMITE` con clave `paso`. La página lo manda con
`sendBeacon`, sin número ni cookies.

```json
{"p":"instalo"}
```

Suma 1 al contador de ese paso en el día (`pasos`). **204** sin cuerpo.

### `POST /api/v1/contacto`
Tope 8192 B. Límite `LIMITE_CONTACTO` (2 por minuto) con clave `contacto`.

```json
{"nombre":"Ana","correo":"<correo de la persona>","tema":"no_bloquea",
 "mensaje":"Los anuncios siguen en la tele.","diagnostico":"Versión: v1.2.0…","turnstile":"<token>"}
```

| Campo | Regla |
|---|---|
| `nombre` | Opcional, hasta 80 caracteres, sin saltos ni caracteres de control |
| `correo` | Hasta 254, un solo `@` con algo a cada lado, sin espacios, saltos ni `<>",;()[]\` (`<correo>\r\nBcc: <otro>` → 400) |
| `tema` | De la lista fija |
| `mensaje` | De 10 a 2000 caracteres |
| `diagnostico` | Opcional, hasta 1500 caracteres (solo si la persona marcó adjuntarlo) |
| `turnstile` | Obligatorio |

En orden: límite por IP (429; en IPv6 por /64) → datos (400) → Turnstile (403) → reserva un lugar
del tope de **200 mensajes al día** para todo el Worker en un solo paso (si no hay → 429) → correo
al dueño. Si ese correo falla → **500** `interno` y se devuelve el lugar. Si sale, suma el tema
(`temas`), y si `RESPUESTA_AUTOMATICA="1"` manda la respuesta automática, **una por dirección y
día** y **50 al día en total** (`respuestas_dia`, con la dirección pasada por SHA-256 y pimienta;
pasado el tope no se contesta y no es error). Si la respuesta falla, igual **200**.

- **Correo al dueño**: a `CONTACTO_DESTINO` (secreto: el correo verificado del dueño), desde `CONTACTO_REMITENTE`
  con nombre "SiBlock", `Reply-To` = el correo de la persona, asunto **fijo** `[SiBlock] ` + el
  nombre del tema (p. ej. `[SiBlock] No bloquea anuncios`): no lleva nada que escribió la persona. El texto va sin HTML: se quitan las etiquetas `<…>`, los
  caracteres de control y los `\r`.
- **Respuesta automática**: asunto "Recibimos tu mensaje" y un texto **fijo** con enlaces a la guía
  del router y a problemas comunes. Nunca repite el nombre ni el mensaje.
- El mensaje no se guarda en D1: solo se cuentan el día y el tema.

**200** `{"ok":true}`.

### `GET /api/v1/pais`
`{"pais":"MX","proveedor":"<asOrganization>"}` (o `""` si Cloudflare no lo sabe), con
`Cache-Control: no-store`. Lo usan la guía del router (país inicial) y la página de reportes
(«Tu internet parece ser de: …»). No guarda nada ni tiene límite.

### `GET /api/v1/estadisticas`
Pública, `Cache-Control: public, max-age=300`.

```json
{"placas7d":12,
 "versiones":[{"v":"v1.2.0","n":10},{"v":"dev","n":2}],
 "paises":[{"pais":"MX","n":7},{"pais":"otros","n":5}],
 "bloqueadasSemanas":[{"desde":"2026-09-07","n":0},"… 12 semanas …",{"desde":"2026-11-16","n":48210}],
 "modems":[{"pais":"MX","proveedor":"Ejemplo","fabricante":"huawei","menu":"internet_wan","n":3}]}
```

| Campo | Regla |
|---|---|
| `placas7d` | Placas con un aviso en los últimos 7 días (hoy y los 6 anteriores) |
| `versiones` | Esas placas por versión |
| `paises` | Esas placas por país; un país sale solo con **5 o más**. Los demás (y los sin país) se suman en `otros`, que sale solo si llega a 5 |
| `bloqueadasSemanas` | Siempre **12** semanas que empiezan en lunes (UTC), la más vieja primero, con 0 si no hay datos |
| `modems` | Grupos `(pais, proveedor, fabricante, menu)` con `dns_ok` = sí y **3 reportes o más**; nunca `menu=otro` ni `fabricante=no-se` |

### `GET /api/v1/dueno/resumen`
Solo el dueño. Cloudflare Access protege `/dueno*` y `/api/v1/dueno/*`, y el Worker además revisa
el JWT de Access (header `Cf-Access-Jwt-Assertion`): RS256 con las llaves de
`<ACCESS_TEAM_DOMAIN>/cdn-cgi/access/certs`, `aud` que incluya `ACCESS_AUD`, `iss` =
`ACCESS_TEAM_DOMAIN`, `exp` y `nbf` con 60 s de holgura. Si falta algo, o las dos variables están
vacías → **403** `acceso`.

```json
{"embudo":[{"paso":"entro","n":120},{"paso":"conecto","n":80},"…"],
 "temas":[{"tema":"no_instala","n":3},"…"],
 "motivos":[{"motivo":"pagina_no_abre","n":5},"…"],
 "bloqueos":[{"dominio":"ads.ejemplo.com","n":4}],
 "guiasFaltan":[{"fabricante":"proveedor","n":6}],
 "placasTotal":40}
```

- `embudo`, `temas` y `motivos`: últimos 30 días, en el orden de las listas fijas (con 0 si no hay).
- `bloqueos`: los 50 dominios más reportados como "no debió bloquearse", de más a menos.
- `guiasFaltan`: reportes de módem con `menu=otro`, `fabricante=no-se` o `dns_ok` = no, por
  fabricante.
- `placasTotal`: placas distintas en la tabla `placas`, que la limpieza diaria recorta a 400 días:
  son las vistas en los últimos 400 días (una placa que borró su configuración cuenta otra vez,
  porque cambia su número de instalación).

### `GET https://siblock.s-dev.win/version.json` (archivo de la página)
No es del Worker: es un archivo fijo de la página que CI escribe en cada versión. En el repo,
`site/version.json` tiene `v0.0.0`.

```json
{"version":"v1.2.0","url":"https://siblock.s-dev.win/#paso-instalar"}
```

La placa solo lee `version` (si el archivo pasa de 1024 B o el valor no es `vX.Y.Z`, lo ignora).

### Limpieza diaria
Un cron (`17 3 * * *`, UTC) borra las filas con más de **400 días** de `bloqueadas`, `pasos`,
`temas`, `contacto_dia` y `placas`, y las de `respuestas_dia` de días anteriores. Los reportes de
módem y de bloqueos se quedan (uno por instalación, sin IP): son los que arman la guía.

### Ejemplos con curl (Worker local)

```bash
cd cloud && npx wrangler d1 migrations apply siblock --local   # tablas en la D1 local
npx wrangler dev                        # Worker y D1 locales; nunca toca el de verdad
curl -s http://127.0.0.1:8787/api/v1/pais
curl -s -X POST http://127.0.0.1:8787/api/v1/placa -H 'Content-Type: application/json' \
     -d '{"i":"000102030405060708090a0b0c0d0e0f","v":"dev","b":7}'
curl -s http://127.0.0.1:8787/api/v1/estadisticas
```

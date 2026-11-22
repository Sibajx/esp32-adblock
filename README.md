# SiBlock

**Instálalo desde la página: https://siblock.s-dev.win**
(la página se publica con la primera versión; hasta entonces, mira "Para desarrolladores" abajo).

SiBlock es una plaquita que quita anuncios y rastreadores en **todos los aparatos de tu casa** a la
vez: celulares, tele, consola, computadoras. No hay que instalar nada en cada aparato.

## Qué hace

- Bloquea anuncios y rastreadores en páginas y apps (unos 160 mil sitios conocidos).
- La lista se pone al día sola cada día.
- Tiene un panel en el navegador para ver cuánto bloquea, pausar el bloqueo un rato si algo no
  carga y permitir o bloquear sitios a mano.
- El panel te dice si **este aparato** ya usa SiBlock, y el botón **Algo no funciona** busca qué
  pasa y te dice qué hacer. También muestra gráficas de las últimas 24 horas y 7 días, y lo que
  más se bloquea.
- **Nombres a los aparatos** ("Tele sala", "Cel de mamá") en vez de números raros.
- **Pausa solo un aparato**, no toda la casa, cuando a alguien no le abre una página.
- **Horarios para los niños**: apaga redes sociales, juegos o todo internet en ciertos aparatos y a
  ciertas horas.
- **Listas que puedes sumar**: estafas y tiendas falsas, y algunos anuncios de sitios en español
  (por ahora son pocos; apagadas de fábrica).
- **Una luz** en la placa te dice si todo va bien, si se está poniendo al día o si algo falla.
- **Cuánto te ahorra**: un estimado de los MB de anuncios que no se bajaron en 7 días.
- **Respaldo**: baja un archivo con tus reglas, nombres y horarios, y súbelo en otra placa.
- La página está en **español e inglés**.
- En la página hay **guías** para poner SiBlock en tu router (por marca y por proveedor), para
  FortiGate y para lo que hace que un aparato se salte el bloqueo.
- SiBlock no manda a nadie la lista de lo que visitas. Lo último que se visitó solo se ve en tu
  panel, con tu contraseña, y se borra al desconectarla. Para los sitios que no bloquea, pregunta
  la dirección a un servicio público (Cloudflare 1.1.1.1 o Quad9), igual que hace hoy tu módem.
  Aparte de eso solo baja la lista y pide la hora.
- **Aparte de lo que necesita para funcionar (buscar direcciones, bajar la lista y pedir la hora),
  SiBlock no manda nada a internet a menos que tú lo actives**: ver el
  [aviso de privacidad](site/privacidad.html) y cómo se publica el servidor de reportes
  ([`cloud/DEPLOY.md`](cloud/DEPLOY.md)).

## Qué no bloquea

Los anuncios de **YouTube, Twitch, Instagram, Facebook y TikTok**. Vienen del mismo sitio que el
video o las fotos, así que no se pueden separar sin romper la app. Ningún bloqueador de este tipo
puede quitarlos.

## Qué necesitas

- Una placa **ESP32 clásica** (ESP32-WROOM-32, 4 MB). No sirven las ESP8266 ni las placas sin
  puerto USB.
- Un cable USB **de datos** (algunos cables solo cargan y no sirven para instalar).
- Una computadora con **Chrome o Edge**. Firefox y Safari todavía no pueden instalarlo.
- Tu WiFi de casa (de 2.4 GHz) y su contraseña.
- Un cargador de celular de 5 V para dejarla conectada.

## Pasos

1. Conecta la placa a la computadora con el cable.
2. Abre https://siblock.s-dev.win en Chrome o Edge y pulsa **Instalar SiBlock**.
3. Cuando lo pida, escribe el nombre y la contraseña de tu WiFi.
4. Al terminar, cierra el instalador y pulsa **Ver mi contraseña de respaldo**: la página te la
   muestra (la primera vez espera un par de minutos, SiBlock se está preparando). Guárdala: la
   necesitas si un día cambias tu WiFi.
5. Pulsa **Abrir mi panel** y crea la contraseña de tu panel.
6. Pon la dirección de SiBlock como DNS en tu router. La guía te explica cómo:
   https://siblock.s-dev.win/guia-router.html
7. Abre tu panel desde el aparato que quieras revisar: arriba te dice si ya usa SiBlock. Si algo
   falla, pulsa **Algo no funciona**.

Si algo se traba, el botón **BOOT** de la placa te saca del apuro:

| Cuánto lo mantienes | Qué pasa |
|---|---|
| Un toque (menos de 2 s) | Puedes crear la contraseña del panel durante 30 min |
| Entre 3 y 8 s | Abre la red `SiBlock-XXXX` para darle otro WiFi (usa la contraseña de respaldo). También se abre sola si lleva 10 min sin poder entrar a tu WiFi |
| 10 s o más | Borra el WiFi, las contraseñas, los nombres de los aparatos, los horarios, las listas elegidas, la opción de Private Relay, la historia de las gráficas y las opciones de Compartir con SiBlock (vuelven a quedar apagadas; el número de instalación cambia). Tus reglas y la lista bajada se quedan |

## Contacto

contacto@s-dev.win

## Licencia

SiBlock es software libre bajo la licencia GPLv3 (ver [`LICENSE`](LICENSE)). El nombre y el logo
de SiBlock no los cubre la licencia.

---

## Para desarrolladores

La documentación técnica (arquitectura, formato de la lista, API, seguridad, pruebas y operación)
está en [`docs/`](docs/README.md). El diseño de SiBlock por fases está en
[`docs/superpowers/specs/`](docs/superpowers/specs/2026-11-21-siblock-design.md).

Hardware: ESP32 clásico (WROOM-32 / DevKit V1, 4 MB de flash, sin PSRAM). Arduino core 2.0.17
(`espressif32@6.9.0`) con PlatformIO.

### Cómo funciona

Cada aparato de la casa pregunta al DNS "¿cuál es la IP de `ads.ejemplo.com`?". Si SiBlock es ese
DNS:

- si el dominio (o uno de sus padres) está en la lista → responde `0.0.0.0` / `::` y el anuncio no carga;
- si ya preguntaron por él hace poco → responde desde la caché (≈0.5 ms);
- si no → reenvía a 1.1.1.1 (o a 9.9.9.9 si el primero no contesta) y devuelve la respuesta.

Las listas (StevenBlack + HaGeZi Light, **~161 mil dominios**) se bajan por HTTPS al arrancar y cada
24 h. Para que quepan en un chip sin PSRAM se guardan en la flash como claves de 40 bits ordenadas
(~800 KB); en RAM solo queda un índice de 16 KB. Cada búsqueda lee un pedacito de flash. Las
listas opcionales de estafas y de anuncios en español se eligen en el panel
([`docs/lista.md`](docs/lista.md#listas-opcionales-fase-5)).

Los aparatos se reconocen por una huella hecha con su MAC y un número al azar de la placa: la MAC
y la IP no se guardan ([`docs/seguridad.md`](docs/seguridad.md#aparatos-con-nombre-y-su-huella)).

### Dónde está cada cosa

| Qué | Dónde |
|---|---|
| Arranque, WiFi sin bloquear, actualización diaria, línea `STATS` por serie | `src/main.cpp` |
| Configuración guardada en la placa (NVS): WiFi, hash de la contraseña del panel, contraseña del AP | `src/settings.*` |
| WiFi y contraseña de respaldo por USB (Improv Serial) | `src/improv_serial.*` |
| Modo de configuración por WiFi (AP `SiBlock-XXXX` + portal cautivo) | `src/portal.*`, `src/portal_page.h` |
| Botón BOOT y LED | `src/button.*` |
| DNS UDP + TCP, caché, reenvío, estadísticas por aparato | `src/dns_server.*` |
| Descarga, armado y búsqueda de la lista, reglas propias, pausa | `src/blocklist.*` |
| Panel web y API | `src/web.*`, `src/web_page.h` |
| Ajustes (DNS, tope, nombre, versión) | `src/config.h` |
| *(Fase 5)* Aparatos con nombre, pausa por aparato y horarios en la placa (`/familia.txt`) | `src/family_store.*` |
| *(Fase 5)* Aparatos, horarios y grupos; formato del respaldo; catálogo de listas | `lib/siblock_core/family.*`, `backup.*`, `lists.*` |
| Lógica pura de DNS, lista, caché, JSON, probada en la PC | `lib/adblock_core/` |
| Lógica pura de SiBlock: PBKDF2, base64, validaciones, Improv, botón, ventana, portal, nombres especiales, historia de 168 h, diagnóstico | `lib/siblock_core/` |
| Pruebas de la lógica | `test_host/` → `make -C test_host test` |
| Simulador del ESP32 en Linux | `sim/` |
| Página de instalación y guías (router, FortiGate, obstáculos); *(fase 3)* reportes, Contáctanos, estadísticas, privacidad; *(fase 5)* versión en inglés (`site/en/`), "¿Qué dice la luz?" y botón de apoyo opcional | `site/` |
| *(Fase 3)* Opciones "Compartir con SiBlock" y cliente HTTPS hacia el Worker | `src/cloud.*`, `lib/siblock_core/cloud.*` |
| *(Fase 3)* Worker `siblock-api` + D1 (reportes, estadísticas, Contáctanos); no lo publica CI | `cloud/` → `cd cloud && npm test` |
| CI (pruebas en cada push) y publicación de versiones | `.github/` |

### Compilar y subir

`src/secrets.h` es **opcional**. El WiFi se da desde la página de instalación (por USB) o desde
la red `SiBlock-XXXX`; la contraseña del panel se crea en el panel. Todo se guarda en la placa. Si quieres valores de fábrica para tu propia compilación:

```bash
cp src/secrets.example.h src/secrets.h   # WiFi 2.4 GHz y WEB_PASSWORD (12 a 64); solo se usan si la placa no tiene nada guardado
pio run -t upload && pio device monitor
```

Si la subida no arranca, mantén apretado **BOOT** mientras empieza. Sin `secrets.h`, la placa
espera el WiFi por USB (Improv) y, a los 2 min sin él, abre la red `SiBlock-XXXX`. La contraseña
de esa red sale por la serie al arrancar (`[ap] respaldo: …`).

Prueba antes de tocar el router:

```bash
dig @192.168.1.50 doubleclick.net    # 0.0.0.0
dig @192.168.1.50 google.com         # IP real
```

### Simulador (probar sin la placa)

`sim/` compila **el mismo código de `src/`** para Linux, cambiando solo la capa de hardware. Arma
dos binarios: `esp32-sim` (con `sim/secrets.h` como semilla) y `esp32-sim-fresh` (sin
`secrets.h`, como el firmware publicado).

```bash
make -C sim
(cd sim && SIM_FLASH=./sim-flash ./esp32-sim)   # DNS 127.0.0.1:5353, panel http://127.0.0.1:8080/ (clave-de-prueba)
python3 sim/test_sim.py                         # en otra terminal
pkill -x esp32-sim
```

Más detalle en [`docs/pruebas.md`](docs/pruebas.md).

### Versiones

CI compila en cada push y PR. Al subir un tag `vX.Y.Z` publica el firmware en la Release de
GitHub y la página en Cloudflare Pages (solo si el repo tiene el secreto `CLOUDFLARE_API_TOKEN`).
La versión se ve en el panel y en `/api/stats`. La actualización es por USB desde la página y no
borra el WiFi ni las contraseñas. No hay OTA: con la lista no cabe.

### Cosas a saber

- **IPv6**: si el router reparte DNS por IPv6, algunos aparatos lo usan y se saltan el bloqueo.
  Desactiva el DNS IPv6 del router (o el IPv6 en la LAN).
- **DNS cifrado** (Chrome/Firefox "DNS seguro", Android "DNS privado") ignora el DNS de la red.
  Desactívalo en esos aparatos. SiBlock apaga solo el DNS cifrado automático de Firefox y avisa en
  el panel de los aparatos que se lo saltan. iCloud Private Relay se puede apagar en la casa con
  una opción del panel (apagada de fábrica).
- **Si SiBlock se apaga, la casa se queda sin internet.** Por eso se cuida solo: si la tarea DNS se
  traba 30 s o el puerto 53 deja de recibir, se reinicia.
- El panel es HTTP: no reutilices una contraseña importante. Ver
  [`docs/seguridad.md`](docs/seguridad.md).
- Solo contesta a la red local; no lo expongas a internet.
- Cada 5 s imprime `STATS,<consultas>,<bloqueadas>,<dominios>,<ram_libre>` a 115200 baudios.

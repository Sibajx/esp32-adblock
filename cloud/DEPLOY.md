# Publicar el Worker de SiBlock (`siblock-api`)

Estos pasos los hace **el autor, a mano**, en este orden. Nada de esto lo hace CI ni un agente.
Todo se corre dentro de `cloud/` (`cd cloud && npm ci` antes del primer paso).

Lo que hace el Worker: recibe los reportes de módem y de "esto no debió bloquearse", el aviso diario
de las placas (solo si la persona encendió "Compartir datos anónimos"), los pasos del embudo de la
página, los mensajes de Contáctanos, y da las estadísticas públicas y el resumen del dueño.
No guarda IPs, MACs, nombres ni correos: solo conteos, opciones fijas y el número de instalación
pasado por SHA-256 con una pimienta secreta.

> En este archivo no van correos personales ni ids reales. El único correo del proyecto es
> `contacto@s-dev.win`.

## Antes de empezar

- Pruebas en verde: `npm test` y `npm run tipos`.
- Que compile sin publicar: `npx wrangler deploy --dry-run --outdir dist` (debe listar los
  bindings `DB`, `CORREO`, `LIMITE`, `LIMITE_CONTACTO` y las variables). Borra `dist/` después.

## 1) Entrar a Cloudflare

```sh
npx wrangler login
npx wrangler whoami   # comprobar: sale tu cuenta
```

## 2) Base de datos D1

```sh
npx wrangler d1 create siblock
```

Copia el `database_id` que imprime y pégalo en `wrangler.toml` en lugar de
`REEMPLAZAR-CON-EL-ID-DE-D1` (ese id no es secreto, pero no lo subas si prefieres; puedes dejarlo
solo en tu copia local). Luego crea las tablas:

```sh
npx wrangler d1 migrations apply siblock --remote
npx wrangler d1 execute siblock --remote --command "SELECT name FROM sqlite_master WHERE type='table'"
```

Comprobar: salen `reportes_modem`, `reportes_bloqueo`, `placas`, `bloqueadas`, `pasos`, `temas`,
`contacto_dia`, `respuestas_dia` (y `d1_migrations`).

## 3) Email Routing (recibir en `contacto@s-dev.win`)

Panel de Cloudflare → zona `s-dev.win` → **Email** → **Email Routing**:

1. Activarlo si no lo está (Cloudflare agrega sus registros MX y SPF; acéptalos).
2. **Destination addresses** → agregar tu correo personal y confirmarlo desde el mensaje que llega.
   Ese correo **nunca** va en el repo.
3. **Routing rules** → dirección `contacto@s-dev.win` → acción "Send to an email" → tu correo verificado.

Comprobar: manda un correo a `contacto@s-dev.win` desde otra cuenta y mira que te llegue.

## 4) Email Sending (que el Worker pueda mandar correos)

Panel → **Email** → **Email Sending** (o "Email Service"; el nombre del menú puede cambiar, falta
verificar con la cuenta real):

1. Incorporar el dominio `s-dev.win`. Cloudflare agrega SPF, DKIM y DMARC en el subdominio
   `cf-bounce`.
2. Revisar los DNS de `s-dev.win` (**DNS** → **Records**):
   - Debe haber **un solo** registro TXT de SPF en `s-dev.win` (que empiece con `v=spf1`). Si ya había
     uno, junta los `include:` en ese mismo registro en vez de crear otro.
   - DMARC en `_dmarc.s-dev.win`: si ya existe uno con `p=reject`, **déjalo así** (es más estricto
     y no hay que bajarlo). Solo si no hay ninguno, crea `v=DMARC1; p=quarantine;
     rua=mailto:contacto@s-dev.win`. Nunca dejes dos: uno solo.
   - DNSSEC: **DNS** → **Settings** → **Enable DNSSEC**, y copia el registro DS que te da Cloudflare
     en el sitio donde compraste el dominio (falta verificar el nombre exacto del menú allí).
3. La respuesta automática a quien escribe necesita poder mandar a **cualquier** correo, y eso pide
   el plan de pago de Workers. Si tu cuenta no lo tiene, cambia en `wrangler.toml`
   `RESPUESTA_AUTOMATICA = "0"` (el mensaje al dueño sigue funcionando).
4. El Worker solo puede mandar a un correo **verificado** en Email Routing, así que el destino
   (tu correo verificado, no `contacto@s-dev.win`) va como **secreto**, nunca en el archivo:

   ```sh
   npx wrangler secret put CONTACTO_DESTINO
   ```

   `contacto@s-dev.win` sigue siendo la dirección pública: una regla de Email Routing la reenvía
   a ese mismo correo verificado.

## 5) Turnstile (la casilla "no soy un robot" de la página)

Panel → **Turnstile** → **Add widget**: nombre "SiBlock", dominio `siblock.s-dev.win`, modo
"Managed".

```sh
npx wrangler secret put TURNSTILE_SECRET   # pega la "Secret key"
```

La "Site key" (pública) va como **variable** del repositorio en GitHub con el nombre
`TURNSTILE_SITEKEY` (Settings → Secrets and variables → Actions → Variables). CI la escribe en
`site/js/config.js`.

Comprobar: el formulario de `reporte.html` muestra la casilla y al enviar responde "ok".

## 6) Pimienta del número de instalación

```sh
openssl rand -hex 32                 # copia el resultado (64 caracteres)
npx wrangler secret put ID_PEPPER    # pégalo
```

Sin `ID_PEPPER` (o con menos de 16 caracteres) el Worker rechaza todos los envíos con un error
interno, a propósito. Guárdala en tu gestor de contraseñas: si se pierde o se cambia, los números
viejos ya no se reconocen y la cuenta de "uno por instalación" empieza de cero.

Comprobar: `npx wrangler secret list` muestra `ID_PEPPER` y `TURNSTILE_SECRET` (sin sus valores).

## 7) Cloudflare Access (panel del dueño)

Panel → **Zero Trust** → **Access** → **Applications** → **Add an application** → "Self-hosted":

1. Nombre "SiBlock dueño". Dominios: `siblock.s-dev.win/dueno*` y
   `siblock.s-dev.win/api/v1/dueno/*`.
2. **Session duration**: 30 días (`720h`).
3. Política "Solo el dueño": acción **Allow**, regla **Emails** = tu correo (se escribe en el panel,
   nunca en el repo). Método de entrada: código de un solo uso por correo.
4. Copia el **Application Audience (AUD) Tag** y el dominio del equipo
   (`https://<tu-equipo>.cloudflareaccess.com`) y ponlos en `wrangler.toml`:
   `ACCESS_AUD` y `ACCESS_TEAM_DOMAIN` (sin barra al final). El Worker además revisa la firma del
   JWT de Access; si esas dos variables están vacías, responde 403 siempre.

Comprobar: en una ventana privada, `https://siblock.s-dev.win/dueno.html` pide el código; tras
entrar, el resumen carga. `curl -i https://siblock.s-dev.win/api/v1/dueno/resumen` sin sesión no
debe dar 200.

## 8) Web Analytics (sin cookies)

Panel → **Analytics & Logs** → **Web Analytics** → **Add a site** → `siblock.s-dev.win`, elegir
"instalar el fragmento JS a mano". Copia el token y guárdalo como **variable** del repositorio
`CF_WA_TOKEN`. CI lo escribe en `site/js/config.js`.

Comprobar: tras publicar la página, en unas horas aparecen visitas en Web Analytics.

## 9) Publicar el Worker

```sh
npx wrangler deploy
curl -s https://siblock.s-dev.win/api/v1/pais          # {"pais":"..","proveedor":".."}
curl -s https://siblock.s-dev.win/api/v1/estadisticas  # {"placas7d":0,...}
```

La ruta `siblock.s-dev.win/api/*` debe ganarle a Cloudflare Pages para esa parte de la dirección
(falta verificar). Si `curl` devuelve la página de Pages (HTML) en vez de JSON, el plan B es servir
la API como **Pages Functions** dentro del proyecto de la página.

Comprobar el cron: Panel → Workers → `siblock-api` → **Triggers** muestra `17 3 * * *`
(limpieza diaria de conteos de más de 400 días).

## 10) Si algo falla, y cómo apagar todo

- Ver errores en vivo: `npx wrangler tail siblock-api`. Las respuestas de error solo dicen un código
  (`datos`, `limite`, `interno`…); no muestran detalles a quien pregunta.
- Contáctanos da 500: casi siempre es el paso 4 (Email Sending o destino no permitido).
- Todo da 429: los límites `LIMITE` / `LIMITE_CONTACTO`; si el servicio de límites falla, el Worker
  rechaza en vez de dejar pasar.
- Reportes desde la página dan 403 `turnstile`: revisa que la Site key y `TURNSTILE_SECRET` sean
  del mismo widget y que el dominio del widget sea `siblock.s-dev.win`.
- **Apagar todo**: Panel → Workers → `siblock-api` → **Settings** → **Domains & Routes** → quitar la
  ruta `siblock.s-dev.win/api/*` (la página sigue funcionando; los formularios dirán que no se pudo
  enviar). Para borrarlo del todo: `npx wrangler delete siblock-api`. Los datos quedan en D1 hasta
  que borres la base (`npx wrangler d1 delete siblock`).
- Las placas no se enteran: con "Compartir datos anónimos" apagado (así vienen de fábrica) nunca
  le hablan al Worker.

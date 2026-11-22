// Revisa todas las páginas site/*.html y site/en/*.html: idioma, título, nada de JS en línea,
// enlaces que existen, solo el correo de contacto y enlaces externos con rel="noopener".
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { join, dirname, posix } from 'node:path';
import { fileURLToPath } from 'node:url';

const SITIO = join(dirname(fileURLToPath(import.meta.url)), '..');
const PAGINAS = readdirSync(SITIO).filter((f) => f.endsWith('.html')).sort();
const PAGINAS_EN = readdirSync(join(SITIO, 'en')).filter((f) => f.endsWith('.html')).sort().map((f) => `en/${f}`);
const TODAS = [...PAGINAS, ...PAGINAS_EN];
const CORREO = 'contacto@s-dev.win';
// Páginas que tienen versión en el otro idioma.
const PARES = { 'index.html': 'en/index.html', 'privacidad.html': 'en/privacy.html' };
const DOMINIO = 'https://siblock.s-dev.win/';

const cache = new Map();
function leer(nombre) {
  if (!cache.has(nombre)) cache.set(nombre, readFileSync(join(SITIO, nombre), 'utf8'));
  return cache.get(nombre);
}

function etiquetas(html, nombre) {
  return [...html.matchAll(new RegExp(`<${nombre}\\b[^>]*>`, 'gi'))].map((m) => m[0]);
}

function atributo(tag, nombre) {
  const m = tag.match(new RegExp(`\\s${nombre}\\s*=\\s*("([^"]*)"|'([^']*)'|([^\\s>]+))`, 'i'));
  return m ? (m[2] ?? m[3] ?? m[4]) : null;
}

function ids(html) {
  return new Set([...html.matchAll(/\sid\s*=\s*"([^"]+)"/gi)].map((m) => m[1]));
}

function esExterno(href) {
  return /^(https?:)?\/\//i.test(href);
}

/** Resuelve un enlace relativo desde `pagina` a un archivo del sitio ('' = la misma página). */
function resolver(pagina, archivo) {
  if (archivo === '') return pagina;
  let destino = posix.normalize(posix.join(posix.dirname(pagina), archivo));
  if (archivo.endsWith('/') || destino === '.') destino = posix.join(destino, 'index.html');
  return destino;
}

/** Idioma esperado según la carpeta. */
function lenguaDe(pagina) {
  return pagina.startsWith('en/') ? 'en' : 'es';
}

test('hay páginas que revisar, incluidas las guías', () => {
  for (const p of ['index.html', 'guia-router.html', 'fortigate.html', 'obstaculos.html']) {
    assert.ok(PAGINAS.includes(p), `falta ${p}`);
  }
});

for (const pagina of TODAS) {
  test(`${pagina}: idioma, viewport y título con SiBlock`, () => {
    const html = leer(pagina);
    assert.match(html, /^<!doctype html>/i);
    const raiz = etiquetas(html, 'html')[0];
    assert.ok(raiz, 'sin <html>');
    assert.equal(atributo(raiz, 'lang'), lenguaDe(pagina));
    assert.match(html, /<meta\s+name="viewport"/i);
    const titulo = html.match(/<title>([^<]*)<\/title>/i);
    assert.ok(titulo, 'sin <title>');
    assert.match(titulo[1], /SiBlock/);
  });

  test(`${pagina}: sin JavaScript en línea`, () => {
    const html = leer(pagina);
    for (const tag of etiquetas(html, 'script')) {
      assert.ok(atributo(tag, 'src'), `script sin src: ${tag}`);
    }
    for (const tag of html.match(/<[a-z][^>]*>/gi) ?? []) {
      assert.doesNotMatch(tag, /\son[a-z]+\s*=/i, `atributo on…: ${tag}`);
    }
    assert.doesNotMatch(html, /javascript:/i);
  });

  test(`${pagina}: los enlaces internos existen`, () => {
    const html = leer(pagina);
    const tags = [...etiquetas(html, 'a'), ...etiquetas(html, 'link'), ...etiquetas(html, 'img'), ...etiquetas(html, 'script')];
    for (const tag of tags) {
      const url = atributo(tag, 'href') ?? atributo(tag, 'src');
      if (url === null || esExterno(url) || /^mailto:/i.test(url) || /^data:/i.test(url)) continue;
      const [archivo, ancla] = url.split('#');
      assert.ok(!archivo.startsWith('/'), `ruta rara: ${url}`);
      const destino = resolver(pagina, archivo);
      assert.ok(!destino.startsWith('..'), `se sale del sitio: ${url}`);
      assert.ok(existsSync(join(SITIO, destino)) && statSync(join(SITIO, destino)).isFile(),
        `${pagina}: no existe ${destino} (${url})`);
      if (ancla !== undefined) {
        assert.ok(destino.endsWith('.html'), `ancla en algo que no es página: ${url}`);
        assert.ok(ids(leer(destino)).has(ancla), `${pagina}: no existe #${ancla} en ${destino}`);
      }
    }
  });

  test(`${pagina}: el único correo es ${CORREO}`, () => {
    const html = leer(pagina);
    const correos = html.match(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g) ?? [];
    for (const c of correos) assert.equal(c, CORREO);
  });

  test(`${pagina}: enlaces externos con rel noopener`, () => {
    const html = leer(pagina);
    for (const tag of etiquetas(html, 'a')) {
      const href = atributo(tag, 'href');
      if (href === null || !esExterno(href)) continue;
      const rel = (atributo(tag, 'rel') ?? '').toLowerCase().split(/\s+/);
      assert.ok(rel.includes('noopener'), `sin noopener: ${tag}`);
    }
  });

  test(`${pagina}: los id no se repiten`, () => {
    const html = leer(pagina);
    const todos = [...html.matchAll(/\sid\s*=\s*"([^"]+)"/gi)].map((m) => m[1]);
    assert.equal(new Set(todos).size, todos.length);
  });
}

test('obstaculos.html no promete detectar el antivirus (SiBlock todavía no lo reconoce)', () => {
  const html = leer('obstaculos.html');
  const fila = html.match(/<tr>\s*<th scope="row">Antivirus[\s\S]*?<\/tr>/);
  assert.ok(fila, 'falta la fila del antivirus');
  assert.doesNotMatch(fila[0], /Lo detecta/);
  assert.match(fila[0], /Solo con guía/);
  assert.match(fila[0], /DNS seguro/);
});

test('obstaculos.html no promete detectar el DNS cifrado de Windows 11 ni los perfiles del iPhone', () => {
  const html = leer('obstaculos.html');
  const fila = html.match(/<tr>\s*<th scope="row">Windows 11[\s\S]*?<\/tr>/);
  assert.ok(fila, 'falta la fila de Windows 11');
  assert.doesNotMatch(fila[0], /Lo detecta/);
  assert.match(fila[0], /Solo con guía/);
  assert.match(fila[0], /Automático/);
  assert.match(fila[0], /VPN y administración de dispositivos/);
});

test('guia-router.html: en Android dice todo lo que pide la IP estática y cómo volver atrás', () => {
  const html = leer('guia-router.html');
  const bloque = html.match(/<h3>Android<\/h3>[\s\S]*?<\/div>/);
  assert.ok(bloque, 'falta el bloque de Android');
  for (const campo of ['Dirección IP', 'Puerta de enlace', 'Longitud del prefijo', 'DNS 1', 'DHCP', 'Falta verificar']) {
    assert.ok(bloque[0].includes(campo), `falta: ${campo}`);
  }
});

// --- Fase 3 (carril site) ---

test('hay páginas nuevas de la fase 3', () => {
  for (const p of ['privacidad.html', 'reporte.html', 'contacto.html', 'estadisticas.html', 'dueno.html']) {
    assert.ok(PAGINAS.includes(p), `falta ${p}`);
  }
});

for (const pagina of PAGINAS) {
  test(`${pagina}: carga js/inicio.js como módulo`, () => {
    const html = leer(pagina);
    const tag = etiquetas(html, 'script').find((t) => atributo(t, 'src') === 'js/inicio.js');
    assert.ok(tag, 'no carga js/inicio.js');
    assert.equal(atributo(tag, 'type'), 'module');
  });

  test(`${pagina}: el pie enlaza a Privacidad y a Contáctanos`, () => {
    const html = leer(pagina);
    const pie = html.match(/<footer[\s\S]*?<\/footer>/i);
    assert.ok(pie, 'sin <footer>');
    assert.match(pie[0], /<a href="privacidad\.html">Privacidad<\/a>/);
    assert.match(pie[0], /<a href="contacto\.html">Contáctanos<\/a>/);
    assert.match(pie[0], /mailto:contacto@s-dev\.win/);
    assert.doesNotMatch(pie[0], /no usa cookies ni cuenta tus visitas/);
  });

}

for (const pagina of TODAS) {
  test(`${pagina}: no usa localStorage, sessionStorage ni document.cookie`, () => {
    assert.doesNotMatch(leer(pagina), /localStorage|sessionStorage|document\.cookie/);
  });
}

test('_headers: la CSP suma analítica y Turnstile, y sigue sin formularios nativos', () => {
  const h = readFileSync(join(SITIO, '_headers'), 'utf8');
  const csp = h.match(/Content-Security-Policy:\s*(.+)/)[1];
  const dir = Object.fromEntries(csp.split(';').map((d) => d.trim().split(/\s+/)).map(([k, ...v]) => [k, v]));
  assert.ok(dir['script-src'].includes('https://static.cloudflareinsights.com'));
  assert.ok(dir['script-src'].includes('https://challenges.cloudflare.com'));
  assert.ok(dir['connect-src'].includes('https://cloudflareinsights.com'));
  assert.deepEqual(dir['frame-src'], ['https://challenges.cloudflare.com']);
  assert.deepEqual(dir['form-action'], ["'none'"]);
  assert.ok(!dir['script-src'].includes("'unsafe-inline'"));
  assert.ok(!dir['script-src'].includes("'unsafe-eval'"));
});

test('_headers: unpkg solo para la carpeta fija del instalador, con HSTS y permisos cerrados', () => {
  const h = readFileSync(join(SITIO, '_headers'), 'utf8');
  const csp = h.match(/Content-Security-Policy:\s*(.+)/)[1];
  const dir = Object.fromEntries(csp.split(';').map((d) => d.trim().split(/\s+/)).map(([k, ...v]) => [k, v]));
  for (const k of ['script-src', 'connect-src']) {
    for (const v of dir[k].filter((x) => x.includes('unpkg'))) {
      assert.equal(v, 'https://unpkg.com/esp-web-tools@10.4.0/dist/web/', k);
    }
  }
  assert.match(h, /Strict-Transport-Security: max-age=\d{7,}/);
  assert.match(h, /Permissions-Policy:[^\n]*camera=\(\)[^\n]*microphone=\(\)[^\n]*geolocation=\(\)/);
});

test('_headers: el panel del dueño no se indexa', () => {
  const h = readFileSync(join(SITIO, '_headers'), 'utf8');
  assert.match(h, /\/dueno\*?[^\n]*\n\s+X-Robots-Tag: noindex/);
});

test('js/*.js: nadie usa localStorage, sessionStorage, document.cookie ni innerHTML', () => {
  const dir = join(SITIO, 'js');
  for (const f of readdirSync(dir).filter((x) => x.endsWith('.js'))) {
    const js = readFileSync(join(dir, f), 'utf8');
    assert.doesNotMatch(js, /localStorage|sessionStorage|document\.cookie|innerHTML|outerHTML|insertAdjacentHTML/, f);
  }
});

test('privacidad.html: secciones de la placa, los reportes y Contáctanos', () => {
  const html = leer('privacidad.html');
  for (const id of ['placa', 'reportes', 'contacto', 'pagina']) assert.ok(ids(html).has(id), `falta #${id}`);
  // "cookie" solo puede salir como "sin cookies".
  const sinPermitidas = html.replace(/sin cookies/gi, '');
  assert.doesNotMatch(sinPermitidas, /cookie/i);
  assert.match(html, /contacto@s-dev\.win/);
  for (const nunca of ['IP', 'MAC']) assert.ok(html.includes(nunca), `no dice nada de ${nunca}`);
  assert.match(html, /apagad[ao]s? de fábrica/);
});

test('privacidad.html: copia tal cual el aviso de "Compartir datos anónimos" del panel', () => {
  const html = leer('privacidad.html').replace(/\s+/g, ' ');
  assert.ok(html.includes('Nunca manda tu IP, la MAC, nombres de aparatos ni qué sitios visitas.'));
  const frase = 'Con esta opción apagada, SiBlock no manda nada tuyo a siblock.s-dev.win (solo revisa si hay versión nueva, si activas «Avisarme de versiones nuevas»).';
  assert.ok(html.includes(frase));
  assert.ok(readFileSync(join(SITIO, '..', 'src', 'web_page.h'), 'utf8').includes(frase), 'el panel dice otra cosa');
  assert.ok(!html.includes('solo se conecta a internet para bajar la lista'), 'no puede decir que solo baja la lista');
});

test('privacidad.html: dice lo que SiBlock hace siempre (DNS de afuera, lista, hora, versiones)', () => {
  const html = leer('privacidad.html').replace(/\s+/g, ' ');
  assert.ok(!html.includes('salvo bajar la lista'), '«Lo corto» no puede decir que solo baja la lista');
  const siempre = html.slice(html.indexOf('id="siempre"'), html.indexOf('</section>', html.indexOf('id="siempre"')));
  assert.ok(html.indexOf('id="siempre"') > 0, 'falta #siempre');
  for (const t of ['Cloudflare', '1.1.1.1', 'Quad9', '9.9.9.9', 'lista de anuncios', 'hora', 'pool.ntp.org',
    'time.cloudflare.com', 'Avisarme de versiones nuevas']) assert.ok(siempre.includes(t), `#siempre no dice «${t}»`);
  assert.match(html, /versión, memoria libre, avisos y el último error al bajar la lista/);
  for (const t of ['listas extra', 'GitHub', 'adblockplus.org']) assert.ok(siempre.includes(t), `#siempre no dice «${t}»`);
});

test('en/privacy.html y en/index.html: las listas extra (de dónde se bajan y que vienen apagadas)', () => {
  const priv = leer('en/privacy.html').replace(/\s+/g, ' ');
  for (const t of ['extra lists', 'GitHub', 'adblockplus.org']) assert.ok(priv.includes(t), `en/privacy.html no dice «${t}»`);
  const inicio = leer('en/index.html').replace(/\s+/g, ' ');
  assert.ok(inicio.includes("They're off by default"), 'en/index.html: las listas extra vienen apagadas');
  assert.ok(!inicio.includes('They come off'), 'en/index.html: «They come off» no se entiende');
});

test('privacidad.html: dice qué se guarda del aviso diario y separa los dos reportes', () => {
  const html = leer('privacidad.html').replace(/\s+/g, ' ');
  const placa = html.slice(html.indexOf('id="placa"'), html.indexOf('id="reportes"'));
  for (const t of ['Del aviso de cada día guardamos:', 'la versión de SiBlock', 'el país', 'cuántos anuncios quitó ese día',
    'el día (no la hora)', 'se borra a los 400 días']) assert.ok(placa.includes(t), `#placa no dice «${t}»`);
  const rep = html.slice(html.indexOf('id="reportes"'), html.indexOf('id="contacto"'));
  assert.ok(!rep.includes('nada de texto libre'), 'no puede decir "nada de texto libre": el sitio se escribe');
  const bloqueo = rep.slice(rep.indexOf('De «esto no debió bloquearse» guardamos:'));
  assert.ok(bloqueo.includes('el nombre del sitio'));
  assert.ok(!bloqueo.includes('compañía de internet'), 'el reporte de bloqueo no guarda país ni compañía');
  assert.doesNotMatch(leer('reporte.html'), /Solo eliges opciones/);
});

test('dueno.html: cada parte dice de qué tiempo es', () => {
  const d = leer('dueno.html').replace(/\s+/g, ' ');
  const parte = (id) => d.slice(d.indexOf(`id="${id}"`), d.indexOf('</section>', d.indexOf(`id="${id}"`)));
  for (const id of ['s-embudo', 's-temas', 's-motivos']) assert.ok(parte(id).includes('Últimos 30 días.'), id);
  for (const id of ['s-bloqueos', 's-guias']) assert.match(parte(id), /desde el principio/i, id);
  // placasTotal sale de la tabla `placas`, que la limpieza diaria recorta a 400 días.
  assert.ok(parte('s-placas').includes('en los últimos 400 días'), 's-placas');
  assert.doesNotMatch(parte('s-placas'), /desde el principio/i);
  assert.ok(parte('s-guias').includes('no pudieron cambiar el DNS'));
});

test('reporte.html: dos formularios con lo que pide el contrato y aviso que enlaza a privacidad', () => {
  const html = leer('reporte.html');
  for (const id of ['form-modem', 'form-bloqueo', 'proveedor-visto', 'aviso-listo']) assert.ok(ids(html).has(id), `falta #${id}`);
  assert.match(html, /href="privacidad\.html#reportes"/);
  for (const tag of etiquetas(html, 'form')) assert.equal(atributo(tag, 'action'), null, 'los formularios no mandan solos');
  assert.ok(etiquetas(html, 'script').some((t) => atributo(t, 'src') === 'js/reporte.js'));
});

test('contacto.html: campos, temas y casilla del diagnóstico', () => {
  const html = leer('contacto.html');
  for (const id of ['form-contacto', 'nombre', 'correo', 'tema', 'mensaje', 'adjuntar', 'diag', 'contacto-estado']) {
    assert.ok(ids(html).has(id), `falta #${id}`);
  }
  assert.match(html, /Adjuntar el diagnóstico de mi SiBlock/);
  assert.ok(etiquetas(html, 'script').some((t) => atributo(t, 'src') === 'js/contacto.js'));
});

test('estadisticas.html y dueno.html: lo que deben decir', () => {
  const e = leer('estadisticas.html').replace(/\s+/g, ' ');
  assert.ok(e.includes('Un país aparece cuando tiene 5 placas o más.'));
  assert.ok(etiquetas(e, 'script').some((t) => atributo(t, 'src') === 'js/estadisticas.js'));
  const d = leer('dueno.html');
  assert.match(d, /<meta name="robots" content="noindex">/);
  assert.ok(etiquetas(d, 'script').some((t) => atributo(t, 'src') === 'js/dueno.js'));
});

test('index.html enlaza a las estadísticas', () => {
  assert.match(leer('index.html'), /href="estadisticas\.html"/);
});

// --- Fase 5 (carril site): página en inglés ---

test('hay páginas en inglés: inicio y privacidad', () => {
  assert.deepEqual(PAGINAS_EN, ['en/index.html', 'en/privacy.html']);
});

for (const pagina of PAGINAS_EN) {
  test(`${pagina}: carga ../js/inicio.js como módulo y los estilos de arriba`, () => {
    const html = leer(pagina);
    const tag = etiquetas(html, 'script').find((t) => atributo(t, 'src') === '../js/inicio.js');
    assert.ok(tag, 'no carga ../js/inicio.js');
    assert.equal(atributo(tag, 'type'), 'module');
    assert.ok(etiquetas(html, 'link').some((t) => atributo(t, 'href') === '../estilos.css'));
  });

  test(`${pagina}: el pie enlaza a Privacy, al contacto y al correo`, () => {
    const pie = leer(pagina).match(/<footer[\s\S]*?<\/footer>/i);
    assert.ok(pie, 'sin <footer>');
    assert.match(pie[0], /<a href="privacy\.html">Privacy<\/a>/);
    assert.match(pie[0], /href="\.\.\/contacto\.html"/);
    assert.match(pie[0], /mailto:contacto@s-dev\.win/);
  });

  test(`${pagina}: sin palabras sueltas en español en el texto principal`, () => {
    const html = leer(pagina).replace(/<span lang="es"[^>]*>[\s\S]*?<\/span>/g, '')
      .replace(/<a [^>]*lang="es"[^>]*>[\s\S]*?<\/a>/g, '').replace(/<[^>]+>/g, ' ');
    for (const palabra of ['Instalar', 'Privacidad', 'Contáctanos', 'contraseña', 'Qué necesito']) {
      assert.ok(!html.includes(palabra), `${pagina} dice «${palabra}»`);
    }
  });
}

for (const pagina of PAGINAS) {
  test(`${pagina}: el pie enlaza a la versión en inglés`, () => {
    const pie = leer(pagina).match(/<footer[\s\S]*?<\/footer>/i)[0];
    const enlace = etiquetas(pie, 'a').find((t) => atributo(t, 'hreflang') === 'en');
    assert.ok(enlace, 'sin enlace hreflang="en" en el pie');
    assert.equal(atributo(enlace, 'lang'), 'en');
    const esperado = PARES[pagina] ?? 'en/index.html';
    assert.equal(resolver(pagina, atributo(enlace, 'href')), esperado);
    assert.match(pie, /hreflang="en" lang="en">English<\/a>/);
  });
}

for (const [es, en] of Object.entries(PARES)) {
  const publica = (p) => DOMINIO + p.replace(/(^|\/)index\.html$/, '$1');

  test(`${es} y ${en} se enlazan entre sí (hreflang)`, () => {
    const alterna = (html, lengua) => etiquetas(html, 'link')
      .find((t) => atributo(t, 'rel') === 'alternate' && atributo(t, 'hreflang') === lengua);
    const deEs = alterna(leer(es), 'en');
    assert.ok(deEs, `${es} sin <link rel="alternate" hreflang="en">`);
    assert.equal(atributo(deEs, 'href'), publica(en));
    const deEn = alterna(leer(en), 'es');
    assert.ok(deEn, `${en} sin <link rel="alternate" hreflang="es">`);
    assert.equal(atributo(deEn, 'href'), publica(es));

    const vuelta = etiquetas(leer(en), 'a').find((t) => atributo(t, 'hreflang') === 'es' && atributo(t, 'lang') === 'es'
      && resolver(en, atributo(t, 'href')) === es);
    assert.ok(vuelta, `${en} no enlaza a ${es}`);
  });
}

test('en/index.html: instala desde ../firmware y avisa que las guías están en español', () => {
  const html = leer('en/index.html');
  const boton = etiquetas(html, 'esp-web-install-button')[0];
  assert.ok(boton, 'sin botón de instalar');
  assert.equal(atributo(boton, 'manifest'), '../firmware/manifest.json');
  assert.ok(etiquetas(html, 'script').some((t) => atributo(t, 'src') === '../js/instalar.js'));
  assert.ok(html.includes('This guide is only in Spanish for now'));
  assert.match(html, /Chrome or Edge/);
  for (const id of ['compra', 'paso-instalar', 'despues', 'router', 'instalar', 'sin-soporte', 'respaldo',
    'ver-respaldo', 'respaldo-estado', 'respaldo-datos', 'respaldo-red', 'respaldo-clave', 'respaldo-panel',
    'abrir-panel', 'respaldo-ip']) {
    assert.ok(ids(html).has(id), `falta #${id}`);
  }
  // Las guías en español se marcan como tales.
  for (const tag of etiquetas(html, 'a').filter((t) => /^\.\.\/(guia-router|obstaculos|fortigate)\.html/.test(atributo(t, 'href') ?? ''))) {
    assert.equal(atributo(tag, 'hreflang'), 'es', tag);
  }
});

test('en/index.html: el botón de apoyo está en el pie, oculto, en inglés', () => {
  const pie = leer('en/index.html').match(/<footer[\s\S]*?<\/footer>/i)[0];
  assert.ok(pie.includes('<a id="apoyo" hidden rel="noopener noreferrer">Buy me a coffee</a>'));
});

test('en/privacy.html: mismas secciones que privacidad.html y sin cookies', () => {
  const html = leer('en/privacy.html');
  for (const id of ['placa', 'reportes', 'contacto', 'pagina', 'siempre']) assert.ok(ids(html).has(id), `falta #${id}`);
  const sinPermitidas = html.replace(/no cookies/gi, '');
  assert.doesNotMatch(sinPermitidas, /cookie/i);
  for (const t of ['IP', 'MAC', 'off by default', '400 days', 'Cloudflare', 'Quad9', 'pool.ntp.org']) {
    assert.ok(html.includes(t), `no dice «${t}»`);
  }
});

// --- Fase 5 (carril site): "¿Qué dice la luz?" ---

const LUZ = {
  'index.html': { filas: ['Luz fija', 'Parpadea despacio', 'Parpadea rápido'], extra: ['SiBlock-XXXX', 'desconéctala y vuelve a conectarla'] },
  'en/index.html': { filas: ['Steady light', 'Slow blink', 'Fast blink'], extra: ['SiBlock-XXXX', 'unplug it and plug it back in'] },
};

function seccion(html, id) {
  const ini = html.search(new RegExp(`<section[^>]*\\sid="${id}"`));
  if (ini < 0) return null;
  return html.slice(ini, html.indexOf('</section>', ini) + '</section>'.length);
}

for (const [pagina, { filas, extra }] of Object.entries(LUZ)) {
  test(`${pagina}: sección #luz con dibujo accesible y las tres formas de la luz`, () => {
    const luz = seccion(leer(pagina), 'luz');
    assert.ok(luz, 'falta <section id="luz">');
    const svg = etiquetas(luz, 'svg')[0];
    assert.ok(svg, 'sin <svg> en #luz');
    assert.equal(atributo(svg, 'role'), 'img');
    assert.ok((atributo(svg, 'aria-label') ?? '').trim().length > 10, 'aria-label vacío o muy corto');
    const plano = luz.replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ');
    for (const t of [...filas, ...extra]) assert.ok(plano.includes(t), `#luz no dice «${t}»`);
  });
}

test('estilos.css: el parpadeo de la luz se quita con prefers-reduced-motion', () => {
  const css = readFileSync(join(SITIO, 'estilos.css'), 'utf8');
  assert.match(css, /@keyframes\s+luz-/);
  const reducido = [...css.matchAll(/@media\s*\(prefers-reduced-motion:\s*reduce\)\s*\{([\s\S]*?\})\s*\}/g)].map((m) => m[1]).join('\n');
  assert.match(reducido, /\.luz-[a-z-]+[^{]*\{[^}]*animation:\s*none/);
});

test('index.html y en/index.html: "Qué puede hacer" nombra los extras de la fase 5', () => {
  const es = seccion(leer('index.html'), 'puede').replace(/\s+/g, ' ');
  for (const t of ['nombre', 'pausa', 'horarios', 'estafas', 'español', 'respaldo']) assert.ok(es.toLowerCase().includes(t), `es: falta «${t}»`);
  const en = seccion(leer('en/index.html'), 'puede').replace(/\s+/g, ' ');
  for (const t of ['name', 'pause', 'schedule', 'scam', 'Spanish', 'backup']) assert.ok(en.toLowerCase().includes(t.toLowerCase()), `en: falta «${t}»`);
});

test('el menú de arriba lleva a la luz en los dos idiomas', () => {
  assert.match(leer('index.html'), /<nav[\s\S]*href="#luz"[\s\S]*<\/nav>/);
  assert.match(leer('en/index.html'), /<nav[\s\S]*href="#luz"[\s\S]*<\/nav>/);
});

for (const [pagina, palabras] of [
  ['index.html', ['3 a 8 segundos', '10 segundos o más', 'Olvidé la contraseña', 'Tus reglas se quedan', '10 minutos']],
  ['en/index.html', ['3 to 8 seconds', '10 seconds or more', 'I forgot the panel password', 'Your rules stay', '10 minutes']],
]) {
  test(`${pagina}: sección #traba con el botón BOOT y enlazada desde #luz`, () => {
    const html = readFileSync(join(SITIO, pagina), 'utf8');
    const sec = html.match(/<section id="traba"[\s\S]*?<\/section>/);
    assert.ok(sec, 'falta <section id="traba">');
    for (const p of palabras) assert.ok(sec[0].includes(p), `#traba no dice «${p}»`);
    const luz = html.match(/<section id="luz"[\s\S]*?<\/section>/)[0];
    assert.match(luz, /href="#traba"/);
  });
}

test('la página no promete quitar virus (la lista extra es de estafas)', () => {
  for (const pagina of ['index.html', 'en/index.html']) {
    assert.doesNotMatch(readFileSync(join(SITIO, pagina), 'utf8'), /virus/i, pagina);
  }
});

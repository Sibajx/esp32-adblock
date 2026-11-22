// Gráficas de barras en SVG (sin librerías), estadísticas públicas y panel del dueño.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { barras, semanaCorta } from '../js/graficas.js';
import { normalizarEstadisticas, pintarEstadisticas, nombrePais, cargarEstadisticas } from '../js/estadisticas.js';
import { normalizarResumen, pintarResumen, cargarResumen, pintarPermitidos, cambiarPermitido, esDominio } from '../js/dueno.js';

// DOM falso: elementos con hijos, atributos y textContent; sabe crear SVG.
function docFalso(ids = []) {
  function nuevo(tag, ns = null) {
    return {
      tagName: tag, ns, children: [], attrs: {}, hidden: false, disabled: false, _texto: '', oyentes: {},
      addEventListener(t, f) { this.oyentes[t] = f; },
      get textContent() { return this._texto + this.children.map((c) => c.textContent).join(''); },
      set textContent(v) { this._texto = String(v); this.children = []; },
      setAttribute(k, v) { this.attrs[k] = String(v); },
      getAttribute(k) { return this.attrs[k] ?? null; },
      appendChild(h) { this.children.push(h); return h; },
      append(...hs) { for (const h of hs) this.appendChild(h); },
      replaceChildren(...hs) { this.children = []; this._texto = ''; this.append(...hs); },
    };
  }
  const els = {};
  for (const id of ids) els[id] = nuevo('div');
  return {
    els,
    getElementById: (id) => els[id] ?? null,
    createElement: (t) => nuevo(t),
    createElementNS: (ns, t) => nuevo(t, ns),
  };
}

function todos(el, tag) {
  const out = [];
  const ir = (e) => { if (e.tagName === tag) out.push(e); for (const c of e.children) ir(c); };
  ir(el);
  return out;
}

const SVG = 'http://www.w3.org/2000/svg';

test('barras: 3 filas → <svg role="img"> con <title> y 3 <rect>', () => {
  const doc = docFalso();
  const g = barras(doc, [{ v: 'v1.0.0', n: 5 }, { v: 'v1.1.0', n: 12 }, { v: 'dev', n: 0 }],
    { etiqueta: 'v', valor: 'n', titulo: 'Versiones' });
  assert.equal(g.tagName, 'svg');
  assert.equal(g.ns, SVG);
  assert.equal(g.attrs.role, 'img');
  assert.equal(g.children[0].tagName, 'title');
  assert.equal(g.children[0].textContent, 'Versiones');
  const rects = todos(g, 'rect');
  assert.equal(rects.length, 3);
  for (const r of rects) assert.equal(r.ns, SVG);
  // La barra más larga es la del valor mayor; la de 0 mide 0.
  const anchos = rects.map((r) => Number(r.attrs.width));
  assert.ok(anchos[1] > anchos[0]);
  assert.equal(anchos[2], 0);
});

test('barras: sin filas → <p>Todavía no hay datos.</p>', () => {
  const doc = docFalso();
  for (const vacio of [[], null, undefined, 'x']) {
    const p = barras(doc, vacio, { etiqueta: 'a', valor: 'n', titulo: 'T' });
    assert.equal(p.tagName, 'p');
    assert.equal(p.textContent, 'Todavía no hay datos.');
  }
});

test('barras: un nombre con <script> sale como texto, nunca como HTML', () => {
  const doc = docFalso();
  const malo = '<script>alert(1)</script>.ejemplo.com';
  const g = barras(doc, [{ d: malo, n: 3 }], { etiqueta: 'd', valor: 'n', titulo: 'Dominios' });
  const textos = todos(g, 'text').map((t) => t.textContent).join(' ');
  const titulos = todos(g, 'title').map((t) => t.textContent).join(' ');
  assert.ok(titulos.includes(malo));
  assert.ok(textos.includes('<script>'));
  assert.equal(todos(g, 'script').length, 0);
});

test('barras: etiquetas largas se cortan en la gráfica pero quedan completas en el título', () => {
  const doc = docFalso();
  const largo = 'a'.repeat(63) + '.ejemplo.com';
  const g = barras(doc, [{ d: largo, n: 1 }], { etiqueta: 'd', valor: 'n', titulo: 'T' });
  const etiqueta = todos(g, 'text')[0].textContent;
  assert.ok(etiqueta.length <= 19);
  assert.ok(etiqueta.endsWith('…'));
  assert.ok(todos(g, 'title').some((t) => t.textContent.includes(largo)));
});

test('barras: acepta funciones para etiqueta y valor, y valores raros cuentan como 0', () => {
  const doc = docFalso();
  const g = barras(doc, [{ x: 'A', y: Number.NaN }, { x: 'B', y: -3 }, { x: 'C', y: '7' }],
    { etiqueta: (f) => `País ${f.x}`, valor: (f) => f.y, titulo: 'T' });
  const anchos = todos(g, 'rect').map((r) => Number(r.attrs.width));
  assert.deepEqual(anchos.slice(0, 2), [0, 0]);
  assert.ok(anchos[2] > 0);
  assert.ok(todos(g, 'text').some((t) => t.textContent === 'País A'));
});

test('barras: 200 filas no rompen y la altura crece', () => {
  const doc = docFalso();
  const filas = Array.from({ length: 200 }, (_, i) => ({ p: `P${i}`, n: i }));
  const g = barras(doc, filas, { etiqueta: 'p', valor: 'n', titulo: 'Países' });
  assert.equal(todos(g, 'rect').length, 200);
  const alto = Number(g.attrs.viewBox.split(' ')[3]);
  assert.ok(alto > 200 * 20);
});

test('semanaCorta: "2026-11-16" → "16 nov"; lo raro → ""', () => {
  assert.equal(semanaCorta('2026-11-16'), '16 nov');
  assert.equal(semanaCorta('2026-01-05'), '5 ene');
  assert.equal(semanaCorta('2026-12-28'), '28 dic');
  for (const malo of ['2026-13-01', '2026-11-32', '16/11/2026', '', null, '<b>']) assert.equal(semanaCorta(malo), '');
});

// --- estadísticas públicas ---

const EST = {
  placas7d: 12,
  versiones: [{ v: 'v1.0.0', n: 7 }, { v: 'dev', n: 5 }],
  paises: [{ pais: 'MX', n: 7 }, { pais: 'otros', n: 5 }],
  bloqueadasSemanas: Array.from({ length: 12 }, (_, i) => ({ desde: `2026-09-${String(i + 1).padStart(2, '0')}`, n: i * 10 })),
  modems: [{ pais: 'MX', proveedor: 'Telmex', fabricante: 'huawei', menu: 'red_local', n: 3 }],
};

test('nombrePais: nombre en español; "otros" y lo raro', () => {
  assert.equal(nombrePais('MX'), 'México');
  assert.equal(nombrePais('otros'), 'Otros países');
  assert.equal(nombrePais('<b>'), 'Otro');
});

test('normalizarEstadisticas: deja solo lo del contrato y descarta lo raro', () => {
  const n = normalizarEstadisticas({ ...EST, extra: 1, versiones: [...EST.versiones, { v: 5, n: 1 }, null],
    modems: [...EST.modems, { pais: 'MX', proveedor: 'x', fabricante: 'linksys', menu: 'red_local', n: 3 }] });
  assert.equal(n.placas7d, 12);
  assert.equal(n.versiones.length, 2);
  assert.equal(n.modems.length, 1);
  assert.equal('extra' in n, false);
  assert.equal(normalizarEstadisticas(null), null);
  assert.equal(normalizarEstadisticas([1]), null);
  assert.equal(normalizarEstadisticas({ placas7d: -1 }).placas7d, 0);
});

test('pintarEstadisticas: llena cada parte; con 0 placas dice que no hay datos', () => {
  const ids = ['placas7d', 'g-versiones', 'g-paises', 'g-semanas', 't-modems', 'est-estado'];
  const doc = docFalso(ids);
  pintarEstadisticas(doc, normalizarEstadisticas(EST));
  assert.equal(doc.els.placas7d.textContent, '12');
  assert.equal(todos(doc.els['g-paises'], 'rect').length, 2);
  assert.ok(doc.els['g-paises'].textContent.includes('México'));
  assert.equal(todos(doc.els['g-semanas'], 'rect').length, 12);
  assert.ok(doc.els['g-semanas'].textContent.includes('1 sep'));
  assert.ok(doc.els['t-modems'].textContent.includes('Huawei'));
  assert.ok(doc.els['t-modems'].textContent.includes('Red local, LAN o DHCP'));

  const doc0 = docFalso(ids);
  pintarEstadisticas(doc0, normalizarEstadisticas({ placas7d: 0, versiones: [], paises: [], bloqueadasSemanas: [], modems: [] }));
  assert.equal(doc0.els.placas7d.textContent, '0');
  assert.ok(doc0.els['g-paises'].textContent.includes('Todavía no hay datos.'));
  assert.ok(doc0.els['t-modems'].textContent.includes('Todavía no hay datos.'));
});

test('cargarEstadisticas: si el servidor falla, lo dice sin romper', async () => {
  const doc = docFalso(['placas7d', 'g-versiones', 'g-paises', 'g-semanas', 't-modems', 'est-estado']);
  await cargarEstadisticas(doc, async () => { throw new Error('x'); });
  assert.match(doc.els['est-estado'].textContent, /No se pudieron cargar/);
  await cargarEstadisticas(doc, async () => new Response('{}', { status: 500 }));
  assert.match(doc.els['est-estado'].textContent, /No se pudieron cargar/);
  let url;
  await cargarEstadisticas(doc, async (u) => { url = u; return new Response(JSON.stringify(EST), { status: 200 }); });
  assert.equal(url, '/api/v1/estadisticas');
  assert.equal(doc.els['est-estado'].textContent, '');
  assert.equal(doc.els.placas7d.textContent, '12');
});

// --- panel del dueño ---

const RES = {
  embudo: [{ paso: 'entro', n: 100 }, { paso: 'conecto', n: 40 }, { paso: 'instalo', n: 30 }, { paso: 'wifi', n: 25 }, { paso: 'panel', n: 20 }],
  temas: [{ tema: 'no_instala', n: 4 }],
  motivos: [{ motivo: 'app_no_funciona', n: 2 }],
  bloqueos: [{ dominio: '<img src=x onerror=alert(1)>.com', n: 9 }, { dominio: 'ads.ejemplo.com', n: 3 }],
  guiasFaltan: [{ fabricante: 'no-se', n: 6 }],
  placasTotal: 31,
};
const IDS_DUENO = ['d-embudo', 'd-temas', 'd-motivos', 'd-bloqueos', 'd-guias', 'd-placas', 'dueno-estado', 'dueno-datos', 'dueno-entrar',
  'd-revisar', 'd-permitidos', 'd-permitidos-total', 'd-permitidos-estado'];

test('pintarResumen: nombres en español y los dominios como texto', () => {
  const doc = docFalso(IDS_DUENO);
  pintarResumen(doc, normalizarResumen(RES));
  assert.equal(doc.els['d-placas'].textContent, '31');
  assert.ok(doc.els['d-embudo'].textContent.includes('Instaló'));
  assert.ok(doc.els['d-temas'].textContent.includes('No puedo instalar SiBlock'));
  assert.ok(doc.els['d-motivos'].textContent.includes('Una app no funciona'));
  assert.ok(doc.els['d-guias'].textContent.includes('No sé / otra'));
  assert.ok(doc.els['d-bloqueos'].textContent.includes('<img src=x onerror=alert(1)>.com'));
  assert.equal(todos(doc.els['d-bloqueos'], 'img').length, 0);
});

test('cargarResumen: 403 o redirección de Access → «Entra con tu correo de dueño.»', async () => {
  for (const res of [new Response('{"error":"acceso"}', { status: 403 }), { ok: false, status: 0, type: 'opaqueredirect' }]) {
    const doc = docFalso(IDS_DUENO);
    await cargarResumen(doc, async () => res);
    assert.equal(doc.els['dueno-estado'].textContent, 'Entra con tu correo de dueño.');
    assert.equal(doc.els['dueno-entrar'].hidden, false);
    assert.equal(doc.els['dueno-datos'].hidden, true);
  }
});

test('cargarResumen: con permiso pinta el resumen; sin red lo dice', async () => {
  const doc = docFalso(IDS_DUENO);
  let opc;
  await cargarResumen(doc, async (u, o) => { opc = o; return new Response(JSON.stringify(RES), { status: 200 }); });
  assert.equal(opc.credentials, 'same-origin');
  assert.equal(opc.redirect, 'manual');
  assert.equal(doc.els['dueno-datos'].hidden, false);
  assert.equal(doc.els['d-placas'].textContent, '31');
  const doc2 = docFalso(IDS_DUENO);
  await cargarResumen(doc2, async () => { throw new Error('x'); });
  assert.match(doc2.els['dueno-estado'].textContent, /No se pudo cargar/);
});

test('juntarPaises: más de 20 países → los 19 más grandes y el resto suma en «otros» (al final)', async () => {
  const { juntarPaises } = await import('../js/estadisticas.js');
  const codigos = Array.from({ length: 30 }, (_, i) => 'A' + String.fromCharCode(65 + i % 26) + (i >= 26 ? '' : '')).slice(0, 26);
  const paises = codigos.map((p, i) => ({ pais: p, n: 100 - i }));
  paises.push({ pais: 'otros', n: 7 });
  const j = juntarPaises(paises);
  assert.equal(j.length, 20);
  assert.equal(j[0].pais, 'AA');
  assert.equal(j[19].pais, 'otros');
  const resto = paises.slice(19, 26).reduce((s, p) => s + p.n, 0) + 7;
  assert.equal(j[19].n, resto);
  // Con pocos, solo ordena y deja «otros» al final.
  assert.deepEqual(juntarPaises([{ pais: 'otros', n: 50 }, { pais: 'MX', n: 5 }, { pais: 'CO', n: 9 }]).map((p) => p.pais), ['CO', 'MX', 'otros']);
});

// --- permitidos de la comunidad (panel del dueño) ---

const RES_P = {
  ...RES,
  bloqueos: [
    { dominio: 'a.ejemplo.com', n: 5, aprobado: true },
    { dominio: 'b.ejemplo.com', n: 1, aprobado: false },
    { dominio: '<img src=x>.com', n: 9, aprobado: 'si' },
  ],
  permitidos: [{ dominio: 'a.ejemplo.com', desde: '2026-11-21' }, { dominio: 'MAL.com', desde: '2026-11-21' }, { dominio: 'c.ejemplo.com', desde: 7 }],
};

test('esDominio: misma regla que el Worker y la placa', () => {
  for (const ok of ['ads.ejemplo.com', 'a-b.c0.mx', 'x.co']) assert.equal(esDominio(ok), true, ok);
  for (const malo of ['ADS.com', 'ejemplo', 'a..b.com', '-a.com', 'a b.com', 'x'.repeat(64) + '.com', '', 7, null]) {
    assert.equal(esDominio(malo), false, String(malo));
  }
});

test('normalizarResumen: aprobado solo con true; permitidos solo con nombres válidos', () => {
  const r = normalizarResumen(RES_P);
  assert.deepEqual(r.bloqueos.map((f) => f.aprobado), [false, true, false]);
  assert.deepEqual(r.permitidos, [{ dominio: 'a.ejemplo.com', desde: '2026-11-21' }, { dominio: 'c.ejemplo.com', desde: '' }]);
  assert.deepEqual(normalizarResumen(RES).permitidos, []);
});

test('pintarPermitidos: «Aprobar» o «Quitar» por sitio, como texto, y la lista con «Quitar»', () => {
  const doc = docFalso(IDS_DUENO);
  const pedidos = [];
  pintarPermitidos(doc, normalizarResumen(RES_P), (d, aprobar) => pedidos.push([d, aprobar]));
  const botones = todos(doc.els['d-revisar'], 'button');
  // El nombre raro no es un dominio válido: no se puede aprobar (el Worker lo rechazaría).
  assert.deepEqual(botones.map((b) => b.textContent), ['Quitar', 'Aprobar']);
  assert.ok(doc.els['d-revisar'].textContent.includes('5 reportes · aprobado'));
  assert.ok(doc.els['d-revisar'].textContent.includes('1 reporte'));
  assert.equal(todos(doc.els['d-revisar'], 'img').length, 0);
  for (const b of botones) assert.equal(b.attrs.type, 'button');
  botones[1].oyentes.click();
  botones[0].oyentes.click();
  assert.deepEqual(pedidos, [['b.ejemplo.com', true], ['a.ejemplo.com', false]]);
  assert.equal(doc.els['d-permitidos-total'].textContent, '2');
  const quitar = todos(doc.els['d-permitidos'], 'button');
  assert.equal(quitar.length, 2);
  assert.ok(doc.els['d-permitidos'].textContent.includes('desde el 2026-11-21'));
  quitar[1].oyentes.click();
  assert.deepEqual(pedidos.at(-1), ['c.ejemplo.com', false]);
});

test('pintarPermitidos: sin nada → avisos en vez de listas vacías', () => {
  const doc = docFalso(IDS_DUENO);
  pintarPermitidos(doc, normalizarResumen({ ...RES, bloqueos: [] }), () => {});
  assert.match(doc.els['d-revisar'].textContent, /Todavía no hay sitios reportados/);
  assert.match(doc.els['d-permitidos'].textContent, /Todavía no apruebas ningún sitio/);
  assert.equal(doc.els['d-permitidos-total'].textContent, '0');
});

test('cambiarPermitido: ruta, cuerpo, cookie de Access y errores en palabras simples', async () => {
  let url, opc;
  assert.equal(await cambiarPermitido('a.ejemplo.com', true, async (u, o) => { url = u; opc = o; return new Response('{"ok":true}'); }), '');
  assert.equal(url, '/api/v1/dueno/permitidos');
  assert.equal(opc.method, 'POST');
  assert.equal(opc.body, '{"dominio":"a.ejemplo.com"}');
  assert.equal(opc.credentials, 'same-origin');
  assert.equal(opc.redirect, 'manual');
  await cambiarPermitido('a.ejemplo.com', false, async (u) => { url = u; return new Response('{"ok":true}'); });
  assert.equal(url, '/api/v1/dueno/permitidos/quitar');
  assert.match(await cambiarPermitido('a.ejemplo.com', true, async () => new Response('{"error":"tope"}', { status: 409 })), /500 sitios/);
  assert.match(await cambiarPermitido('a.ejemplo.com', true, async () => ({ ok: false, status: 0, type: 'opaqueredirect' })), /Vuelve a entrar/);
  assert.match(await cambiarPermitido('a.ejemplo.com', true, async () => { throw new Error('x'); }), /No se pudo guardar/);
  let llamado = false;
  assert.match(await cambiarPermitido('NO VALE', true, async () => { llamado = true; }), /no es válido/);
  assert.equal(llamado, false);
});

test('cargarResumen: «Aprobar» manda el pedido y vuelve a cargar la lista', async () => {
  const doc = docFalso(IDS_DUENO);
  const urls = [];
  let aprobado = false;
  const fetchFn = async (u, o) => {
    urls.push([u, o?.method ?? 'GET']);
    if (u.endsWith('/dueno/permitidos')) { aprobado = true; return new Response('{"ok":true}'); }
    const res = { ...RES, bloqueos: [{ dominio: 'b.ejemplo.com', n: 1, aprobado }], permitidos: aprobado ? [{ dominio: 'b.ejemplo.com', desde: '2026-11-21' }] : [] };
    return new Response(JSON.stringify(res), { status: 200 });
  };
  await cargarResumen(doc, fetchFn);
  const [b] = todos(doc.els['d-revisar'], 'button');
  assert.equal(b.textContent, 'Aprobar');
  await b.oyentes.click();
  await new Promise((r) => setTimeout(r, 0));
  assert.deepEqual(urls.map((x) => x[1]), ['GET', 'POST', 'GET']);
  assert.equal(doc.els['d-permitidos-estado'].textContent, 'Aprobado: b.ejemplo.com.');
  assert.equal(todos(doc.els['d-revisar'], 'button')[0].textContent, 'Quitar');
  assert.equal(doc.els['d-permitidos-total'].textContent, '1');
});

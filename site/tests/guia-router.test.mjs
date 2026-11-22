import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MARCAS, PAISES, FICHAS } from '../js/guias-datos.js';
import {
  paisDesdeIdioma, proveedoresDe, guiaPara, pintarGuia, iniciar, PASOS_GENERALES, IPS_GENERALES,
} from '../js/guia-router.js';

// DOM falso mínimo: solo lo que usan pintarGuia e iniciar.
function docFalso(ids = []) {
  const creados = [];
  function nuevo(tag) {
    const el = {
      tagName: tag.toUpperCase(),
      children: [],
      textContent: '',
      value: '',
      className: '',
      hidden: false,
      oyentes: {},
      appendChild(hijo) { this.children.push(hijo); return hijo; },
      append(...hijos) { for (const h of hijos) this.appendChild(h); },
      replaceChildren(...hijos) { this.children = []; this.textContent = ''; this.append(...hijos); },
      addEventListener(tipo, fn) { (this.oyentes[tipo] ??= []).push(fn); },
      setAttribute(k, v) { this[`attr:${k}`] = String(v); },
      disparar(tipo) { for (const fn of this.oyentes[tipo] ?? []) fn({ target: this }); },
    };
    creados.push(el);
    return el;
  }
  const els = {};
  for (const [id, tag] of ids) els[id] = nuevo(tag);
  creados.length = 0;
  return {
    els,
    creados,
    getElementById: (id) => els[id] ?? null,
    createElement: (tag) => nuevo(tag),
  };
}

function textoDe(el) {
  return [el.textContent, ...el.children.map(textoDe)].join(' ');
}

test('paisDesdeIdioma: los cuatro países conocidos', () => {
  assert.equal(paisDesdeIdioma('es-MX'), 'MX');
  assert.equal(paisDesdeIdioma('es-CO'), 'CO');
  assert.equal(paisDesdeIdioma('es-ES'), 'ES');
  assert.equal(paisDesdeIdioma('en-US'), 'US');
});

test('paisDesdeIdioma: sin región, desconocido o vacío → otro', () => {
  assert.equal(paisDesdeIdioma('es'), 'otro');
  assert.equal(paisDesdeIdioma('fr-FR'), 'otro');
  assert.equal(paisDesdeIdioma('es-419'), 'otro');
  assert.equal(paisDesdeIdioma(undefined), 'otro');
  assert.equal(paisDesdeIdioma(null), 'otro');
  assert.equal(paisDesdeIdioma(''), 'otro');
  assert.equal(paisDesdeIdioma(42), 'otro');
});

test('paisDesdeIdioma: no distingue mayúsculas ni guion bajo', () => {
  assert.equal(paisDesdeIdioma('es-mx'), 'MX');
  assert.equal(paisDesdeIdioma('es_CO'), 'CO');
});

test('MARCAS: orden pedido y la última es "No sé / otra"', () => {
  assert.deepEqual(MARCAS.map((m) => m.id), [
    'tp-link', 'huawei', 'zte', 'technicolor', 'arris', 'netgear', 'asus', 'mikrotik', 'proveedor', 'no-se',
  ]);
  assert.equal(MARCAS.at(-1).nombre, 'No sé / otra');
});

test('PAISES: cuatro países con cuatro proveedores y "Otro país" vacío', () => {
  assert.deepEqual(PAISES.map((p) => p.id), ['MX', 'CO', 'ES', 'US', 'otro']);
  for (const p of PAISES.slice(0, 4)) assert.equal(p.proveedores.length, 4, p.id);
  assert.deepEqual(PAISES.at(-1).proveedores, []);
});

test('ids únicos dentro de cada lista', () => {
  const unicos = (xs) => new Set(xs).size === xs.length;
  assert.ok(unicos(MARCAS.map((m) => m.id)));
  assert.ok(unicos(PAISES.map((p) => p.id)));
  for (const p of PAISES) assert.ok(unicos(proveedoresDe(p.id).map((x) => x.id)), p.id);
});

test('FICHAS empieza vacío: no hay datos sin fuente', () => {
  assert.deepEqual(Object.keys(FICHAS), []);
});

test('proveedoresDe: México trae 5 y termina en "otro"', () => {
  const ps = proveedoresDe('MX');
  assert.equal(ps.length, 5);
  assert.deepEqual(ps.at(-1), { id: 'otro', nombre: 'Otro proveedor' });
  assert.equal(ps[0].nombre, 'Telmex (Infinitum)');
});

test('proveedoresDe: país desconocido u "otro" → solo "otro"', () => {
  assert.deepEqual(proveedoresDe('zz').map((p) => p.id), ['otro']);
  assert.deepEqual(proveedoresDe('otro').map((p) => p.id), ['otro']);
  assert.deepEqual(proveedoresDe(undefined).map((p) => p.id), ['otro']);
});

test('proveedoresDe: no deja cambiar la lista de datos', () => {
  proveedoresDe('MX').push({ id: 'x', nombre: 'x' });
  assert.equal(proveedoresDe('MX').length, 5);
});

test('guiaPara huawei: guía general sin verificar', () => {
  const g = guiaPara({ marca: 'huawei' });
  assert.equal(g.verificada, false);
  assert.equal(g.fuente, null);
  assert.equal(g.pasos.length, 7);
  assert.deepEqual(g.ips, ['192.168.1.1', '192.168.0.1']);
  assert.match(g.titulo, /Huawei/);
  assert.match(g.aviso, /^Falta verificar: esta guía es general para tu marca/);
  assert.deepEqual(g.pasos, PASOS_GENERALES);
  assert.deepEqual(IPS_GENERALES, ['192.168.1.1', '192.168.0.1']);
});

test('guiaPara por proveedor: nombre en el título y aviso de proveedor', () => {
  const g = guiaPara({ marca: 'proveedor', pais: 'MX', proveedor: 'telmex' });
  assert.match(g.titulo, /Telmex/);
  assert.equal(g.verificada, false);
  assert.match(g.aviso, /^Falta verificar: esta guía es general para tu proveedor/);
});

test('guiaPara: la marca manda sobre el proveedor en el título', () => {
  const g = guiaPara({ marca: 'tp-link', pais: 'CO', proveedor: 'claro' });
  assert.match(g.titulo, /TP-Link/);
});

test('guiaPara: sin datos, "No sé" o ids raros → guía general', () => {
  for (const sel of [{}, { marca: 'no-se' }, { marca: 'zzz', pais: 'zz', proveedor: '<b>' }, undefined]) {
    const g = guiaPara(sel);
    assert.equal(g.pasos.length, 7);
    assert.equal(g.verificada, false);
    assert.doesNotMatch(g.titulo, /zzz|<b>/);
  }
});

test('guiaPara: el proveedor "otro" no se trata como proveedor conocido', () => {
  const g = guiaPara({ pais: 'MX', proveedor: 'otro' });
  assert.doesNotMatch(g.titulo, /Otro proveedor/);
});

test('guiaPara: los pasos devueltos son copia', () => {
  guiaPara({ marca: 'asus' }).pasos.push('x');
  assert.equal(guiaPara({ marca: 'asus' }).pasos.length, 7);
});

test('los pasos generales dicen lo importante', () => {
  const todo = PASOS_GENERALES.join(' ');
  assert.match(todo, /192\.168\.1\.1/);
  assert.match(todo, /Deja vacío el DNS secundario/);
  assert.match(todo, /IPv6/);
  assert.match(todo, /Abre tu panel desde ese aparato/);
  // El panel no tiene un botón "Comprobar" a secas: la prueba corre sola al abrirlo.
  assert.doesNotMatch(todo, /pulsa Comprobar/);
  // Módems que no dejan el segundo DNS vacío: misma indicación que la página de inicio.
  assert.match(todo, /Si te obliga a llenarlo, pon la misma dirección de tu SiBlock/);
});

test('la copia en HTML de los pasos generales es igual a la del JS', async () => {
  const { readFileSync } = await import('node:fs');
  const html = readFileSync(new URL('../guia-router.html', import.meta.url), 'utf8');
  for (const paso of PASOS_GENERALES) assert.ok(html.includes(`<li>${paso}</li>`), `falta en el HTML: ${paso}`);
});

test('pintarGuia: solo texto, nunca HTML', () => {
  const doc = docFalso([['guia', 'section']]);
  const malo = '<img src=x onerror=alert(1)>';
  pintarGuia(doc, {
    titulo: malo, aviso: malo, ips: [malo], pasos: [malo], verificada: false, fuente: malo,
  });
  const sec = doc.els.guia;
  assert.ok(textoDe(sec).includes(malo));
  assert.equal(doc.creados.filter((e) => e.tagName === 'IMG').length, 0);
});

test('pintarGuia: título, aviso, direcciones y 7 pasos', () => {
  const doc = docFalso([['guia', 'section']]);
  pintarGuia(doc, guiaPara({ marca: 'zte' }));
  const sec = doc.els.guia;
  const tags = doc.creados.map((e) => e.tagName);
  assert.ok(tags.includes('H2'));
  assert.equal(doc.creados.filter((e) => e.tagName === 'OL').length, 1);
  const ol = doc.creados.find((e) => e.tagName === 'OL');
  assert.equal(ol.children.length, 7);
  assert.match(textoDe(sec), /Falta verificar/);
  assert.match(textoDe(sec), /192\.168\.0\.1/);
});

test('pintarGuia: con fuente la muestra y sin aviso de "falta verificar"', () => {
  const doc = docFalso([['guia', 'section']]);
  pintarGuia(doc, {
    titulo: 'X', aviso: null, ips: ['10.0.0.1'], pasos: ['a'], verificada: true, fuente: 'Manual del fabricante',
  });
  const t = textoDe(doc.els.guia);
  assert.match(t, /Manual del fabricante/);
  assert.doesNotMatch(t, /Falta verificar/);
});

test('pintarGuia: repintar no acumula', () => {
  const doc = docFalso([['guia', 'section']]);
  pintarGuia(doc, guiaPara({ marca: 'zte' }));
  const n = doc.els.guia.children.length;
  pintarGuia(doc, guiaPara({ marca: 'asus' }));
  assert.equal(doc.els.guia.children.length, n);
  assert.doesNotMatch(textoDe(doc.els.guia), /ZTE/);
});

test('pintarGuia: sin la sección no truena', () => {
  const doc = docFalso([]);
  assert.doesNotThrow(() => pintarGuia(doc, guiaPara({})));
});

function docPagina() {
  return docFalso([['marca', 'select'], ['pais', 'select'], ['proveedor', 'select'], ['guia', 'section']]);
}

test('iniciar: llena las listas y elige el país por el idioma', () => {
  const doc = docPagina();
  iniciar(doc, { language: 'es-CO' });
  const { marca, pais, proveedor, guia } = doc.els;
  assert.equal(marca.children.length, MARCAS.length + 1); // + "Elige"
  assert.equal(pais.children.length, PAISES.length);
  assert.equal(pais.value, 'CO');
  assert.deepEqual(proveedor.children.map((o) => o.value).slice(1), ['claro', 'movistar', 'tigo', 'etb', 'otro']);
  assert.ok(guia.children.length > 0);
});

test('iniciar: idioma desconocido → "Otro país" con solo "Otro proveedor"', () => {
  const doc = docPagina();
  iniciar(doc, { language: 'de-DE' });
  assert.equal(doc.els.pais.value, 'otro');
  assert.deepEqual(doc.els.proveedor.children.map((o) => o.value).slice(1), ['otro']);
});

test('iniciar: sin navigator no truena', () => {
  const doc = docPagina();
  assert.doesNotThrow(() => iniciar(doc, undefined));
  assert.equal(doc.els.pais.value, 'otro');
});

test('iniciar: cambiar de país rehace la lista de proveedores y repinta', () => {
  const doc = docPagina();
  iniciar(doc, { language: 'es-MX' });
  const { pais, proveedor, marca, guia } = doc.els;
  proveedor.value = 'telmex';
  proveedor.disparar('change');
  assert.match(textoDe(guia), /Telmex/);
  pais.value = 'ES';
  pais.disparar('change');
  assert.deepEqual(proveedor.children.map((o) => o.value).slice(1), ['movistar', 'vodafone', 'orange', 'digi', 'otro']);
  assert.equal(proveedor.value, '');
  assert.doesNotMatch(textoDe(guia), /Telmex/);
  marca.value = 'netgear';
  marca.disparar('change');
  assert.match(textoDe(guia), /Netgear/);
});

test('iniciar: sin los elementos de la página no truena', () => {
  assert.doesNotThrow(() => iniciar(docFalso([]), { language: 'es-MX' }));
});

test('guiaPara: una ficha con fuente se marca como verificada', () => {
  const fichas = { 'tp-link': { ips: ['192.168.0.1'], menu: 'Avanzado → Red → DHCP', fuente: 'Manual X' } };
  const g = guiaPara({ marca: 'tp-link' }, fichas);
  assert.equal(g.verificada, true);
  assert.equal(g.fuente, 'Manual X');
  assert.equal(g.aviso, null);
  assert.deepEqual(g.ips, ['192.168.0.1']);
  assert.equal(g.pasos[2], 'Ve a: Avanzado → Red → DHCP.');
  const p = guiaPara({ pais: 'MX', proveedor: 'izzi' }, { 'MX:izzi': { fuente: 'Soporte de izzi' } });
  assert.equal(p.verificada, true);
});

test('guiaPara: una ficha sin fuente no cuenta', () => {
  const g = guiaPara({ marca: 'tp-link' }, { 'tp-link': { ips: ['1.2.3.4'], menu: 'X' } });
  assert.equal(g.verificada, false);
  assert.deepEqual(g.ips, ['192.168.1.1', '192.168.0.1']);
});

test('iniciar: muestra el selector, que sin JavaScript queda oculto', () => {
  const doc = docFalso([['marca', 'select'], ['pais', 'select'], ['proveedor', 'select'], ['guia', 'section'], ['elegir', 'div']]);
  doc.els.elegir.hidden = true;
  iniciar(doc, { language: 'es-MX' });
  assert.equal(doc.els.elegir.hidden, false);
});

// --- Fase 3: país desde /api/v1/pais ---

const respuesta = (obj, status = 200) => async () => new Response(JSON.stringify(obj), { status });

test('iniciar: si el servidor dice CO, empieza en Colombia aunque el idioma sea es-MX', async () => {
  const doc = docPagina();
  let url;
  await iniciar(doc, { language: 'es-MX' }, async (u) => { url = u; return respuesta({ pais: 'CO', proveedor: 'X' })(); });
  assert.equal(url, '/api/v1/pais');
  assert.equal(doc.els.pais.value, 'CO');
  assert.ok(doc.els.proveedor.children.some((o) => o.value === 'tigo'));
});

test('iniciar: si el servidor falla o no sabe, se queda con el país del idioma', async () => {
  for (const f of [async () => { throw new Error('x'); }, respuesta({}, 404), respuesta({ pais: '' }), respuesta({ pais: 'FR' }), respuesta({ pais: 'otro' })]) {
    const doc = docPagina();
    await iniciar(doc, { language: 'es-MX' }, f);
    assert.equal(doc.els.pais.value, 'MX');
  }
});

test('iniciar: si la persona ya cambió el país, la respuesta tardía no lo pisa', async () => {
  const doc = docPagina();
  let soltar;
  const espera = new Promise((r) => { soltar = r; });
  const listo = iniciar(doc, { language: 'es-MX' }, async () => { await espera; return respuesta({ pais: 'CO' })(); });
  doc.els.pais.value = 'ES';
  doc.els.pais.disparar('change');
  soltar();
  await listo;
  assert.equal(doc.els.pais.value, 'ES');
});

test('iniciar: si la persona ya eligió proveedor, la respuesta tardía no lo borra', async () => {
  const doc = docPagina();
  let soltar;
  const espera = new Promise((r) => { soltar = r; });
  const listo = iniciar(doc, { language: 'es-MX' }, async () => { await espera; return respuesta({ pais: 'CO' })(); });
  doc.els.proveedor.value = 'telmex';
  doc.els.proveedor.disparar('change');
  soltar();
  await listo;
  assert.equal(doc.els.pais.value, 'MX');
  assert.equal(doc.els.proveedor.value, 'telmex');
});

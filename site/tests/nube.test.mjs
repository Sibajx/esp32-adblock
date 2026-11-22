// Ayudas de la página para hablar con el Worker de SiBlock: analítica sin cookies, Turnstile,
// envíos con fetch, embudo con sendBeacon y país desde /api/v1/pais.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CONFIG } from '../js/config.js';
import {
  cargarAnalitica, cargarTurnstile, enviar, mensajeDeError, contarPaso, pedirPais, PASOS, tokenValido,
} from '../js/nube.js';

const SITIO = join(dirname(fileURLToPath(import.meta.url)), '..');

// DOM falso mínimo: head, createElement y getElementById.
function docFalso() {
  const porId = {};
  const agregados = [];
  const nuevo = (tag) => ({
    tagName: tag.toUpperCase(),
    attrs: {},
    oyentes: {},
    setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'id') this.id = String(v); },
    getAttribute(k) { return this.attrs[k] ?? null; },
    addEventListener(t, fn) { (this.oyentes[t] ??= []).push(fn); },
  });
  const head = {
    appendChild(el) {
      agregados.push(el);
      if (el.id) porId[el.id] = el;
      return el;
    },
  };
  return { head, agregados, createElement: nuevo, getElementById: (id) => porId[id] ?? null };
}

test('config.js cumple el contrato: api y tres valores vacíos que llena CI', () => {
  assert.deepEqual({ ...CONFIG }, { api: '/api/v1', analiticaToken: '', turnstileClave: '', apoyoUrl: '' });
  assert.ok(Object.isFrozen(CONFIG));
  const texto = readFileSync(join(SITIO, 'js/config.js'), 'utf8');
  assert.match(texto, /export const CONFIG = Object\.freeze\(\{ api: '\/api\/v1', analiticaToken: '', turnstileClave: '', apoyoUrl: '' \}\);/);
});

test('version.json cumple el contrato', () => {
  const v = JSON.parse(readFileSync(join(SITIO, 'version.json'), 'utf8'));
  assert.deepEqual(v, { version: 'v0.0.0', url: 'https://siblock.s-dev.win/#paso-instalar' });
});

test('cargarAnalitica: sin token no agrega nada', () => {
  const doc = docFalso();
  assert.equal(cargarAnalitica(doc, { analiticaToken: '' }), false);
  assert.equal(doc.agregados.length, 0);
});

test('cargarAnalitica: con token agrega un solo script aunque se llame dos veces', () => {
  const doc = docFalso();
  const config = { analiticaToken: 'abc123_-XYZ' };
  assert.equal(cargarAnalitica(doc, config), true);
  assert.equal(cargarAnalitica(doc, config), true);
  assert.equal(doc.agregados.length, 1);
  const s = doc.agregados[0];
  assert.equal(s.tagName, 'SCRIPT');
  assert.equal(s.src, 'https://static.cloudflareinsights.com/beacon.min.js');
  assert.equal(s.defer, true);
  assert.deepEqual(JSON.parse(s.attrs['data-cf-beacon']), { token: 'abc123_-XYZ' });
});

test('cargarAnalitica: un token con caracteres raros no se usa', () => {
  for (const malo of ['a"b', "a'}", '<script>', 'a b', 'x'.repeat(200), 42, null]) {
    const doc = docFalso();
    assert.equal(cargarAnalitica(doc, { analiticaToken: malo }), false, String(malo));
    assert.equal(doc.agregados.length, 0);
  }
});

test('cargarAnalitica: respeta "no rastrear" (GPC o DNT) del navegador', () => {
  for (const nav of [{ globalPrivacyControl: true }, { doNotTrack: '1' }]) {
    const doc = docFalso();
    assert.equal(cargarAnalitica(doc, { analiticaToken: 'abc' }, nav), false);
    assert.equal(doc.agregados.length, 0);
  }
});

test('tokenValido: solo letras, números, guion y guion bajo', () => {
  assert.equal(tokenValido('0x4AAAAAAA_b-c'), true);
  assert.equal(tokenValido(''), false);
  assert.equal(tokenValido('a.b'), false);
});

test('cargarTurnstile: sin clave → false y nada cargado', () => {
  const doc = docFalso();
  assert.equal(cargarTurnstile(doc, { turnstileClave: '' }), false);
  assert.equal(doc.agregados.length, 0);
});

test('cargarTurnstile: con clave carga el script una vez y avisa al cargar', () => {
  const doc = docFalso();
  let avisos = 0;
  assert.equal(cargarTurnstile(doc, { turnstileClave: '0x4AAA' }, () => avisos++), true);
  assert.equal(cargarTurnstile(doc, { turnstileClave: '0x4AAA' }), true);
  assert.equal(doc.agregados.length, 1);
  const s = doc.agregados[0];
  assert.equal(s.src, 'https://challenges.cloudflare.com/turnstile/v0/api.js');
  for (const fn of s.oyentes.load ?? []) fn();
  assert.equal(avisos, 1);
});

test('enviar: 200 → {ok:true} y manda JSON por POST a /api/v1 + ruta', async () => {
  let visto;
  const fetchFalso = async (url, opc) => { visto = { url, opc }; return new Response('{"ok":true}', { status: 200 }); };
  assert.deepEqual(await enviar('/reporte/modem', { a: 1 }, fetchFalso), { ok: true });
  assert.equal(visto.url, '/api/v1/reporte/modem');
  assert.equal(visto.opc.method, 'POST');
  assert.equal(visto.opc.headers['Content-Type'], 'application/json');
  assert.equal(visto.opc.body, '{"a":1}');
  assert.equal(visto.opc.credentials, 'omit');
});

test('enviar: 429 → limite; códigos del contrato pasan tal cual', async () => {
  const con = (status, cuerpo) => async () => new Response(cuerpo, { status });
  assert.deepEqual(await enviar('/x', {}, con(429, '{"error":"limite"}')), { ok: false, error: 'limite' });
  assert.deepEqual(await enviar('/x', {}, con(429, 'no es json')), { ok: false, error: 'limite' });
  assert.deepEqual(await enviar('/x', {}, con(403, '{"error":"turnstile"}')), { ok: false, error: 'turnstile' });
  assert.deepEqual(await enviar('/x', {}, con(400, '{"error":"datos"}')), { ok: false, error: 'datos' });
  assert.deepEqual(await enviar('/x', {}, con(413, '')), { ok: false, error: 'grande' });
  assert.deepEqual(await enviar('/x', {}, con(500, '{"error":"<b>raro</b>"}')), { ok: false, error: 'interno' });
  assert.deepEqual(await enviar('/x', {}, con(404, '')), { ok: false, error: 'no_existe' });
});

test('enviar: red caída → sin_internet', async () => {
  const caido = async () => { throw new TypeError('Failed to fetch'); };
  assert.deepEqual(await enviar('/x', {}, caido), { ok: false, error: 'sin_internet' });
});

test('mensajeDeError: texto simple para cada código y uno general para lo demás', () => {
  for (const c of ['datos', 'origen', 'turnstile', 'acceso', 'no_existe', 'metodo', 'grande', 'limite', 'interno', 'sin_internet']) {
    const t = mensajeDeError(c);
    assert.equal(typeof t, 'string');
    assert.ok(t.length > 10, c);
    assert.doesNotMatch(t, /error|HTTP|\d{3}/i, c);
  }
  assert.equal(mensajeDeError('<script>'), mensajeDeError('interno'));
  assert.match(mensajeDeError('limite'), /espera/i);
});

test('contarPaso: solo los pasos del contrato y por sendBeacon', () => {
  assert.deepEqual([...PASOS], ['entro', 'conecto', 'instalo', 'wifi', 'panel']);
  const vistos = [];
  const nav = { sendBeacon: (url, datos) => { vistos.push({ url, datos }); return true; } };
  assert.equal(contarPaso('raro', nav), false);
  assert.equal(contarPaso('__proto__', nav), false);
  assert.equal(vistos.length, 0);
  assert.equal(contarPaso('entro', nav), true);
  assert.equal(vistos.length, 1);
  assert.equal(vistos[0].url, '/api/v1/paso');
});

test('contarPaso: el cuerpo es solo {"p":paso}', async () => {
  let datos;
  const nav = { sendBeacon: (url, d) => { datos = d; return true; } };
  contarPaso('panel', nav);
  const texto = typeof datos === 'string' ? datos : await datos.text();
  assert.equal(texto, '{"p":"panel"}');
});

test('contarPaso: sin sendBeacon, con "no rastrear" o si sendBeacon truena → false sin romper', () => {
  assert.equal(contarPaso('entro', {}), false);
  assert.equal(contarPaso('entro', undefined), false);
  let llamado = false;
  assert.equal(contarPaso('entro', { globalPrivacyControl: true, sendBeacon: () => { llamado = true; } }), false);
  assert.equal(llamado, false);
  assert.equal(contarPaso('entro', { sendBeacon: () => { throw new Error('x'); } }), false);
});

test('pedirPais: respuesta buena → {pais, proveedor}', async () => {
  let url;
  const f = async (u) => { url = u; return new Response('{"pais":"CO","proveedor":"Ejemplo S.A."}', { status: 200 }); };
  assert.deepEqual(await pedirPais(f), { pais: 'CO', proveedor: 'Ejemplo S.A.' });
  assert.equal(url, '/api/v1/pais');
});

test('pedirPais: falla, 404, JSON raro o valores raros → null o limpio', async () => {
  assert.equal(await pedirPais(async () => { throw new Error('x'); }), null);
  assert.equal(await pedirPais(async () => new Response('', { status: 404 })), null);
  assert.equal(await pedirPais(async () => new Response('[1]', { status: 200 })), null);
  assert.equal(await pedirPais(async () => new Response('no', { status: 200 })), null);
  const raro = await pedirPais(async () => new Response(JSON.stringify({ pais: 'mx<', proveedor: 'a\u0000b'.repeat(80) }), { status: 200 }));
  assert.equal(raro.pais, '');
  assert.ok(raro.proveedor.length <= 100);
  assert.doesNotMatch(raro.proveedor, /\u0000/);
});

test('montarTurnstile: sin clave → no está listo y no hay token', async () => {
  const { montarTurnstile } = await import('../js/nube.js');
  const doc = docFalso();
  const ts = montarTurnstile(doc, {}, { turnstileClave: '' });
  assert.equal(ts.listo, false);
  assert.equal(ts.token(), '');
  assert.doesNotThrow(() => ts.reiniciar());
});

test('montarTurnstile: dibuja el widget al cargar, guarda el token y lo borra al reiniciar o vencer', async () => {
  const { montarTurnstile } = await import('../js/nube.js');
  const doc = docFalso();
  const contenedor = {};
  let opciones;
  const reinicios = [];
  globalThis.turnstile = {
    render: (el, o) => { assert.equal(el, contenedor); opciones = o; return 'w1'; },
    reset: (w) => reinicios.push(w),
  };
  try {
    const ts = montarTurnstile(doc, contenedor, { turnstileClave: 'clave1' });
    assert.equal(ts.listo, true);
    for (const fn of doc.agregados[0].oyentes.load ?? []) fn();
    assert.equal(opciones.sitekey, 'clave1');
    assert.equal(opciones.language, 'es');
    opciones.callback('tok');
    assert.equal(ts.token(), 'tok');
    ts.reiniciar();
    assert.equal(ts.token(), '');
    assert.deepEqual(reinicios, ['w1']);
    opciones.callback('tok2');
    opciones['expired-callback']();
    assert.equal(ts.token(), '');
  } finally {
    delete globalThis.turnstile;
  }
});

// --- Embudo en la página de instalación (js/instalar.js) ---

function dialogoFalso() {
  return {
    localName: 'ewt-install-dialog',
    oyentes: {},
    addEventListener(t, fn) { (this.oyentes[t] ??= []).push(fn); },
    cerrar() { for (const fn of this.oyentes.closed ?? []) fn(); },
  };
}

function relojFalso() {
  const tareas = new Map();
  let sig = 1;
  return {
    cada: (fn) => { tareas.set(sig, fn); return sig++; },
    parar: (id) => tareas.delete(id),
    tic() { for (const fn of [...tareas.values()]) fn(); },
    activas: () => tareas.size,
  };
}

test('seguirDialogo: cuenta conecto al abrir, instalo al terminar y wifi al pasar a "con WiFi"', async () => {
  const { seguirDialogo } = await import('../js/instalar.js');
  const contados = [];
  const reloj = relojFalso();
  const d = dialogoFalso();
  seguirDialogo(d, (p) => contados.push(p), reloj.cada, reloj.parar);
  assert.deepEqual(contados, ['conecto']);
  d._installState = { state: 'writing' };
  reloj.tic();
  assert.deepEqual(contados, ['conecto']);
  d._installState = { state: 'finished' };
  d._client = { state: 2 };
  reloj.tic();
  assert.deepEqual(contados, ['conecto', 'instalo']);
  d._client = { state: 4 };
  reloj.tic();
  assert.deepEqual(contados, ['conecto', 'instalo', 'wifi']);
  d.cerrar();
  assert.equal(reloj.activas(), 0);
});

test('seguirDialogo: una placa que ya tenía WiFi no cuenta wifi', async () => {
  const { seguirDialogo } = await import('../js/instalar.js');
  const contados = [];
  const reloj = relojFalso();
  const d = dialogoFalso();
  d._client = { state: 4 };
  seguirDialogo(d, (p) => contados.push(p), reloj.cada, reloj.parar);
  reloj.tic();
  d.cerrar();
  assert.deepEqual(contados, ['conecto']);
});

test('iniciarEmbudo: entro al cargar, cada paso una sola vez, panel al tocar el enlace y el diálogo cuando aparece', async () => {
  const { iniciarEmbudo } = await import('../js/instalar.js');
  const contados = [];
  const enlace = { oyentes: {}, addEventListener(t, fn) { (this.oyentes[t] ??= []).push(fn); } };
  let observador;
  class Observador {
    constructor(fn) { this.fn = fn; observador = this; }
    observe(nodo, opc) { this.nodo = nodo; this.opc = opc; }
  }
  const body = {};
  const doc = { body, getElementById: (id) => (id === 'abrir-panel' ? enlace : null) };
  const reloj = relojFalso();
  iniciarEmbudo(doc, (p) => contados.push(p), Observador, reloj.cada, reloj.parar);
  assert.deepEqual(contados, ['entro']);
  assert.equal(observador.nodo, body);
  assert.equal(observador.opc.childList, true);
  observador.fn([{ addedNodes: [{ localName: 'div' }, dialogoFalso()] }]);
  observador.fn([{ addedNodes: [dialogoFalso()] }]); // segunda vez: no vuelve a contar
  enlace.oyentes.click[0]();
  enlace.oyentes.click[0]();
  assert.deepEqual(contados, ['entro', 'conecto', 'panel']);
});

test('iniciarEmbudo: sin MutationObserver ni enlace no truena', async () => {
  const { iniciarEmbudo } = await import('../js/instalar.js');
  const contados = [];
  assert.doesNotThrow(() => iniciarEmbudo({ getElementById: () => null }, (p) => contados.push(p), undefined));
  assert.deepEqual(contados, ['entro']);
});

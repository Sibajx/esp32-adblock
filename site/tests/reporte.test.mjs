// Formularios de reporte de la página: módem y "no debió bloquearse".
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MARCAS } from '../js/guias-datos.js';
import {
  idDesdeFragmento, idNuevo, cuerpoModem, cuerpoBloqueo, dominioValido, FABRICANTES, MENUS, MOTIVOS,
  pintarProveedor,
} from '../js/reporte.js';

const ID = '0123456789abcdef0123456789abcdef';
const TOKEN = 'token-de-turnstile';

test('listas iguales al contrato', () => {
  assert.deepEqual(FABRICANTES.map((f) => f.id),
    ['tp-link', 'huawei', 'zte', 'technicolor', 'arris', 'netgear', 'asus', 'mikrotik', 'proveedor', 'no-se']);
  assert.deepEqual(FABRICANTES.map((f) => f.id), MARCAS.map((m) => m.id));
  assert.deepEqual(MENUS.map((m) => m.id), ['internet_wan', 'red_local', 'avanzado', 'otro']);
  assert.deepEqual(MOTIVOS.map((m) => m.id), ['pagina_no_abre', 'app_no_funciona', 'juego_no_funciona', 'otro']);
});

test('idDesdeFragmento: 32 hex en minúsculas → id', () => {
  assert.equal(idDesdeFragmento('#i=' + ID), ID);
});

test('idDesdeFragmento: lo demás → null', () => {
  for (const malo of ['#i=ABC', '#i=<b>', '', '#i=' + ID + 'a', '#i=' + ID.slice(1), '#i=' + ID.toUpperCase(),
    '#x=' + ID, 'i=' + ID, '#i=' + ID + '&x=1', null, undefined, 42, '#i=' + ID.slice(0, 31) + 'g']) {
    assert.equal(idDesdeFragmento(malo), null, String(malo));
  }
});

test('idNuevo: 32 hex con getRandomValues', () => {
  const falso = { getRandomValues: (a) => { for (let i = 0; i < a.length; i++) a[i] = i; return a; } };
  assert.equal(idNuevo(falso), '000102030405060708090a0b0c0d0e0f');
  const real = idNuevo();
  assert.match(real, /^[0-9a-f]{32}$/);
  assert.notEqual(real, idNuevo());
});

test('cuerpoModem: forma exacta del contrato con origen pagina', () => {
  assert.deepEqual(cuerpoModem(ID, { fabricante: 'huawei', dnsOk: 'si', menu: 'internet_wan', turnstile: TOKEN }),
    { i: ID, origen: 'pagina', fabricante: 'huawei', dns_ok: true, menu: 'internet_wan', turnstile: TOKEN });
  assert.equal(cuerpoModem(ID, { fabricante: 'zte', dnsOk: 'no', menu: 'otro', turnstile: TOKEN }).dns_ok, false);
});

test('cuerpoModem: algo fuera de las listas → null', () => {
  const bueno = { fabricante: 'huawei', dnsOk: 'si', menu: 'internet_wan', turnstile: TOKEN };
  assert.equal(cuerpoModem(ID, { ...bueno, fabricante: 'linksys' }), null);
  assert.equal(cuerpoModem(ID, { ...bueno, fabricante: '__proto__' }), null);
  assert.equal(cuerpoModem(ID, { ...bueno, menu: '' }), null);
  assert.equal(cuerpoModem(ID, { ...bueno, dnsOk: 'tal vez' }), null);
  assert.equal(cuerpoModem(ID, { ...bueno, turnstile: '' }), null);
  assert.equal(cuerpoModem(ID, { ...bueno, turnstile: 'x'.repeat(2049) }), null);
  assert.equal(cuerpoModem('corto', bueno), null);
  assert.equal(cuerpoModem(ID, null), null);
});

test('cuerpoBloqueo: limpia el dominio y arma el contrato', () => {
  assert.deepEqual(cuerpoBloqueo(ID, { dominio: ' Ads.Ejemplo.COM ', motivo: 'pagina_no_abre', turnstile: TOKEN }),
    { i: ID, origen: 'pagina', dominio: 'ads.ejemplo.com', motivo: 'pagina_no_abre', turnstile: TOKEN });
});

test('cuerpoBloqueo: dominio o motivo malos → null', () => {
  for (const dominio of ['ejemplo', '<img>', 'a..com', '-a.com', 'a-.com', 'a_b.com', 'a.com.', 'https://a.com',
    'a.com/x', 'x'.repeat(64) + '.com', ('a'.repeat(60) + '.').repeat(5) + 'com', '', 'á.com']) {
    assert.equal(cuerpoBloqueo(ID, { dominio, motivo: 'otro', turnstile: TOKEN }), null, dominio);
  }
  assert.equal(cuerpoBloqueo(ID, { dominio: 'a.com', motivo: 'otra', turnstile: TOKEN }), null);
  assert.equal(cuerpoBloqueo(ID, { dominio: 'a.com', motivo: 'otro' }), null);
});

test('dominioValido: la misma regla que la placa', () => {
  for (const ok of ['ads.ejemplo.com', 'a-b.c0', 'a.b']) assert.equal(dominioValido(ok), true, ok);
  assert.equal(dominioValido('a'.repeat(63) + '.com'), true);
  const d253 = ['a'.repeat(63), 'b'.repeat(63), 'c'.repeat(63), 'd'.repeat(61)].join('.');
  assert.equal(d253.length, 253);
  assert.equal(dominioValido(d253), true);
  assert.equal(dominioValido(d253 + 'd'), false); // 254
  assert.equal(dominioValido('a'.repeat(250) + '.com'), false);
  assert.equal(dominioValido('ADS.com'), false);
});

// DOM falso mínimo para pintar el proveedor.
function elFalso() {
  return { hidden: true, textContent: '' };
}

test('pintarProveedor: muestra la compañía como texto; si no hay, se queda oculto', () => {
  const caja = elFalso();
  const nombre = elFalso();
  const doc = { getElementById: (id) => ({ 'proveedor-visto': caja, 'proveedor-nombre': nombre })[id] ?? null };
  pintarProveedor(doc, { pais: 'MX', proveedor: '<img src=x onerror=alert(1)>' });
  assert.equal(caja.hidden, false);
  assert.equal(nombre.textContent, '<img src=x onerror=alert(1)>');

  const caja2 = elFalso();
  const doc2 = { getElementById: (id) => ({ 'proveedor-visto': caja2, 'proveedor-nombre': elFalso() })[id] ?? null };
  pintarProveedor(doc2, null);
  assert.equal(caja2.hidden, true);
  pintarProveedor(doc2, { pais: '', proveedor: '' });
  assert.equal(caja2.hidden, true);
});

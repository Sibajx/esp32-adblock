// Contáctanos: diagnóstico opcional desde #diag= y validación igual a la del Worker.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { diagDesdeFragmento, textoDiag, validarContacto, TEMAS, pintarDiag } from '../js/contacto.js';

const b64url = (s) => Buffer.from(s, 'utf8').toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const frag = (obj) => '#diag=' + b64url(JSON.stringify(obj));

test('temas iguales al contrato', () => {
  assert.deepEqual(TEMAS.map((t) => t.id), ['no_instala', 'no_bloquea', 'bloqueo_de_mas', 'mi_modem', 'otra']);
});

test('diagDesdeFragmento: base64url bueno → objeto con las cuatro claves', () => {
  const d = { v: 'v1.2.3', heap: 81234, listErr: 'ninguno', codes: ['dns_lejos', 'sin_lista'] };
  assert.deepEqual(diagDesdeFragmento(frag(d)), d);
});

test('diagDesdeFragmento: acepta texto con acentos y emoji (UTF-8)', () => {
  assert.deepEqual(diagDesdeFragmento(frag({ v: 'versión ñ 🙂' })), { v: 'versión ñ 🙂' });
});

test('diagDesdeFragmento: roto, vacío o grande → null', () => {
  for (const malo of ['%%%', '#diag=%%%', '#diag=', '', null, 42, '#i=abc', '#diag=' + b64url('no es json'),
    '#diag=' + b64url('[1,2]'), '#diag=' + b64url('"texto"'), '#diag=' + b64url('null'),
    '#diag=' + b64url(JSON.stringify({ v: 'x'.repeat(700) })), '#diag=' + 'A'.repeat(601), '#diag=ab$cd',
    '#diag=' + b64url('{}'), '#diag=_w' /* byte 0xff: UTF-8 roto */]) {
    assert.equal(diagDesdeFragmento(malo), null, String(malo).slice(0, 40));
  }
});

test('diagDesdeFragmento: quita claves extra y valores raros', () => {
  assert.deepEqual(diagDesdeFragmento(frag({ v: 'x', ip: '1.2.3.4', mac: 'aa:bb' })), { v: 'x' });
  assert.deepEqual(diagDesdeFragmento(frag({ v: 'x', codes: ['<b>', 'dns_lejos', 'A', 7, 'sin lista'] })),
    { v: 'x', codes: ['dns_lejos'] });
  assert.deepEqual(diagDesdeFragmento(frag({ v: 'x'.repeat(101), heap: 5 })), { heap: 5 });
  assert.deepEqual(diagDesdeFragmento(frag({ v: { a: 1 }, heap: -1, listErr: 'ok' })), { listErr: 'ok' });
  assert.deepEqual(diagDesdeFragmento(frag({ v: 'a\u0000b\nc' })), { v: 'abc' });
  const muchos = Array.from({ length: 25 }, (_, i) => 'c_' + 'abcdefghijklmnopqrstuvwxy'[i]);
  assert.equal(diagDesdeFragmento(frag({ codes: muchos })).codes.length, 20);
  const proto = diagDesdeFragmento('#diag=' + b64url('{"__proto__":{"v":"x"},"heap":1}'));
  assert.deepEqual(proto, { heap: 1 });
  assert.equal(proto.v, undefined);
  assert.equal(Object.getPrototypeOf(diagDesdeFragmento(frag({ heap: 1 }))), Object.prototype);
});

test('textoDiag: líneas para leer y adjuntar', () => {
  const t = textoDiag({ v: 'v1.2.3', heap: 81234, listErr: 'ninguno', codes: ['dns_lejos', 'sin_lista'] });
  assert.match(t, /^Versión: v1\.2\.3$/m);
  assert.match(t, /^Memoria libre: 79 KB$/m);
  assert.match(t, /^Último error al bajar la lista: ninguno$/m);
  assert.match(t, /^Avisos: otro aviso \(dns_lejos\); SiBlock no tenía su lista$/m);
  assert.equal(textoDiag({ v: 'v1' }), 'Versión: v1');
  assert.equal(textoDiag(null), '');
  assert.ok(textoDiag({ v: 'x'.repeat(100), heap: 'y'.repeat(100), listErr: 'z'.repeat(100),
    codes: Array(20).fill('a'.repeat(40)) }).length <= 1500);
});

test('textoDiag: los avisos del panel salen en palabras simples, sin códigos', () => {
  const codigos = ['usa_siblock', 'navegador_dns_propio', 'no_usa_siblock', 'ipv6', 'sin_consultas', 'dns_cifrado',
    'private_relay', 'en_pausa', 'aparato_en_pausa', 'horario_todo', 'horario_activo', 'sin_lista', 'sin_internet',
    'router_dns', 'todo_bien', 'windows_dns_cifrado', 'antivirus_dns', 'vpn', 'apple_perfil_dns',
    'android_dns_privado', 'revisar_aparato'];
  const t = textoDiag({ codes: codigos });
  for (const c of codigos) assert.ok(!t.includes(c), `sale el código ${c}`);
  assert.ok(!t.includes('otro aviso'));
  assert.equal(textoDiag({ codes: ['constructor', '__proto__'] }), 'Avisos: otro aviso (constructor); otro aviso (__proto__)');
});

const BUENO = { nombre: 'Ana', correo: 'ana@ejemplo.com', tema: 'no_instala', mensaje: 'No me aparece la placa.', turnstile: 'tok' };

test('validarContacto: bueno → cuerpo del contrato', () => {
  assert.deepEqual(validarContacto(BUENO), {
    ok: true,
    cuerpo: { nombre: 'Ana', correo: 'ana@ejemplo.com', tema: 'no_instala', mensaje: 'No me aparece la placa.', turnstile: 'tok' },
  });
});

test('validarContacto: nombre vacío no va; espacios de sobra se quitan', () => {
  const r = validarContacto({ ...BUENO, nombre: '   ', correo: ' ana@ejemplo.com ', mensaje: '  No me aparece la placa.  ' });
  assert.equal(r.ok, true);
  assert.equal('nombre' in r.cuerpo, false);
  assert.equal(r.cuerpo.correo, 'ana@ejemplo.com');
  assert.equal(r.cuerpo.mensaje, 'No me aparece la placa.');
});

test('validarContacto: correo con salto de línea o raro → campo correo', () => {
  for (const correo of ['a@b.c\r\nBcc: x@y.z', 'a@b.c\nx', 'sin-arroba', 'a@@b.c', 'a@b@c.d', 'a b@c.d', '@b.c', 'a@',
    'x'.repeat(250) + '@b.co', '', undefined, 'a@b.c\u0000']) {
    assert.deepEqual(validarContacto({ ...BUENO, correo }), { ok: false, campo: 'correo' }, String(correo));
  }
});

test('validarContacto: mensaje de 9 o de 2001 → campo mensaje; 10 y 2000 pasan', () => {
  assert.deepEqual(validarContacto({ ...BUENO, mensaje: '123456789' }), { ok: false, campo: 'mensaje' });
  assert.deepEqual(validarContacto({ ...BUENO, mensaje: 'x'.repeat(2001) }), { ok: false, campo: 'mensaje' });
  assert.equal(validarContacto({ ...BUENO, mensaje: '1234567890' }).ok, true);
  assert.equal(validarContacto({ ...BUENO, mensaje: 'x'.repeat(2000) }).ok, true);
  assert.equal(validarContacto({ ...BUENO, mensaje: '🙂'.repeat(1000) }).ok, true);
});

test('validarContacto: tema raro o nombre largo o con saltos', () => {
  assert.deepEqual(validarContacto({ ...BUENO, tema: 'spam' }), { ok: false, campo: 'tema' });
  assert.deepEqual(validarContacto({ ...BUENO, tema: '__proto__' }), { ok: false, campo: 'tema' });
  assert.deepEqual(validarContacto({ ...BUENO, nombre: 'x'.repeat(81) }), { ok: false, campo: 'nombre' });
  assert.equal(validarContacto({ ...BUENO, nombre: 'x'.repeat(80) }).ok, true);
  assert.deepEqual(validarContacto({ ...BUENO, nombre: 'Ana\r\nBcc: x@y.z' }), { ok: false, campo: 'nombre' });
});

test('validarContacto: el diagnóstico solo va si se marca la casilla', () => {
  const diag = { v: 'v1.2.3', heap: 81920 };
  assert.equal('diagnostico' in validarContacto({ ...BUENO, diag, adjuntar: false }).cuerpo, false);
  assert.equal('diagnostico' in validarContacto({ ...BUENO, diag: null, adjuntar: true }).cuerpo, false);
  assert.equal(validarContacto({ ...BUENO, diag, adjuntar: true }).cuerpo.diagnostico, 'Versión: v1.2.3\nMemoria libre: 80 KB');
});

test('pintarDiag: muestra el diagnóstico como texto y la casilla; sin diagnóstico, oculto', () => {
  const caja = { hidden: true };
  const pre = { textContent: '' };
  const doc = { getElementById: (id) => ({ diag: caja, 'diag-texto': pre })[id] ?? null };
  pintarDiag(doc, { v: '<img src=x onerror=alert(1)>' });
  assert.equal(caja.hidden, false);
  assert.equal(pre.textContent, 'Versión: <img src=x onerror=alert(1)>');
  const caja2 = { hidden: true };
  pintarDiag({ getElementById: (id) => ({ diag: caja2, 'diag-texto': { textContent: '' } })[id] ?? null }, null);
  assert.equal(caja2.hidden, true);
});

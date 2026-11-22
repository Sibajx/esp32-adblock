// Textos por idioma (fase 5): mismas claves en español e inglés, ninguno vacío, y instalar.js
// solo usa claves que existen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { TEXTOS, idioma, texto } from '../js/textos.js';

const JS = join(dirname(fileURLToPath(import.meta.url)), '..', 'js');
const docCon = (lang) => ({ documentElement: lang === undefined ? {} : { lang } });

test('TEXTOS.es y TEXTOS.en tienen las mismas claves y ningún texto vacío', () => {
  const es = Object.keys(TEXTOS.es).sort();
  const en = Object.keys(TEXTOS.en).sort();
  assert.ok(es.length > 0);
  assert.deepEqual(en, es);
  for (const lengua of ['es', 'en']) {
    for (const [k, v] of Object.entries(TEXTOS[lengua])) {
      assert.equal(typeof v, 'string', `${lengua}.${k}`);
      assert.ok(v.trim().length > 0, `${lengua}.${k} vacío`);
    }
  }
});

test('los textos no se pueden cambiar desde afuera', () => {
  assert.ok(Object.isFrozen(TEXTOS));
  assert.ok(Object.isFrozen(TEXTOS.es));
  assert.ok(Object.isFrozen(TEXTOS.en));
});

test('idioma: inglés solo si <html lang> empieza con en', () => {
  assert.equal(idioma(docCon('en')), 'en');
  assert.equal(idioma(docCon('en-US')), 'en');
  assert.equal(idioma(docCon('EN-gb')), 'en');
  assert.equal(idioma(docCon('es')), 'es');
  assert.equal(idioma(docCon('es-MX')), 'es');
  assert.equal(idioma(docCon('')), 'es');
  assert.equal(idioma(docCon(undefined)), 'es');
  assert.equal(idioma({}), 'es');
  assert.equal(idioma(null), 'es');
  assert.equal(idioma(undefined), 'es'); // en node no hay document
});

test('texto: da el del idioma, o el español si falta', () => {
  const clave = Object.keys(TEXTOS.es)[0];
  assert.equal(texto(clave, docCon('es')), TEXTOS.es[clave]);
  assert.equal(texto(clave, docCon('en')), TEXTOS.en[clave]);
  assert.equal(texto(clave, {}), TEXTOS.es[clave]);
  assert.equal(texto('no-existe-esta-clave', docCon('en')), '');
  assert.equal(texto('__proto__', docCon('en')), '');
  assert.equal(texto('toString', docCon('es')), '');
});

test('instalar.js importa texto y cada clave que usa existe en los dos idiomas', () => {
  const js = readFileSync(join(JS, 'instalar.js'), 'utf8');
  assert.match(js, /import\s*\{[^}]*\btexto\b[^}]*\}\s*from\s*'\.\/textos\.js'/);
  const claves = [...js.matchAll(/\btexto\(\s*'([^']+)'/g)].map((m) => m[1]);
  assert.ok(claves.length >= 4, 'instalar.js casi no usa texto()');
  for (const c of claves) {
    assert.ok(Object.hasOwn(TEXTOS.es, c), `falta es.${c}`);
    assert.ok(Object.hasOwn(TEXTOS.en, c), `falta en.${c}`);
  }
});

test('instalar.js ya no tiene frases en español escritas a mano', () => {
  const js = readFileSync(join(JS, 'instalar.js'), 'utf8')
    .split('\n').filter((l) => !/^\s*(\/\/|\*|\/\*\*)/.test(l)).join('\n');
  for (const frase of ['par de minutos', 'Leyendo la placa', 'está ocupada', 'siblock.local']) {
    assert.ok(!js.includes(frase), `«${frase}» sigue en instalar.js`);
  }
});

test('en inglés los mensajes del instalador salen en inglés', async () => {
  const { mostrarFallo } = await import('../js/instalar.js');
  const estado = { textContent: '' };
  const doc = {
    documentElement: { lang: 'en' },
    getElementById: (id) => (id === 'respaldo-estado' ? estado : null),
  };
  mostrarFallo(doc);
  assert.equal(estado.textContent, TEXTOS.en.fallo);
  assert.doesNotMatch(estado.textContent, /desconecta/);
});

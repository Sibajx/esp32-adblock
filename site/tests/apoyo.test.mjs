// Botón de apoyo (fase 5): oculto si no hay enlace; solo acepta un https:// limpio.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { enlaceApoyo, ponerApoyo } from '../js/inicio.js';
import { CONFIG } from '../js/config.js';

const SITIO = join(dirname(fileURLToPath(import.meta.url)), '..');
const CONTRATO = "export const CONFIG = Object.freeze({ api: '/api/v1', analiticaToken: '', turnstileClave: '', apoyoUrl: '' });";

test('config.js: la línea export es la del contrato, byte a byte', () => {
  const js = readFileSync(join(SITIO, 'js', 'config.js'), 'utf8');
  const lineas = js.split('\n').filter((l) => l.startsWith('export'));
  assert.deepEqual(lineas, [CONTRATO]);
  assert.match(js, /enlace de apoyo/);
  assert.equal(CONFIG.apoyoUrl, '');
});

test('enlaceApoyo: rechaza lo vacío, lo raro y lo que no es https', () => {
  for (const malo of ['', 'http://x.com', 'https://x.com/a"b', 'javascript:alert(1)', "https://x.com/a'b",
    'https://x.com/<s>', 'https://x.com/a b', 'https://' + 'a'.repeat(101), 'https://x.com/' + 'a'.repeat(151),
    'https://', 'https://x.com?a=1', 'https://x.com#a', 'https://user@x.com', ' https://x.com',
    'https://x.com\n', 'HTTPS://x.com', '//x.com', null, undefined, 42, {}]) {
    assert.equal(enlaceApoyo(malo), '', JSON.stringify(malo));
  }
});

test('enlaceApoyo: deja pasar un enlace https limpio tal cual', () => {
  for (const bueno of ['https://ko-fi.com/siblock', 'https://ko-fi.com', 'https://www.buymeacoffee.com/si_block-1',
    'https://x.com/a.b~c/d-e_f/']) {
    assert.equal(enlaceApoyo(bueno), bueno);
  }
});

function docApoyo(lang = 'es') {
  const a = {
    hidden: true, attrs: {},
    setAttribute(k, v) { this.attrs[k] = String(v); },
  };
  return { a, documentElement: { lang }, getElementById: (id) => (id === 'apoyo' ? a : null) };
}

test('ponerApoyo: sin enlace el botón sigue oculto y sin href', () => {
  for (const url of ['', 'http://x.com', 'javascript:alert(1)']) {
    const doc = docApoyo();
    assert.equal(ponerApoyo(doc, url), false);
    assert.equal(doc.a.hidden, true);
    assert.equal(doc.a.attrs.href, undefined);
  }
});

test('ponerApoyo: con enlace pone href y lo muestra', () => {
  const doc = docApoyo();
  assert.equal(ponerApoyo(doc, 'https://ko-fi.com/siblock'), true);
  assert.equal(doc.a.hidden, false);
  assert.equal(doc.a.attrs.href, 'https://ko-fi.com/siblock');
});

test('ponerApoyo: no falla si la página no tiene el botón', () => {
  assert.equal(ponerApoyo({ getElementById: () => null }, 'https://ko-fi.com/siblock'), false);
});

test('index.html: el botón de apoyo está en el pie, oculto y sin href', () => {
  const html = readFileSync(join(SITIO, 'index.html'), 'utf8');
  const pie = html.match(/<footer[\s\S]*?<\/footer>/i)[0];
  assert.ok(pie.includes('<a id="apoyo" hidden rel="noopener noreferrer">Invítame un café</a>'));
});

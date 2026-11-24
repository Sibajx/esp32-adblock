import { test } from 'node:test';
import assert from 'node:assert/strict';
import { puedeInstalar, prepararPagina, soloPlacas, FILTROS_PLACA } from '../js/instalar.js';

function docFalso() {
  const els = {
    'instalar': { hidden: false },
    'sin-soporte': { hidden: true },
  };
  return {
    els,
    getElementById: (id) => els[id] ?? null,
  };
}

test('puedeInstalar: true con WebSerial', () => {
  assert.equal(puedeInstalar({ serial: {} }), true);
});

test('puedeInstalar: false sin WebSerial', () => {
  assert.equal(puedeInstalar({}), false);
});

test('puedeInstalar: false sin navigator', () => {
  assert.equal(puedeInstalar(undefined), false);
  assert.equal(puedeInstalar(null), false);
});

test('prepararPagina sin WebSerial muestra el aviso y oculta el botón', () => {
  const doc = docFalso();
  prepararPagina(doc, {});
  assert.equal(doc.els['sin-soporte'].hidden, false);
  assert.equal(doc.els['instalar'].hidden, true);
});

test('prepararPagina con WebSerial muestra el botón y oculta el aviso', () => {
  const doc = docFalso();
  doc.els['instalar'].hidden = true;
  doc.els['sin-soporte'].hidden = false;
  prepararPagina(doc, { serial: {} });
  assert.equal(doc.els['sin-soporte'].hidden, true);
  assert.equal(doc.els['instalar'].hidden, false);
});

test('prepararPagina no falla si falta algún elemento', () => {
  assert.doesNotThrow(() => prepararPagina({ getElementById: () => null }, {}));
});

// --- Tarea 12: contraseña de respaldo y "Abrir mi panel" ---
import { mostrarRespaldo, mostrarFallo, conectarRespaldo } from '../js/instalar.js';

function docRespaldo() {
  const ids = ['instalar', 'sin-soporte', 'respaldo', 'ver-respaldo', 'respaldo-estado',
    'respaldo-datos', 'respaldo-red', 'respaldo-clave', 'respaldo-panel', 'abrir-panel', 'respaldo-ip'];
  const els = {};
  for (const id of ids) {
    els[id] = {
      hidden: false, textContent: '', disabled: false, attrs: {},
      setAttribute(k, v) { this.attrs[k] = String(v); },
      removeAttribute(k) { delete this.attrs[k]; },
      addEventListener(tipo, fn) { this.al = fn; },
    };
  }
  els['respaldo-datos'].hidden = true;
  els['respaldo-panel'].hidden = true;
  return { els, getElementById: (id) => els[id] ?? null };
}

test('prepararPagina oculta el bloque de respaldo sin WebSerial', () => {
  const doc = docRespaldo();
  prepararPagina(doc, {});
  assert.equal(doc.els['respaldo'].hidden, true);
  const doc2 = docRespaldo();
  doc2.els['respaldo'].hidden = true;
  prepararPagina(doc2, { serial: {} });
  assert.equal(doc2.els['respaldo'].hidden, false);
});

test('mostrarRespaldo pone red, clave y el enlace al panel', () => {
  const doc = docRespaldo();
  mostrarRespaldo(doc, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: 'http://192.168.1.50/' });
  assert.equal(doc.els['respaldo-red'].textContent, 'SiBlock-0C3F');
  assert.equal(doc.els['respaldo-clave'].textContent, 'abcdefghjk');
  assert.equal(doc.els['respaldo-datos'].hidden, false);
  assert.equal(doc.els['respaldo-panel'].hidden, false);
  assert.equal(doc.els['abrir-panel'].attrs.href, 'http://192.168.1.50/');
  assert.equal(doc.els['respaldo-ip'].textContent, '192.168.1.50');
});

test('mostrarRespaldo no enlaza una URL rara aunque llegue', () => {
  for (const panel of [null, 'javascript:alert(1)', 'http://evil.example/']) {
    const doc = docRespaldo();
    doc.els['abrir-panel'].attrs.href = 'http://10.0.0.9/';
    doc.els['respaldo-panel'].hidden = false;
    mostrarRespaldo(doc, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel });
    assert.equal(doc.els['respaldo-panel'].hidden, true);
    assert.equal(doc.els['abrir-panel'].attrs.href, undefined);
    assert.equal(doc.els['respaldo-ip'].textContent, '');
  }
});

test('mostrarRespaldo usa textContent: el texto raro no se interpreta', () => {
  const doc = docRespaldo();
  mostrarRespaldo(doc, { red: '<img src=x onerror=alert(1)>', clave: '"&<>', panel: null });
  assert.equal(doc.els['respaldo-red'].textContent, '<img src=x onerror=alert(1)>');
  assert.equal(doc.els['respaldo-clave'].textContent, '"&<>');
});

test('mostrarFallo da el mensaje de qué hacer y oculta datos viejos', () => {
  const doc = docRespaldo();
  doc.els['respaldo-datos'].hidden = false;
  doc.els['respaldo-panel'].hidden = false;
  mostrarFallo(doc);
  assert.match(doc.els['respaldo-estado'].textContent, /desconecta y vuelve a conectar la placa/i);
  assert.match(doc.els["respaldo-estado"].textContent, /par de minutos en prepararse/);
  assert.match(doc.els['respaldo-estado'].textContent, /También la ves en tu panel/);
  assert.equal(doc.els['respaldo-datos'].hidden, true);
  assert.equal(doc.els['respaldo-panel'].hidden, true);
});

test('conectarRespaldo: al pulsar pide el puerto y muestra el resultado', async () => {
  const doc = docRespaldo();
  const puerto = {};
  const nav = { serial: { requestPort: async () => puerto } };
  const pedir = async (p) => {
    assert.equal(p, puerto);
    return { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: null };
  };
  conectarRespaldo(doc, nav, pedir);
  await doc.els['ver-respaldo'].al();
  assert.equal(doc.els['respaldo-red'].textContent, 'SiBlock-0C3F');
  assert.equal(doc.els['ver-respaldo'].disabled, false);
});

test('conectarRespaldo: si la persona cierra la lista de puertos no muestra error', async () => {
  const doc = docRespaldo();
  const nav = { serial: { requestPort: async () => { const e = new Error('x'); e.name = 'NotFoundError'; throw e; } } };
  conectarRespaldo(doc, nav, async () => { throw new Error('no debe llamarse'); });
  await doc.els['ver-respaldo'].al();
  assert.equal(doc.els['respaldo-estado'].textContent, '');
});

test('conectarRespaldo: si la placa no contesta muestra qué hacer', async () => {
  const doc = docRespaldo();
  const nav = { serial: { requestPort: async () => ({}) } };
  conectarRespaldo(doc, nav, async () => { throw new Error('la placa no contestó'); });
  await doc.els['ver-respaldo'].al();
  assert.match(doc.els['respaldo-estado'].textContent, /desconecta y vuelve a conectar/i);
  assert.equal(doc.els['ver-respaldo'].disabled, false);
});

test('mostrarRespaldo sin dirección del panel dice qué hacer (no deja a nadie atorado)', async () => {
  const { TEXTO_SIN_PANEL } = await import('../js/instalar.js');
  for (const panel of [null, undefined, 'javascript:alert(1)']) {
    const doc = docRespaldo();
    mostrarRespaldo(doc, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel });
    assert.equal(doc.els['respaldo-datos'].hidden, false);
    assert.equal(doc.els['respaldo-panel'].hidden, true);
    assert.equal(doc.els['respaldo-estado'].textContent, TEXTO_SIN_PANEL);
    assert.match(doc.els['respaldo-estado'].textContent, /Espera un minuto y pulsa el botón otra vez/);
    assert.match(doc.els['respaldo-estado'].textContent, /http:\/\/siblock\.local/);
  }
  const doc = docRespaldo();
  mostrarRespaldo(doc, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: 'http://192.168.1.50/' });
  assert.equal(doc.els['respaldo-estado'].textContent, '');
});

test('conectarRespaldo: si la placa tarda, dice que se está preparando', async () => {
  const { TEXTO_PREPARANDO } = await import('../js/instalar.js');
  const doc = docRespaldo();
  const nav = { serial: { requestPort: async () => ({}) } };
  let visto = '';
  conectarRespaldo(doc, nav, async (p, opc) => {
    opc.alEsperar();
    visto = doc.els['respaldo-estado'].textContent;
    return { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: 'http://192.168.1.50/' };
  });
  await doc.els['ver-respaldo'].al();
  assert.equal(visto, TEXTO_PREPARANDO);
  assert.match(visto, /par de minutos/);
  assert.equal(doc.els['respaldo-clave'].textContent, 'abcdefghjk');
});

function serialFalso(respuestas) {
  const llamadas = [];
  return {
    llamadas,
    nav: { serial: { requestPort: async (op) => {
      llamadas.push(op);
      const r = respuestas.shift();
      if (r instanceof Error) throw r;
      return r;
    } } },
  };
}
const cancelado = () => Object.assign(new Error('x'), { name: 'NotFoundError' });

test('soloPlacas: la lista muestra solo chips de placa (CH340, CP210x...)', async () => {
  const { nav, llamadas } = serialFalso(['puerto']);
  assert.equal(soloPlacas(nav), true);
  assert.equal(await nav.serial.requestPort(), 'puerto');
  assert.deepEqual(llamadas[0].filters, FILTROS_PLACA);
  assert.ok(FILTROS_PLACA.some((f) => f.usbVendorId === 0x1a86), 'CH340');
  assert.ok(FILTROS_PLACA.some((f) => f.usbVendorId === 0x10c4), 'CP210x');
});

test('soloPlacas: si cierra la lista sin elegir, la siguiente vez salen todos los puertos', async () => {
  const { nav, llamadas } = serialFalso([cancelado(), 'puerto']);
  soloPlacas(nav);
  await assert.rejects(nav.serial.requestPort(), { name: 'NotFoundError' });
  assert.equal(await nav.serial.requestPort(), 'puerto');
  assert.equal(llamadas[1]?.filters, undefined);
});

test('soloPlacas: otro error no quita el filtro; filtros propios se respetan', async () => {
  const otro = Object.assign(new Error('y'), { name: 'SecurityError' });
  const propios = [{ usbVendorId: 1 }];
  const { nav, llamadas } = serialFalso([otro, 'a', 'b']);
  soloPlacas(nav);
  await assert.rejects(nav.serial.requestPort(), { name: 'SecurityError' });
  await nav.serial.requestPort();
  assert.deepEqual(llamadas[1].filters, FILTROS_PLACA);
  await nav.serial.requestPort({ filters: propios });
  assert.equal(llamadas[2].filters, propios);
});

test('soloPlacas: sin WebSerial no hace nada', () => {
  assert.equal(soloPlacas({}), false);
  assert.equal(soloPlacas(undefined), false);
});

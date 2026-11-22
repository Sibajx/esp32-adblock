import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  paquete, comando, Lector, leerTextos, urlPanelSegura, pedirRespaldo,
} from '../js/improv-lite.js';

const hex = (s) => Uint8Array.from(s.trim().split(/\s+/).map((h) => parseInt(h, 16)));
const ascii = (s) => new TextEncoder().encode(s);
const unir = (...partes) => {
  const total = partes.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let i = 0;
  for (const p of partes) { out.set(p, i); i += p.length; }
  return out;
};

// Bytes exactos de la tarea 2 del plan.
const WIFI_CASA = hex('49 4d 50 52 4f 56 01 03 10 01 0e 04 43 61 73 61 08 63 6c 61 76 65 31 32 33 25');
const RESULTADO_URL = hex('49 4d 50 52 4f 56 01 04 17 01 15 14 68 74 74 70 3a 2f 2f 31 39 32 2e 31 36 38 2e 31 2e 35 30 2f 05 0a');
const ESTADO_LISTO = hex('49 4d 50 52 4f 56 01 01 01 02 e2 0a');

test('comando 0xA0 sin datos: bytes exactos', () => {
  assert.deepEqual(comando(0xA0, []), hex('49 4d 50 52 4f 56 01 03 02 a0 00 83 0a'));
});

test('comando WiFi Casa/clave123: paquete de la tarea 2 + salto de línea', () => {
  assert.deepEqual(comando(1, ['Casa', 'clave123']), unir(WIFI_CASA, hex('0a')));
});

test('paquete de estado listo: bytes exactos', () => {
  assert.deepEqual(paquete(1, Uint8Array.of(2)), ESTADO_LISTO);
});

test('paquete rechaza datos de más de 255 bytes', () => {
  assert.throws(() => paquete(3, new Uint8Array(256)));
});

test('Lector ignora texto y lee el resultado con la URL', () => {
  const l = new Lector();
  const r = l.agregar(unir(ascii('STATS,1,2,3,4\n'), RESULTADO_URL));
  assert.equal(r.length, 1);
  assert.equal(r[0].tipo, 4);
  assert.deepEqual(leerTextos(r[0].datos), { comando: 1, textos: ['http://192.168.1.50/'] });
});

test('Lector: checksum malo no da nada', () => {
  const malo = RESULTADO_URL.slice();
  malo[malo.length - 2] ^= 0x01;
  assert.deepEqual(new Lector().agregar(malo), []);
});

test('Lector: tras un checksum malo lee el siguiente paquete bueno', () => {
  const malo = RESULTADO_URL.slice();
  malo[malo.length - 2] ^= 0x01;
  const r = new Lector().agregar(unir(malo, ascii('[wifi] conectado\n'), ESTADO_LISTO));
  assert.equal(r.length, 1);
  assert.equal(r[0].tipo, 1);
  assert.deepEqual([...r[0].datos], [2]);
});

test('Lector: paquete partido en trozos de un byte', () => {
  const l = new Lector();
  const todos = [];
  for (const b of unir(ascii('STATS,10,2\n'), RESULTADO_URL, ESTADO_LISTO)) {
    todos.push(...l.agregar(Uint8Array.of(b)));
  }
  assert.equal(todos.length, 2);
  assert.equal(todos[0].tipo, 4);
  assert.equal(todos[1].tipo, 1);
});

test('Lector: paquete cortado a la mitad seguido de uno completo', () => {
  const r = new Lector().agregar(unir(RESULTADO_URL.slice(0, 15), ESTADO_LISTO));
  assert.equal(r.length, 1);
  assert.equal(r[0].tipo, 1);
});

test('resultado 0xA0 armado con paquete se lee de vuelta', () => {
  const textos = ['SiBlock-0C3F', 'abcdefghjk'];
  const datos = comando(0xA0, textos).slice(9, -2); // id, largo y textos, igual que un resultado
  const r = new Lector().agregar(paquete(4, datos));
  assert.equal(r.length, 1);
  assert.deepEqual(leerTextos(r[0].datos), { comando: 0xA0, textos });
});

test('textos UTF-8 viajan byte por byte', () => {
  const textos = ['Casa de Mamá ñ', ''];
  const datos = comando(1, textos).slice(9, -2);
  assert.deepEqual(leerTextos(datos), { comando: 1, textos });
});

test('leerTextos rechaza largos que no cuadran', () => {
  assert.equal(leerTextos(hex('01 05 04 43 61')), null);       // largo total mayor que lo que hay
  assert.equal(leerTextos(hex('01 03 09 43 61')), null);       // texto que se sale
  assert.equal(leerTextos(new Uint8Array(0)), null);
});

test('urlPanelSegura acepta solo http:// + IPv4 + /', () => {
  assert.equal(urlPanelSegura('http://192.168.1.50/'), 'http://192.168.1.50/');
  assert.equal(urlPanelSegura('http://10.0.0.2/'), 'http://10.0.0.2/');
  for (const malo of [
    'javascript:alert(1)', 'http://evil.example/', 'https://192.168.1.50/',
    'http://192.168.1.50', 'http://192.168.1.50/x', 'http://256.1.1.1/',
    'http://192.168.1/', 'http://192.168.1.50:80/', 'http://user@192.168.1.50/',
    'http://01.2.3.4/', ' http://192.168.1.50/', 'http://192.168.1.50/\n', '', null, 42,
  ]) {
    assert.equal(urlPanelSegura(malo), null, String(malo));
  }
});

// Puerto falso con la forma de Web Serial.
function puertoFalso(respuestas, { abiertoFalla = false } = {}) {
  const enviado = [];
  let abierto = false;
  let cola = [];
  let esperando = null;
  const port = {
    enviado,
    get abierto() { return abierto; },
    async open(opc) {
      if (abiertoFalla) throw new Error('ocupado');
      assert.equal(opc.baudRate, 115200);
      abierto = true;
    },
    async close() { abierto = false; },
    writable: {
      getWriter() {
        return {
          async write(b) {
            enviado.push(b);
            const r = respuestas(b);
            if (r) {
              cola.push(r);
              if (esperando) { const e = esperando; esperando = null; e(); }
            }
          },
          releaseLock() {},
        };
      },
    },
    readable: {
      getReader() {
        let cancelado = false;
        return {
          async read() {
            while (!cola.length && !cancelado) await new Promise((res) => { esperando = res; });
            if (cancelado) return { done: true };
            return { value: cola.shift(), done: false };
          },
          async cancel() { cancelado = true; if (esperando) { const e = esperando; esperando = null; e(); } },
          releaseLock() {},
        };
      },
    },
  };
  return port;
}

const rapido = { espera: 5, total: 300 };

test('pedirRespaldo: red, clave y URL del panel', async () => {
  const datosA0 = comando(0xA0, ['SiBlock-0C3F', 'abcdefghjk']).slice(9, -2);
  const port = puertoFalso((b) => {
    if (b[9] === 0xA0) return unir(ascii('STATS,1,2\n'), paquete(4, datosA0));
    if (b[9] === 0x02) return unir(paquete(1, Uint8Array.of(4)), RESULTADO_URL);
    return null;
  });
  const r = await pedirRespaldo(port, rapido);
  assert.deepEqual(r, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: 'http://192.168.1.50/' });
  assert.deepEqual(port.enviado[0], comando(0xA0, []));
  assert.deepEqual(port.enviado[1], comando(0x02, []));
  assert.equal(port.abierto, false);
});

test('pedirRespaldo: sin WiFi conectado, panel es null', async () => {
  const datosA0 = comando(0xA0, ['SiBlock-0C3F', 'abcdefghjk']).slice(9, -2);
  const port = puertoFalso((b) => {
    if (b[9] === 0xA0) return paquete(4, datosA0);
    if (b[9] === 0x02) return paquete(1, Uint8Array.of(2));
    return null;
  });
  const r = await pedirRespaldo(port, rapido);
  assert.deepEqual(r, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: null });
});

test('pedirRespaldo: URL rara no se acepta', async () => {
  const datosA0 = comando(0xA0, ['SiBlock-0C3F', 'abcdefghjk']).slice(9, -2);
  const datosUrl = comando(0x02, ['javascript:alert(1)']).slice(9, -2);
  const port = puertoFalso((b) => {
    if (b[9] === 0xA0) return paquete(4, datosA0);
    if (b[9] === 0x02) return unir(paquete(1, Uint8Array.of(4)), paquete(4, datosUrl));
    return null;
  });
  const r = await pedirRespaldo(port, rapido);
  assert.equal(r.panel, null);
});

test('pedirRespaldo: sin respuesta falla y cierra el puerto', async () => {
  const port = puertoFalso(() => null);
  await assert.rejects(pedirRespaldo(port, { espera: 5, total: 50 }));
  assert.equal(port.abierto, false);
});

test('pedirRespaldo: si no abre el puerto, falla', async () => {
  const port = puertoFalso(() => null, { abiertoFalla: true });
  await assert.rejects(pedirRespaldo(port, rapido));
});

test('pedirRespaldo: si la placa está ocupada, avisa, vuelve a preguntar y lee la respuesta', async () => {
  const datosA0 = comando(0xA0, ['SiBlock-0C3F', 'abcdefghjk']).slice(9, -2);
  let preguntas = 0;
  const port = puertoFalso((b) => {
    if (b[9] === 0xA0) preguntas++;
    if (preguntas < 2) return null; // la primera vez no contesta (bajando la lista)
    if (b[9] === 0xA0) return paquete(4, datosA0);
    if (b[9] === 0x02) return unir(paquete(1, Uint8Array.of(4)), RESULTADO_URL);
    return null;
  });
  let avisos = 0;
  const r = await pedirRespaldo(port, {
    espera: 5, total: 2000, repetir: 60, avisarTras: 20, alEsperar: () => { avisos++; },
  });
  assert.deepEqual(r, { red: 'SiBlock-0C3F', clave: 'abcdefghjk', panel: 'http://192.168.1.50/' });
  assert.equal(avisos, 1);
  assert.ok(preguntas >= 2);
  assert.equal(port.abierto, false);
});

test('pedirRespaldo: si contesta enseguida no avisa', async () => {
  const datosA0 = comando(0xA0, ['SiBlock-0C3F', 'abcdefghjk']).slice(9, -2);
  const port = puertoFalso((b) => {
    if (b[9] === 0xA0) return paquete(4, datosA0);
    if (b[9] === 0x02) return paquete(1, Uint8Array.of(2));
    return null;
  });
  let avisos = 0;
  await pedirRespaldo(port, { espera: 5, total: 300, avisarTras: 100, alEsperar: () => { avisos++; } });
  await new Promise((res) => setTimeout(res, 150));
  assert.equal(avisos, 0);
});

// Improv Serial mínimo para leer la contraseña de respaldo y la dirección del panel.
// Protocolo: https://www.improv-wifi.com/serial/
// Paquete = "IMPROV" + versión 1 + tipo + largo + datos + checksum (suma mod 256) [+ '\n' al enviar].

const CABECERA = [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56]; // "IMPROV"
const VERSION = 0x01;
const TIPO_ESTADO = 0x01;
const TIPO_RPC = 0x03;
const TIPO_RESULTADO = 0x04;
const ESTADO_CONECTADO = 0x04;
const CMD_WIFI = 0x01;
const CMD_ESTADO = 0x02;
const CMD_RESPALDO = 0xA0;
const TOPE_BUFFER = 4096;

/** Arma un paquete con checksum y '\n' al final. */
export function paquete(tipo, datos) {
  const d = datos ?? new Uint8Array(0);
  if (d.length > 255) throw new RangeError('datos de más de 255 bytes');
  const out = new Uint8Array(CABECERA.length + 3 + d.length + 2);
  out.set(CABECERA, 0);
  out[6] = VERSION;
  out[7] = tipo & 0xff;
  out[8] = d.length;
  out.set(d, 9);
  let suma = 0;
  for (let i = 0; i < 9 + d.length; i++) suma = (suma + out[i]) & 0xff;
  out[9 + d.length] = suma;
  out[10 + d.length] = 0x0a;
  return out;
}

/** Arma un comando RPC (tipo 0x03) con textos UTF-8 con prefijo de largo. */
export function comando(id, textos = []) {
  const enc = new TextEncoder();
  const partes = textos.map((t) => enc.encode(String(t)));
  const total = partes.reduce((n, p) => n + 1 + p.length, 0);
  if (total > 253) throw new RangeError('textos demasiado largos');
  const datos = new Uint8Array(2 + total);
  datos[0] = id & 0xff;
  datos[1] = total;
  let i = 2;
  for (const p of partes) {
    if (p.length > 255) throw new RangeError('texto demasiado largo');
    datos[i++] = p.length;
    datos.set(p, i);
    i += p.length;
  }
  return paquete(TIPO_RPC, datos);
}

/** Lee paquetes de un flujo de bytes; ignora texto (como "STATS,...") y checksums malos. */
export class Lector {
  constructor() {
    this.buf = new Uint8Array(0);
  }

  agregar(bytes) {
    const nuevo = new Uint8Array(this.buf.length + bytes.length);
    nuevo.set(this.buf, 0);
    nuevo.set(bytes, this.buf.length);
    this.buf = nuevo;
    const salida = [];

    for (;;) {
      const ini = buscarCabecera(this.buf);
      if (ini < 0) {
        // Conserva una posible cabecera a medias al final.
        this.buf = this.buf.slice(Math.max(0, this.buf.length - (CABECERA.length - 1)));
        break;
      }
      if (ini > 0) this.buf = this.buf.slice(ini);
      const r = probar(this.buf, 0);
      if (r === INCOMPLETO) {
        // Un paquete cortado no debe tapar a uno completo que venga detrás.
        const otro = buscarCabecera(this.buf, 1);
        if (otro > 0 && probar(this.buf, otro) >= 0) {
          this.buf = this.buf.slice(otro);
          continue;
        }
        break;
      }
      if (r === MALO) { this.buf = this.buf.slice(1); continue; }
      salida.push({ tipo: this.buf[7], datos: this.buf.slice(9, r) });
      this.buf = this.buf.slice(r + 1);
    }

    if (this.buf.length > TOPE_BUFFER) this.buf = this.buf.slice(this.buf.length - TOPE_BUFFER);
    return salida;
  }
}

const INCOMPLETO = -1;
const MALO = -2;

/** Prueba un paquete que empieza en `ini`: posición del checksum, INCOMPLETO o MALO. */
function probar(buf, ini) {
  if (buf.length - ini < 9) return INCOMPLETO;
  if (buf[ini + 6] !== VERSION) return MALO;
  const fin = ini + 9 + buf[ini + 8]; // posición del checksum
  if (buf.length < fin + 1) return INCOMPLETO;
  let suma = 0;
  for (let i = ini; i < fin; i++) suma = (suma + buf[i]) & 0xff;
  return suma === buf[fin] ? fin - ini : MALO;
}

function buscarCabecera(buf, desde = 0) {
  fuera: for (let i = desde; i <= buf.length - CABECERA.length; i++) {
    for (let j = 0; j < CABECERA.length; j++) {
      if (buf[i + j] !== CABECERA[j]) continue fuera;
    }
    return i;
  }
  return -1;
}

/** Datos de un resultado RPC → {comando, textos}; null si los largos no cuadran. */
export function leerTextos(datos) {
  if (!datos || datos.length < 2) return null;
  const total = datos[1];
  if (2 + total > datos.length) return null;
  const dec = new TextDecoder('utf-8');
  const textos = [];
  let i = 2;
  const fin = 2 + total;
  while (i < fin) {
    const n = datos[i++];
    if (i + n > fin) return null;
    textos.push(dec.decode(datos.subarray(i, i + n)));
    i += n;
  }
  return { comando: datos[0], textos };
}

/** Solo acepta "http://" + IPv4 + "/". Cualquier otra cosa → null. */
export function urlPanelSegura(texto) {
  if (typeof texto !== 'string') return null;
  const m = /^http:\/\/(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})\/$/.exec(texto);
  if (!m) return null;
  for (const parte of m.slice(1)) {
    if (parte.length > 1 && parte[0] === '0') return null;
    if (Number(parte) > 255) return null;
  }
  return texto;
}

function dormir(ms) {
  return new Promise((res) => setTimeout(res, ms));
}

/**
 * Abre el puerto, espera a la placa, pide la contraseña de respaldo (0xA0) y el estado (0x02),
 * junta respuestas hasta `total` ms y cierra el puerto.
 *
 * Recién instalada, la placa pasa un par de minutos bajando su primera lista y no contesta
 * por el cable. Por eso la espera es larga, se vuelve a preguntar cada `repetir` ms y, si a
 * los `avisarTras` ms no contestó, se llama a `alEsperar()` para decirle a la persona que espere.
 * Devuelve {red, clave, panel|null}; lanza un error si no llegó la contraseña.
 */
export async function pedirRespaldo(port, {
  espera = 2000, total = 180000, repetir = 20000, avisarTras = 8000, alEsperar = null,
} = {}) {
  await port.open({ baudRate: 115200 });
  const lector = new Lector();
  let red = null;
  let clave = null;
  let panel = null;
  let estado = null;
  let reader = null;
  let temporizador = null;
  let otraVez = null;
  let aviso = null;

  const preguntar = async () => {
    const w = port.writable.getWriter();
    try {
      await w.write(comando(CMD_RESPALDO, []));
      await w.write(comando(CMD_ESTADO, []));
    } finally {
      w.releaseLock();
    }
  };

  try {
    await dormir(espera);
    await preguntar();
    if (alEsperar) aviso = setTimeout(() => { if (red === null) alEsperar(); }, avisarTras);
    otraVez = setInterval(() => { if (red === null) preguntar().catch(() => {}); }, repetir);

    reader = port.readable.getReader();
    const vence = new Promise((res) => { temporizador = setTimeout(() => res({ vencio: true }), total); });

    for (;;) {
      const r = await Promise.race([reader.read(), vence]);
      if (r.vencio || r.done) break;
      if (!r.value) continue;
      for (const p of lector.agregar(r.value)) {
        if (p.tipo === TIPO_ESTADO && p.datos.length >= 1) {
          estado = p.datos[0];
        } else if (p.tipo === TIPO_RESULTADO) {
          const res = leerTextos(p.datos);
          if (!res) continue;
          if (res.comando === CMD_RESPALDO && res.textos.length >= 2) {
            red = res.textos[0];
            clave = res.textos[1];
          } else if ((res.comando === CMD_ESTADO || res.comando === CMD_WIFI) && res.textos.length >= 1) {
            panel = urlPanelSegura(res.textos[0]) ?? panel;
          }
        }
      }
      const listo = red !== null && estado !== null && (estado !== ESTADO_CONECTADO || panel !== null);
      if (listo) break;
    }
  } finally {
    if (temporizador) clearTimeout(temporizador);
    if (otraVez) clearInterval(otraVez);
    if (aviso) clearTimeout(aviso);
    if (reader) {
      try { await reader.cancel(); } catch { /* nada */ }
      try { reader.releaseLock(); } catch { /* nada */ }
    }
    try { await port.close(); } catch { /* nada */ }
  }

  if (red === null || clave === null) throw new Error('la placa no contestó');
  return { red, clave, panel };
}

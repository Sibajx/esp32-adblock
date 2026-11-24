// Página de instalación de SiBlock.
// Sin cookies ni nada guardado en el navegador. El embudo solo suma contadores por paso (sin número).
import { pedirRespaldo, urlPanelSegura } from './improv-lite.js';
import { contarPaso } from './nube.js';
import { TEXTOS, texto } from './textos.js';

// Los textos salen de textos.js según el idioma de la página. Estos dos se exportan en español
// para las pruebas de siempre.
export const TEXTO_PREPARANDO = TEXTOS.es.preparando;
export const TEXTO_SIN_PANEL = TEXTOS.es.sinPanel;

/** true si el navegador puede hablar con la placa por USB (WebSerial). */
export function puedeInstalar(nav) {
  return !!nav && typeof nav === 'object' && 'serial' in nav;
}

// Chips USB-serie de las placas ESP32 (CP210x, CH340/CH9102, FTDI, Prolific y el USB propio de
// Espressif). En Linux la lista de puertos trae además decenas de ttyS* internos que no son la placa.
export const FILTROS_PLACA = [0x10c4, 0x1a86, 0x0403, 0x067b, 0x303a].map((usbVendorId) => ({ usbVendorId }));

/**
 * Hace que la lista de puertos muestre solo placas. El instalador (esp-web-tools, sin opción para
 * esto) llama a requestPort() sin filtros: se le ponen los de arriba. Si la persona cierra la lista
 * sin elegir (quizá su placa usa otro chip), la siguiente vez se muestran todos los puertos.
 */
export function soloPlacas(nav) {
  if (!puedeInstalar(nav) || typeof nav.serial.requestPort !== 'function') return false;
  const original = nav.serial.requestPort.bind(nav.serial);
  let todos = false;
  nav.serial.requestPort = async (opciones) => {
    if (opciones?.filters || todos) return original(opciones);
    try {
      return await original({ ...opciones, filters: FILTROS_PLACA });
    } catch (e) {
      if (e && e.name === 'NotFoundError') todos = true;
      throw e;
    }
  };
  return true;
}

function el(doc, id) {
  return doc.getElementById(id);
}

/** Muestra el botón de instalar o el aviso de navegador sin soporte. */
export function prepararPagina(doc, nav) {
  const ok = puedeInstalar(nav);
  const instalar = el(doc, 'instalar');
  const sinSoporte = el(doc, 'sin-soporte');
  const respaldo = el(doc, 'respaldo');
  if (instalar) instalar.hidden = !ok;
  if (sinSoporte) sinSoporte.hidden = ok;
  if (respaldo) respaldo.hidden = !ok;
  return ok;
}

function ocultarResultado(doc) {
  const datos = el(doc, 'respaldo-datos');
  const panel = el(doc, 'respaldo-panel');
  const enlace = el(doc, 'abrir-panel');
  const ip = el(doc, 'respaldo-ip');
  if (datos) datos.hidden = true;
  if (panel) panel.hidden = true;
  if (enlace) enlace.removeAttribute('href');
  if (ip) ip.textContent = '';
}

function ponerEstado(doc, texto) {
  const estado = el(doc, 'respaldo-estado');
  if (estado) estado.textContent = texto;
}

/** Muestra red y contraseña; enlaza el panel solo si la dirección es segura. */
export function mostrarRespaldo(doc, { red, clave, panel }) {
  ocultarResultado(doc);
  const r = el(doc, 'respaldo-red');
  const c = el(doc, 'respaldo-clave');
  if (r) r.textContent = String(red ?? '');
  if (c) c.textContent = String(clave ?? '');
  const datos = el(doc, 'respaldo-datos');
  if (datos) datos.hidden = false;
  ponerEstado(doc, '');

  const url = urlPanelSegura(panel);
  const caja = el(doc, 'respaldo-panel');
  const enlace = el(doc, 'abrir-panel');
  const ip = el(doc, 'respaldo-ip');
  if (url && caja && enlace) {
    enlace.setAttribute('href', url);
    if (ip) ip.textContent = url.slice('http://'.length, -1);
    caja.hidden = false;
  } else {
    // Sin dirección todavía (la placa se está conectando): que nadie se quede sin saber qué hacer.
    ponerEstado(doc, texto('sinPanel', doc));
  }
}

/** Mensaje de qué hacer si no se pudo leer la placa. */
export function mostrarFallo(doc, mensaje = texto('fallo', doc)) {
  ocultarResultado(doc);
  ponerEstado(doc, mensaje);
}

/** Conecta el botón "Ver mi contraseña de respaldo". `pedir` se puede cambiar en pruebas. */
export function conectarRespaldo(doc, nav, pedir = pedirRespaldo) {
  const boton = el(doc, 'ver-respaldo');
  if (!boton || !puedeInstalar(nav)) return;
  let ocupado = false;

  const alPulsar = async () => {
    if (ocupado) return;
    let puerto;
    try {
      puerto = await nav.serial.requestPort();
    } catch (e) {
      // La persona cerró la lista sin elegir: no es un error.
      if (e && e.name === 'NotFoundError') return;
      mostrarFallo(doc);
      return;
    }
    ocupado = true;
    boton.disabled = true;
    ocultarResultado(doc);
    ponerEstado(doc, texto('leyendo', doc));
    try {
      mostrarRespaldo(doc, await pedir(puerto, { alEsperar: () => ponerEstado(doc, texto('preparando', doc)) }));
    } catch (e) {
      const ocupada = e && (e.name === 'InvalidStateError' || e.name === 'NetworkError');
      mostrarFallo(doc, ocupada ? texto('ocupada', doc) : texto('fallo', doc));
    } finally {
      ocupado = false;
      boton.disabled = false;
    }
  };

  boton.addEventListener('click', alPulsar);
  return alPulsar;
}

// --- Embudo: entro, conecto, instalo, wifi, panel ---
// El instalador (esp-web-tools 10.4.0, fijado con SRI en index.html) no avisa con eventos públicos
// cuándo termina; se mira su estado una vez por segundo mientras está abierto.
// Falta verificar con la placa que los nombres internos no cambien al subir de versión.
const IMPROV_CON_WIFI = 4; // ImprovSerialCurrentState.PROVISIONED

/** Sigue un <ewt-install-dialog>: conecto al aparecer, instalo al terminar, wifi al recibir el WiFi. */
export function seguirDialogo(dialogo, una, cada = setInterval, parar = clearInterval) {
  una('conecto');
  let previo = dialogo?._client?.state;
  let instalo = false;
  let wifi = false;
  const revisar = () => {
    if (!instalo && dialogo?._installState?.state === 'finished') { instalo = true; una('instalo'); }
    const st = dialogo?._client?.state;
    if (!wifi && st === IMPROV_CON_WIFI && previo !== undefined && previo !== IMPROV_CON_WIFI) { wifi = true; una('wifi'); }
    if (st !== undefined) previo = st;
  };
  const reloj = cada(revisar, 1000);
  dialogo.addEventListener('closed', () => { revisar(); parar(reloj); }, { once: true });
}

/** Cuenta cada paso una sola vez por visita. Devuelve la función que cuenta. */
export function iniciarEmbudo(doc, contar = contarPaso, Observador = globalThis.MutationObserver,
  cada = setInterval, parar = clearInterval) {
  const vistos = new Set();
  const una = (paso) => {
    if (vistos.has(paso)) return;
    vistos.add(paso);
    contar(paso);
  };
  una('entro');
  const enlace = doc.getElementById('abrir-panel');
  if (enlace) enlace.addEventListener('click', () => una('panel'));
  if (typeof Observador === 'function' && doc.body) {
    new Observador((cambios) => {
      for (const c of cambios) {
        for (const n of c.addedNodes ?? []) {
          if (n && n.localName === 'ewt-install-dialog') seguirDialogo(n, una, cada, parar);
        }
      }
    }).observe(doc.body, { childList: true });
  }
  return una;
}

if (typeof document !== 'undefined' && typeof navigator !== 'undefined') {
  prepararPagina(document, navigator);
  soloPlacas(navigator);
  conectarRespaldo(document, navigator);
  iniciarEmbudo(document);
}

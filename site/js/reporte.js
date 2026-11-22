// Reportes desde la página: "qué módem tengo" y "esto no debió bloquearse".
// Solo opciones fijas. El número de instalación llega del panel en el fragmento (#i=…), que nunca
// viaja a un servidor por sí solo; si no hay, se inventa uno al azar para esta visita y no se guarda.
// Todo se pinta con textContent.
import { MARCAS } from './guias-datos.js';
import { CONFIG } from './config.js';
import { enviar, mensajeDeError, pedirPais, montarTurnstile } from './nube.js';

const congelar = (xs) => Object.freeze(xs.map((x) => Object.freeze({ ...x })));

/** Fabricantes del contrato: los mismos id y nombres que MARCAS. */
export const FABRICANTES = congelar(MARCAS);
export const MENUS = congelar([
  { id: 'internet_wan', nombre: 'Internet o WAN' },
  { id: 'red_local', nombre: 'Red local, LAN o DHCP' },
  { id: 'avanzado', nombre: 'Avanzado' },
  { id: 'otro', nombre: 'Otro o no lo encontré' },
]);
export const MOTIVOS = congelar([
  { id: 'pagina_no_abre', nombre: 'Una página no abre' },
  { id: 'app_no_funciona', nombre: 'Una app no funciona' },
  { id: 'juego_no_funciona', nombre: 'Un juego no funciona' },
  { id: 'otro', nombre: 'Otro' },
]);

const esDe = (lista, v) => typeof v === 'string' && lista.some((x) => x.id === v);
const ID_RE = /^[0-9a-f]{32}$/;
const ETIQUETA_RE = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/;

function tokenOk(t) {
  return typeof t === 'string' && t.length > 0 && t.length <= 2048;
}

/** "#i=<32 hex>" → id; cualquier otra cosa → null. */
export function idDesdeFragmento(hash) {
  if (typeof hash !== 'string' || !hash.startsWith('#i=')) return null;
  const id = hash.slice(3);
  return ID_RE.test(id) ? id : null;
}

/** 32 hex al azar (16 bytes de getRandomValues). */
export function idNuevo(cryptoObj = globalThis.crypto) {
  const b = cryptoObj.getRandomValues(new Uint8Array(16));
  return Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
}

/** Misma regla que la placa: 3..253, ≥ 2 etiquetas de 1..63 [a-z0-9-], sin '-' al inicio o al final. */
export function dominioValido(d) {
  if (typeof d !== 'string' || d.length < 3 || d.length > 253) return false;
  const partes = d.split('.');
  return partes.length >= 2 && partes.every((p) => ETIQUETA_RE.test(p));
}

/** Cuerpo de POST /api/v1/reporte/modem, o null si algo no está en las listas. */
export function cuerpoModem(id, campos) {
  if (!ID_RE.test(String(id)) || !campos || typeof campos !== 'object') return null;
  const { fabricante, dnsOk, menu, turnstile } = campos;
  if (!esDe(FABRICANTES, fabricante) || !esDe(MENUS, menu) || !tokenOk(turnstile)) return null;
  const dns = dnsOk === 'si' || dnsOk === true ? true : dnsOk === 'no' || dnsOk === false ? false : null;
  if (dns === null) return null;
  return { i: id, origen: 'pagina', fabricante, dns_ok: dns, menu, turnstile };
}

/** Cuerpo de POST /api/v1/reporte/bloqueo (dominio con trim y en minúsculas), o null. */
export function cuerpoBloqueo(id, campos) {
  if (!ID_RE.test(String(id)) || !campos || typeof campos !== 'object') return null;
  const { motivo, turnstile } = campos;
  const dominio = typeof campos.dominio === 'string' ? campos.dominio.trim().toLowerCase() : '';
  if (!dominioValido(dominio) || !esDe(MOTIVOS, motivo) || !tokenOk(turnstile)) return null;
  return { i: id, origen: 'pagina', dominio, motivo, turnstile };
}

/** «Tu internet parece ser de: …» solo si el servidor lo supo. */
export function pintarProveedor(doc, info) {
  const caja = doc.getElementById('proveedor-visto');
  const nombre = doc.getElementById('proveedor-nombre');
  if (!caja || !nombre) return;
  const prov = info && typeof info.proveedor === 'string' ? info.proveedor : '';
  nombre.textContent = prov;
  caja.hidden = prov === '';
}

function llenar(doc, select, lista, primera) {
  const op = (valor, texto) => {
    const o = doc.createElement('option');
    o.value = valor;
    o.textContent = texto;
    return o;
  };
  select.replaceChildren(op('', primera), ...lista.map((x) => op(x.id, x.nombre)));
}

/** Conecta un formulario: arma el cuerpo, lo manda y dice qué pasó. */
function conectar(doc, { form, estado, boton, turnstile, armar, ruta, exito }) {
  let ocupado = false;
  form.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    if (ocupado) return;
    const token = turnstile.token();
    if (!token) {
      estado.textContent = 'Marca la casilla de abajo para mostrar que eres una persona.';
      return;
    }
    const cuerpo = armar(new FormData(form), token);
    if (!cuerpo) {
      estado.textContent = 'Revisa lo que elegiste: falta algo o no está bien escrito.';
      return;
    }
    ocupado = true;
    boton.disabled = true;
    estado.textContent = 'Mandando…';
    const r = await enviar(ruta, cuerpo);
    estado.textContent = r.ok ? exito : mensajeDeError(r.error);
    if (r.ok) form.reset();
    turnstile.reiniciar();
    ocupado = false;
    boton.disabled = false;
  });
}

/** Prepara la página de reportes. */
export function iniciar(doc, win, config = CONFIG) {
  const formModem = doc.getElementById('form-modem');
  const formBloqueo = doc.getElementById('form-bloqueo');
  if (!formModem || !formBloqueo) return;

  const delPanel = idDesdeFragmento(win.location.hash);
  const id = delPanel ?? idNuevo();
  if (win.location.hash) {
    // Quita el número de la barra de direcciones y del historial.
    try { win.history.replaceState(null, '', win.location.pathname + win.location.search); } catch { /* sin historial */ }
  }
  const desdePanel = doc.getElementById('desde-panel');
  if (desdePanel) desdePanel.hidden = !delPanel;

  llenar(doc, doc.getElementById('fabricante'), FABRICANTES, 'Elige la marca');
  llenar(doc, doc.getElementById('menu'), MENUS, 'Elige el menú');
  llenar(doc, doc.getElementById('motivo'), MOTIVOS, 'Elige qué pasó');

  const botones = [doc.getElementById('mandar-modem'), doc.getElementById('mandar-bloqueo')];
  const tsModem = montarTurnstile(doc, doc.getElementById('turnstile-modem'), config);
  const tsBloqueo = montarTurnstile(doc, doc.getElementById('turnstile-bloqueo'), config);
  const aviso = doc.getElementById('aviso-listo');
  if (!tsModem.listo) {
    if (aviso) aviso.hidden = false;
    for (const b of botones) if (b) b.disabled = true;
  }

  conectar(doc, {
    form: formModem,
    estado: doc.getElementById('estado-modem'),
    boton: botones[0],
    turnstile: tsModem,
    ruta: '/reporte/modem',
    exito: 'Gracias. Tu reporte ya cuenta para la guía de tu módem.',
    armar: (fd, t) => cuerpoModem(id, { fabricante: fd.get('fabricante'), dnsOk: fd.get('dnsOk'), menu: fd.get('menu'), turnstile: t }),
  });
  conectar(doc, {
    form: formBloqueo,
    estado: doc.getElementById('estado-bloqueo'),
    boton: botones[1],
    turnstile: tsBloqueo,
    ruta: '/reporte/bloqueo',
    exito: 'Gracias. Vamos a revisar ese sitio.',
    armar: (fd, t) => cuerpoBloqueo(id, { dominio: fd.get('dominio'), motivo: fd.get('motivo'), turnstile: t }),
  });

  pedirPais().then((info) => pintarProveedor(doc, info));
}

if (typeof document !== 'undefined' && typeof window !== 'undefined') {
  iniciar(document, window);
}

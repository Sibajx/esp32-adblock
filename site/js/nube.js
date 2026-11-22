// Ayudas de la página para hablar con el servidor de SiBlock (siblock.s-dev.win/api/v1).
// Sin cookies ni nada guardado en el navegador. Todo lo que llega de afuera se revisa antes de usarse.
import { CONFIG } from './config.js';

const ANALITICA_SRC = 'https://static.cloudflareinsights.com/beacon.min.js';
const TURNSTILE_SRC = 'https://challenges.cloudflare.com/turnstile/v0/api.js';
const ID_ANALITICA = 'siblock-analitica';
const ID_TURNSTILE = 'siblock-turnstile';

/** Pasos del embudo de instalación (contrato). */
export const PASOS = Object.freeze(['entro', 'conecto', 'instalo', 'wifi', 'panel']);

/** Códigos de error del Worker (contrato) más el de la página sin red. */
const MENSAJES = Object.freeze({
  datos: 'Algo del formulario no está bien. Revísalo y prueba otra vez.',
  origen: 'Este formulario solo funciona desde la página de SiBlock.',
  turnstile: 'No pudimos comprobar que eres una persona. Vuelve a marcar la casilla y prueba otra vez.',
  acceso: 'Entra con tu correo de dueño.',
  no_existe: 'Esto todavía no está listo. Prueba más tarde.',
  metodo: 'Esto todavía no está listo. Prueba más tarde.',
  grande: 'El mensaje es muy largo. Hazlo más corto y prueba otra vez.',
  limite: 'Mandaste muchos seguidos. Espera un minuto y prueba otra vez.',
  interno: 'No se pudo guardar. Prueba más tarde.',
  sin_internet: 'No pudimos llegar al servidor. Revisa tu internet y prueba otra vez.',
});
const POR_ESTADO = Object.freeze({ 400: 'datos', 403: 'origen', 404: 'no_existe', 405: 'metodo', 413: 'grande', 429: 'limite' });

/** Token o clave pública: solo letras, números, '-' y '_' (así no hace falta escapar nada). */
export function tokenValido(t) {
  return typeof t === 'string' && /^[A-Za-z0-9_-]{1,128}$/.test(t);
}

/** true si el navegador pide no ser rastreado (Global Privacy Control o Do Not Track). */
function noRastrear(nav) {
  if (!nav || typeof nav !== 'object') return false;
  return nav.globalPrivacyControl === true || nav.doNotTrack === '1';
}

function agregarScript(doc, id, src, preparar) {
  if (doc.getElementById(id)) return doc.getElementById(id);
  const s = doc.createElement('script');
  s.setAttribute('id', id);
  s.id = id;
  s.src = src;
  s.defer = true;
  if (preparar) preparar(s);
  doc.head.appendChild(s);
  return s;
}

/**
 * Cloudflare Web Analytics (sin cookies). Token vacío o raro, o "no rastrear" → no hace nada.
 * Devuelve true si la analítica queda cargada.
 */
export function cargarAnalitica(doc, config = CONFIG, nav) {
  if (!doc || !config || !tokenValido(config.analiticaToken) || noRastrear(nav)) return false;
  agregarScript(doc, ID_ANALITICA, ANALITICA_SRC, (s) => {
    s.setAttribute('data-cf-beacon', JSON.stringify({ token: config.analiticaToken }));
  });
  return true;
}

/**
 * Turnstile de Cloudflare. Clave vacía → false. Si no, carga el script una sola vez → true.
 * `alCargar` se llama cuando el script ya está listo (o enseguida si ya lo estaba).
 */
export function cargarTurnstile(doc, config = CONFIG, alCargar) {
  if (!doc || !config || !tokenValido(config.turnstileClave)) return false;
  const yaEstaba = !!doc.getElementById(ID_TURNSTILE);
  const s = agregarScript(doc, ID_TURNSTILE, TURNSTILE_SRC);
  if (typeof alCargar === 'function') {
    const listo = typeof globalThis.turnstile === 'object' && globalThis.turnstile !== null;
    if (yaEstaba && listo) alCargar();
    else s.addEventListener('load', () => alCargar(), { once: true });
  }
  return true;
}

/** POST JSON a CONFIG.api + ruta → {ok:true} | {ok:false, error:'<código>'}. Nunca lanza. */
export async function enviar(ruta, datos, fetchFn = globalThis.fetch) {
  let res;
  try {
    res = await fetchFn(CONFIG.api + ruta, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(datos),
      credentials: 'omit',
      cache: 'no-store',
      redirect: 'error',
    });
  } catch {
    return { ok: false, error: 'sin_internet' };
  }
  if (res.ok) return { ok: true };
  let codigo = null;
  try {
    const cuerpo = await res.json();
    if (cuerpo && typeof cuerpo.error === 'string' && Object.hasOwn(MENSAJES, cuerpo.error)) codigo = cuerpo.error;
  } catch { /* cuerpo vacío o no JSON: se usa el estado */ }
  return { ok: false, error: codigo ?? POR_ESTADO[res.status] ?? 'interno' };
}

/** Texto simple en español para un código de error. */
export function mensajeDeError(error) {
  return typeof error === 'string' && Object.hasOwn(MENSAJES, error) ? MENSAJES[error] : MENSAJES.interno;
}

/** Cuenta un paso del embudo con sendBeacon: sin número, sin cookie. Devuelve si salió. */
export function contarPaso(paso, nav = globalThis.navigator) {
  if (!PASOS.includes(paso) || !nav || typeof nav.sendBeacon !== 'function' || noRastrear(nav)) return false;
  const cuerpo = JSON.stringify({ p: paso });
  try {
    const datos = typeof Blob === 'function' ? new Blob([cuerpo], { type: 'application/json' }) : cuerpo;
    return nav.sendBeacon(CONFIG.api + '/paso', datos) === true;
  } catch {
    return false;
  }
}

function limpiarTexto(s, max) {
  if (typeof s !== 'string') return '';
  // Sin caracteres de control; se muestra siempre con textContent.
  return s.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, max);
}

/** País (2 letras) y compañía de internet según el servidor; null si no se pudo. No guarda nada. */
export async function pedirPais(fetchFn = globalThis.fetch) {
  try {
    const res = await fetchFn(CONFIG.api + '/pais', { credentials: 'omit', cache: 'no-store', redirect: 'error' });
    if (!res.ok) return null;
    const d = await res.json();
    if (!d || typeof d !== 'object' || Array.isArray(d)) return null;
    const pais = typeof d.pais === 'string' && /^[A-Z]{2}$/.test(d.pais) ? d.pais : '';
    return { pais, proveedor: limpiarTexto(d.proveedor, 100) };
  } catch {
    return null;
  }
}

/**
 * Dibuja Turnstile dentro de `contenedor`. Devuelve {listo, token(), reiniciar()}.
 * Sin clave → listo:false (el formulario debe quedar desactivado). Cada token sirve una sola vez.
 */
export function montarTurnstile(doc, contenedor, config = CONFIG) {
  let token = '';
  let widget = null;
  const borrar = () => { token = ''; };
  const listo = !!contenedor && cargarTurnstile(doc, config, () => {
    const ts = globalThis.turnstile;
    if (!ts || typeof ts.render !== 'function' || widget !== null) return;
    widget = ts.render(contenedor, {
      sitekey: config.turnstileClave,
      language: 'es',
      callback: (t) => { token = typeof t === 'string' ? t : ''; },
      'expired-callback': borrar,
      'error-callback': borrar,
    });
  });
  return {
    listo,
    token: () => token,
    reiniciar() {
      borrar();
      const ts = globalThis.turnstile;
      if (widget !== null && ts && typeof ts.reset === 'function') ts.reset(widget);
    },
  };
}

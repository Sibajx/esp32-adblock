// Panel del dueño (detrás de Cloudflare Access). Solo lee GET /api/v1/dueno/resumen.
// Los dominios reportados vienen de afuera: se pintan siempre como texto.
import { CONFIG } from './config.js';
import { barras } from './graficas.js';
import { PASOS } from './nube.js';
import { FABRICANTES, MOTIVOS } from './reporte.js';
import { TEMAS } from './contacto.js';

const numero = new Intl.NumberFormat('es-MX');
const NOMBRE_PASO = Object.freeze({
  entro: 'Entró a la página',
  conecto: 'Conectó la placa',
  instalo: 'Instaló SiBlock',
  wifi: 'Dio su WiFi',
  panel: 'Abrió su panel',
});
const entero = (n) => (Number.isSafeInteger(n) && n >= 0 ? n : 0);
const lista = (x) => (Array.isArray(x) ? x.filter((f) => f && typeof f === 'object') : []);
const nombreDe = (xs, id) => xs.find((x) => x.id === id)?.nombre ?? '';
const porN = (a, b) => b.n - a.n;

/** Deja solo lo del contrato; el embudo siempre en el orden del contrato. */
export function normalizarResumen(d) {
  if (!d || typeof d !== 'object' || Array.isArray(d)) return null;
  const embudo = lista(d.embudo);
  const deLista = (xs, campo, valida) => lista(xs).filter((f) => valida(f[campo]))
    .map((f) => ({ [campo]: f[campo], n: entero(f.n) })).sort(porN);
  return {
    embudo: PASOS.map((paso) => ({ paso, n: entero(embudo.find((f) => f.paso === paso)?.n) })),
    temas: deLista(d.temas, 'tema', (v) => TEMAS.some((t) => t.id === v)),
    motivos: deLista(d.motivos, 'motivo', (v) => MOTIVOS.some((t) => t.id === v)),
    bloqueos: deLista(d.bloqueos, 'dominio', (v) => typeof v === 'string' && v.length > 0 && v.length <= 253).slice(0, 50),
    guiasFaltan: deLista(d.guiasFaltan, 'fabricante', (v) => FABRICANTES.some((t) => t.id === v)),
    placasTotal: entero(d.placasTotal),
  };
}

function poner(doc, id, el) {
  const caja = doc.getElementById(id);
  if (caja) caja.replaceChildren(el);
}

/** Pinta el resumen ya normalizado. */
export function pintarResumen(doc, r) {
  if (!r) return;
  const placas = doc.getElementById('d-placas');
  if (placas) placas.textContent = numero.format(r.placasTotal);
  poner(doc, 'd-embudo', barras(doc, r.embudo.some((f) => f.n > 0) ? r.embudo : [], {
    etiqueta: (f) => NOMBRE_PASO[f.paso], valor: 'n', titulo: 'Embudo de instalación, últimos 30 días',
  }));
  poner(doc, 'd-temas', barras(doc, r.temas, { etiqueta: (f) => nombreDe(TEMAS, f.tema), valor: 'n', titulo: 'Temas de Contáctanos' }));
  poner(doc, 'd-motivos', barras(doc, r.motivos, { etiqueta: (f) => nombreDe(MOTIVOS, f.motivo), valor: 'n', titulo: 'Por qué no debió bloquearse' }));
  poner(doc, 'd-bloqueos', barras(doc, r.bloqueos, { etiqueta: 'dominio', valor: 'n', titulo: 'Sitios más reportados' }));
  poner(doc, 'd-guias', barras(doc, r.guiasFaltan, {
    etiqueta: (f) => nombreDe(FABRICANTES, f.fabricante), valor: 'n', titulo: 'Guías que faltan',
  }));
}

/** Pide el resumen. 401/403 o redirección a Access → pide entrar. */
export async function cargarResumen(doc, fetchFn = globalThis.fetch) {
  const estado = doc.getElementById('dueno-estado');
  const datos = doc.getElementById('dueno-datos');
  const entrar = doc.getElementById('dueno-entrar');
  const decir = (t) => { if (estado) estado.textContent = t; };
  let res;
  try {
    res = await fetchFn(CONFIG.api + '/dueno/resumen', { credentials: 'same-origin', redirect: 'manual', cache: 'no-store' });
  } catch {
    decir('No se pudo cargar el resumen. Prueba más tarde.');
    return;
  }
  if (res.type === 'opaqueredirect' || res.status === 401 || res.status === 403) {
    decir('Entra con tu correo de dueño.');
    if (entrar) entrar.hidden = false;
    if (datos) datos.hidden = true;
    return;
  }
  let r = null;
  try { if (res.ok) r = normalizarResumen(await res.json()); } catch { r = null; }
  if (!r) {
    decir('No se pudo cargar el resumen. Prueba más tarde.');
    return;
  }
  pintarResumen(doc, r);
  decir('');
  if (entrar) entrar.hidden = true;
  if (datos) datos.hidden = false;
}

if (typeof document !== 'undefined' && document.getElementById('dueno-datos')) {
  cargarResumen(document);
}

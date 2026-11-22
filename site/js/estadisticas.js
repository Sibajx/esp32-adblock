// Estadísticas públicas de SiBlock. Lo que llega del servidor se revisa y se pinta como texto.
import { CONFIG } from './config.js';
import { barras, semanaCorta } from './graficas.js';
import { FABRICANTES, MENUS } from './reporte.js';

const numero = new Intl.NumberFormat('es-MX');
const VERSION_RE = /^(v\d{1,4}\.\d{1,4}\.\d{1,4}|dev)$/;
const PAIS_RE = /^[A-Z]{2}$/;
let nombresRegion = null;
try { nombresRegion = new Intl.DisplayNames(['es'], { type: 'region' }); } catch { /* navegador viejo */ }

const entero = (n) => (Number.isSafeInteger(n) && n >= 0 ? n : 0);
const lista = (x) => (Array.isArray(x) ? x.filter((f) => f && typeof f === 'object') : []);
const nombreDe = (xs, id) => xs.find((x) => x.id === id)?.nombre ?? '';
const texto = (s, max) => (typeof s === 'string' ? s.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, max) : '');

/** Código de país → nombre en español. "otros" → "Otros países". */
export function nombrePais(codigo) {
  if (codigo === 'otros') return 'Otros países';
  if (typeof codigo !== 'string' || !PAIS_RE.test(codigo)) return 'Otro';
  try { return nombresRegion?.of(codigo) ?? codigo; } catch { return codigo; }
}

/** Deja solo lo que dice el contrato de GET /api/v1/estadisticas; null si no es un objeto. */
export function normalizarEstadisticas(d) {
  if (!d || typeof d !== 'object' || Array.isArray(d)) return null;
  return {
    placas7d: entero(d.placas7d),
    versiones: lista(d.versiones).filter((f) => typeof f.v === 'string' && VERSION_RE.test(f.v))
      .map((f) => ({ v: f.v, n: entero(f.n) })),
    paises: lista(d.paises).filter((f) => f.pais === 'otros' || (typeof f.pais === 'string' && PAIS_RE.test(f.pais)))
      .map((f) => ({ pais: f.pais, n: entero(f.n) })),
    bloqueadasSemanas: lista(d.bloqueadasSemanas).filter((f) => semanaCorta(f.desde) !== '')
      .map((f) => ({ desde: f.desde, n: entero(f.n) })),
    modems: lista(d.modems)
      .filter((f) => FABRICANTES.some((x) => x.id === f.fabricante) && MENUS.some((x) => x.id === f.menu))
      .map((f) => ({
        pais: typeof f.pais === 'string' && PAIS_RE.test(f.pais) ? f.pais : '',
        proveedor: texto(f.proveedor, 100),
        fabricante: f.fabricante,
        menu: f.menu,
        n: entero(f.n),
      })),
  };
}

const MAX_PAISES = 20;

/** Ordena de mayor a menor con «otros» al final; si son más de 20, el resto se suma a «otros». */
export function juntarPaises(paises) {
  const conNombre = paises.filter((p) => p.pais !== 'otros').sort((a, b) => b.n - a.n);
  let otros = paises.filter((p) => p.pais === 'otros').reduce((s, p) => s + p.n, 0);
  const lugares = otros > 0 || conNombre.length > MAX_PAISES ? MAX_PAISES - 1 : MAX_PAISES;
  for (const p of conNombre.slice(lugares)) otros += p.n;
  const out = conNombre.slice(0, lugares);
  if (otros > 0) out.push({ pais: 'otros', n: otros });
  return out;
}

function tablaModems(doc, modems) {
  if (modems.length === 0) {
    const p = doc.createElement('p');
    p.textContent = 'Todavía no hay datos.';
    p.setAttribute('class', 'sin-datos');
    return p;
  }
  const celda = (tag, t, scope) => {
    const c = doc.createElement(tag);
    c.textContent = t;
    if (scope) c.setAttribute('scope', scope);
    return c;
  };
  const tabla = doc.createElement('table');
  const cap = doc.createElement('caption');
  cap.textContent = 'Donde sí se pudo cambiar el DNS';
  const thead = doc.createElement('thead');
  const filaCab = doc.createElement('tr');
  for (const t of ['País', 'Compañía de internet', 'Módem', 'Menú', 'Reportes']) filaCab.appendChild(celda('th', t, 'col'));
  thead.appendChild(filaCab);
  const tbody = doc.createElement('tbody');
  for (const m of modems) {
    const tr = doc.createElement('tr');
    tr.append(
      celda('td', m.pais ? nombrePais(m.pais) : 'Sin país'),
      celda('th', m.proveedor || 'No se sabe', 'row'),
      celda('td', nombreDe(FABRICANTES, m.fabricante)),
      celda('td', nombreDe(MENUS, m.menu)),
      celda('td', numero.format(m.n)),
    );
    tbody.appendChild(tr);
  }
  tabla.append(cap, thead, tbody);
  return tabla;
}

function poner(doc, id, el) {
  const caja = doc.getElementById(id);
  if (caja) caja.replaceChildren(el);
}

/** Pinta las estadísticas ya normalizadas. */
export function pintarEstadisticas(doc, d) {
  if (!d) return;
  const placas = doc.getElementById('placas7d');
  if (placas) placas.textContent = numero.format(d.placas7d);
  poner(doc, 'g-versiones', barras(doc, d.versiones, { etiqueta: 'v', valor: 'n', titulo: 'Placas por versión de SiBlock' }));
  const paises = juntarPaises(d.paises);
  poner(doc, 'g-paises', barras(doc, paises, { etiqueta: (f) => nombrePais(f.pais), valor: 'n', titulo: 'Placas por país' }));
  const semanas = d.bloqueadasSemanas.some((s) => s.n > 0) ? d.bloqueadasSemanas : [];
  poner(doc, 'g-semanas', barras(doc, semanas, {
    etiqueta: (f) => semanaCorta(f.desde), valor: 'n', titulo: 'Anuncios quitados por semana (desde el lunes)',
  }));
  const modems = [...d.modems].sort((a, b) => b.n - a.n);
  const tabla = tablaModems(doc, modems);
  const caja = doc.getElementById('t-modems');
  if (caja) caja.replaceChildren(tabla);
}

/** Pide GET /api/v1/estadisticas y lo pinta; si falla, lo dice. */
export async function cargarEstadisticas(doc, fetchFn = globalThis.fetch) {
  const estado = doc.getElementById('est-estado');
  const fallo = () => { if (estado) estado.textContent = 'No se pudieron cargar las estadísticas. Prueba más tarde.'; };
  try {
    const res = await fetchFn(CONFIG.api + '/estadisticas', { credentials: 'omit', redirect: 'error' });
    if (!res.ok) return fallo();
    const d = normalizarEstadisticas(await res.json());
    if (!d) return fallo();
    pintarEstadisticas(doc, d);
    if (estado) estado.textContent = '';
  } catch {
    fallo();
  }
}

if (typeof document !== 'undefined' && document.getElementById('placas7d')) {
  cargarEstadisticas(document);
}

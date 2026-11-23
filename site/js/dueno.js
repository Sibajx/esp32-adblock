// Panel del dueño (detrás de Cloudflare Access). Lee GET /api/v1/dueno/resumen y aprueba o quita
// sitios de la lista de permitidos de la comunidad (POST /api/v1/dueno/permitidos[/quitar]).
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
// Misma regla que el Worker (esDominio) y la placa (validReportDomain).
const ETIQUETA = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
export function esDominio(s) {
  if (typeof s !== 'string' || s.length < 3 || s.length > 253) return false;
  const partes = s.split('.');
  return partes.length >= 2 && partes.every((p) => ETIQUETA.test(p));
}
const esFecha = (v) => typeof v === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(v);

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
    bloqueos: lista(d.bloqueos).filter((f) => typeof f.dominio === 'string' && f.dominio.length > 0 && f.dominio.length <= 253)
      .map((f) => ({
        dominio: f.dominio,
        n: entero(f.n),
        aprobado: f.aprobado === true,
        // Lo deja pasar un padre aprobado (x.com cubre cdn.x.com): solo si de verdad es su padre.
        cubiertoPor: f.aprobado === true && esDominio(f.cubiertoPor) && f.dominio.endsWith('.' + f.cubiertoPor) ? f.cubiertoPor : '',
      })).sort(porN).slice(0, 50),
    guiasFaltan: deLista(d.guiasFaltan, 'fabricante', (v) => FABRICANTES.some((t) => t.id === v)),
    placasTotal: entero(d.placasTotal),
    permitidos: lista(d.permitidos).filter((f) => esDominio(f.dominio))
      .map((f) => ({ dominio: f.dominio, desde: esFecha(f.desde) ? f.desde : '' })).slice(0, 500),
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

function boton(doc, texto, etiqueta, alTocar) {
  const b = doc.createElement('button');
  b.setAttribute('type', 'button');
  b.setAttribute('class', 'boton boton-secundario boton-chico');
  b.setAttribute('aria-label', etiqueta);
  b.textContent = texto;
  b.addEventListener('click', () => alTocar(b));
  return b;
}

function filaSitio(doc, dominio, detalle, b) {
  const li = doc.createElement('li');
  const nombre = doc.createElement('span');
  nombre.setAttribute('class', 'sitio');
  nombre.textContent = dominio;
  li.appendChild(nombre);
  if (detalle) {
    const d = doc.createElement('span');
    d.setAttribute('class', 'sitio-detalle');
    d.textContent = detalle;
    li.appendChild(d);
  }
  if (b) li.appendChild(b);
  return li;
}

function sinDatos(doc, texto) {
  const p = doc.createElement('p');
  p.setAttribute('class', 'sin-datos');
  p.textContent = texto;
  return p;
}

/**
 * Lista para revisar: cada sitio reportado con «Aprobar» (o «Quitar» si ya está aprobado), y la
 * lista de permitidos de la comunidad con «Quitar». `cambiar(dominio, aprobar)` hace el pedido.
 */
export function pintarPermitidos(doc, r, cambiar) {
  if (!r) return;
  const revisar = r.bloqueos.filter((f) => esDominio(f.dominio));
  if (revisar.length === 0) {
    poner(doc, 'd-revisar', sinDatos(doc, 'Todavía no hay sitios reportados.'));
  } else {
    const ul = doc.createElement('ul');
    ul.setAttribute('class', 'lista-sitios');
    for (const f of revisar) {
      const veces = f.n === 1 ? '1 reporte' : `${numero.format(f.n)} reportes`;
      if (f.cubiertoPor) {
        // Ya pasa por su padre: no hace falta aprobarlo aparte (se quita desde la lista de abajo).
        ul.appendChild(filaSitio(doc, f.dominio, `${veces} · cubierto por ${f.cubiertoPor}`, null));
        continue;
      }
      const b = f.aprobado
        ? boton(doc, 'Quitar', `Quitar ${f.dominio} de los permitidos`, (el) => cambiar(f.dominio, false, el))
        : boton(doc, 'Aprobar', `Aprobar ${f.dominio}`, (el) => cambiar(f.dominio, true, el));
      ul.appendChild(filaSitio(doc, f.dominio, f.aprobado ? `${veces} · aprobado` : veces, b));
    }
    poner(doc, 'd-revisar', ul);
  }
  const total = doc.getElementById('d-permitidos-total');
  if (total) total.textContent = numero.format(r.permitidos.length);
  if (r.permitidos.length === 0) {
    poner(doc, 'd-permitidos', sinDatos(doc, 'Todavía no apruebas ningún sitio.'));
    return;
  }
  const ul = doc.createElement('ul');
  ul.setAttribute('class', 'lista-sitios');
  for (const f of r.permitidos) {
    const b = boton(doc, 'Quitar', `Quitar ${f.dominio} de los permitidos`, (el) => cambiar(f.dominio, false, el));
    ul.appendChild(filaSitio(doc, f.dominio, f.desde ? `desde el ${f.desde}` : '', b));
  }
  poner(doc, 'd-permitidos', ul);
}

const MENSAJE_CAMBIO = Object.freeze({
  tope: 'La lista ya está llena (500 sitios o 30 KB de nombres). Quita alguno antes de aprobar otro.',
  sufijo: 'Ese nombre es de muchos sitios a la vez (como com.mx o github.io). Aprueba el sitio concreto.',
  datos: 'Ese nombre no es válido.',
  acceso: 'Tu sesión venció. Vuelve a entrar.',
});

/**
 * Aprueba (aprobar=true) o quita un sitio. Devuelve '' si salió bien o el texto del error.
 * Lleva la cookie de Access (same-origin) y no sigue redirecciones (sesión vencida → error).
 */
export async function cambiarPermitido(dominio, aprobar, fetchFn = globalThis.fetch) {
  if (!esDominio(dominio)) return MENSAJE_CAMBIO.datos;
  let res;
  try {
    res = await fetchFn(CONFIG.api + (aprobar ? '/dueno/permitidos' : '/dueno/permitidos/quitar'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ dominio }),
      credentials: 'same-origin',
      redirect: 'manual',
      cache: 'no-store',
    });
  } catch {
    return 'No se pudo guardar. Prueba más tarde.';
  }
  if (res.ok) return '';
  if (res.type === 'opaqueredirect' || res.status === 401) return MENSAJE_CAMBIO.acceso;
  let codigo = '';
  try { codigo = (await res.json())?.error ?? ''; } catch { codigo = ''; }
  return Object.hasOwn(MENSAJE_CAMBIO, codigo) ? MENSAJE_CAMBIO[codigo] : 'No se pudo guardar. Prueba más tarde.';
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
  pintarPermitidos(doc, r, async (dominio, aprobar, el) => {
    const aviso = doc.getElementById('d-permitidos-estado');
    if (el) el.disabled = true;
    const mal = await cambiarPermitido(dominio, aprobar, fetchFn);
    if (aviso) aviso.textContent = mal || (aprobar ? `Aprobado: ${dominio}.` : `Quitado: ${dominio}.`);
    if (mal) {
      if (el) el.disabled = false;
      return;
    }
    await cargarResumen(doc, fetchFn);
  });
  decir('');
  if (entrar) entrar.hidden = true;
  if (datos) datos.hidden = false;
}

if (typeof document !== 'undefined' && document.getElementById('dueno-datos')) {
  cargarResumen(document);
}

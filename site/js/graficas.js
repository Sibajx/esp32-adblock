// Gráficas de barras horizontales en SVG, hechas a mano (sin librerías).
// Todo el texto entra con textContent: un nombre raro se ve tal cual y nunca se interpreta.
const NS = 'http://www.w3.org/2000/svg';
const ANCHO = 360;
const FILA = 28;
const BARRA = 16;
const X_BARRA = 132;
const ANCHO_BARRA = 150; // termina en 282; los números van alineados a la derecha en 356
const MAX_ETIQUETA = 18;
const MESES = ['ene', 'feb', 'mar', 'abr', 'may', 'jun', 'jul', 'ago', 'sep', 'oct', 'nov', 'dic'];
const numero = new Intl.NumberFormat('es-MX');

function svg(doc, tag, attrs = {}, texto) {
  const el = doc.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
  if (texto !== undefined) el.textContent = String(texto);
  return el;
}

function leer(fila, campo) {
  if (typeof campo === 'function') return campo(fila);
  return fila && typeof fila === 'object' ? fila[campo] : undefined;
}

function aNumero(v) {
  const n = typeof v === 'string' && v.trim() !== '' ? Number(v) : v;
  return typeof n === 'number' && Number.isFinite(n) && n > 0 ? n : 0;
}

function cortar(s) {
  const letras = [...s];
  return letras.length > MAX_ETIQUETA ? letras.slice(0, MAX_ETIQUETA).join('') + '…' : s;
}

/**
 * Barras horizontales. `etiqueta` y `valor` son el nombre del campo o una función (fila → …).
 * Sin filas → <p>Todavía no hay datos.</p>.
 */
export function barras(doc, filas, { etiqueta, valor, titulo }) {
  if (!Array.isArray(filas) || filas.length === 0) {
    const p = doc.createElement('p');
    p.textContent = 'Todavía no hay datos.';
    p.setAttribute('class', 'sin-datos');
    return p;
  }
  const datos = filas.map((f) => ({ nombre: String(leer(f, etiqueta) ?? ''), n: aNumero(leer(f, valor)) }));
  const max = Math.max(...datos.map((d) => d.n));
  const alto = datos.length * FILA + 8;
  const raiz = svg(doc, 'svg', {
    viewBox: `0 0 ${ANCHO} ${alto}`, role: 'img', class: 'grafica', preserveAspectRatio: 'xMinYMin meet',
  });
  raiz.appendChild(svg(doc, 'title', {}, String(titulo ?? '')));
  datos.forEach((d, i) => {
    const y = 4 + i * FILA;
    const g = svg(doc, 'g', { class: 'grafica-fila' });
    g.appendChild(svg(doc, 'title', {}, `${d.nombre}: ${numero.format(d.n)}`));
    g.appendChild(svg(doc, 'text', { x: 0, y: y + 13, class: 'grafica-etiqueta' }, cortar(d.nombre)));
    const ancho = max > 0 ? Math.round((d.n / max) * ANCHO_BARRA * 10) / 10 : 0;
    g.appendChild(svg(doc, 'rect', { x: X_BARRA, y: y + 1, width: ancho, height: BARRA, rx: 3, class: 'grafica-barra' }));
    g.appendChild(svg(doc, 'text', { x: ANCHO - 4, y: y + 13, class: 'grafica-valor', 'text-anchor': 'end' }, numero.format(d.n)));
    raiz.appendChild(g);
  });
  return raiz;
}

/** "2026-11-16" → "16 nov"; lo que no sea una fecha válida → "". */
export function semanaCorta(desde) {
  if (typeof desde !== 'string') return '';
  const m = desde.match(/^(\d{4})-(\d{2})-(\d{2})$/);
  if (!m) return '';
  const [a, mes, dia] = [Number(m[1]), Number(m[2]), Number(m[3])];
  const f = new Date(Date.UTC(a, mes - 1, dia));
  if (f.getUTCFullYear() !== a || f.getUTCMonth() !== mes - 1 || f.getUTCDate() !== dia) return '';
  return `${dia} ${MESES[mes - 1]}`;
}

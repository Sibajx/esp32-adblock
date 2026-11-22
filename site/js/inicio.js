// Se carga en todas las páginas. Prepara la analítica sin cookies (si hay token) y el botón de
// apoyo del pie (si CI puso un enlace).
import { CONFIG } from './config.js';
import { cargarAnalitica } from './nube.js';
import { texto } from './textos.js';

// La misma regla que usa CI al escribir config.js (contrato entre carriles).
const APOYO_RE = /^https:\/\/[A-Za-z0-9.-]{1,100}(\/[A-Za-z0-9._~/-]{0,150})?$/;

/** El enlace de apoyo si es un https:// limpio; si no, ''. */
export function enlaceApoyo(url) {
  if (typeof url !== 'string' || !APOYO_RE.test(url)) return '';
  try {
    return new URL(url).protocol === 'https:' ? url : '';
  } catch {
    return '';
  }
}

/** Muestra el botón #apoyo solo si hay un enlace bueno. Devuelve true si lo mostró. */
export function ponerApoyo(doc, url) {
  const boton = doc?.getElementById?.('apoyo');
  const href = enlaceApoyo(url);
  if (!boton || !href) return false;
  boton.setAttribute('href', href);
  boton.textContent = texto('apoyo', doc);
  boton.hidden = false;
  return true;
}

if (typeof document !== 'undefined') {
  cargarAnalitica(document, CONFIG, typeof navigator !== 'undefined' ? navigator : undefined);
  ponerApoyo(document, CONFIG.apoyoUrl);
}

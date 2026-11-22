// Selector de guías del router de SiBlock.
// Sin cookies ni nada guardado: el país sale de lo que ve el servidor (/api/v1/pais) o, si no se
// puede, del idioma del navegador; y siempre se puede cambiar.
// Todo se dibuja con textContent / createElement: nunca HTML armado con texto.
import { MARCAS, PAISES, FICHAS } from './guias-datos.js';
import { pedirPais } from './nube.js';

export const IPS_GENERALES = Object.freeze(['192.168.1.1', '192.168.0.1']);

export const PASOS_GENERALES = Object.freeze([
  'Mira la etiqueta de abajo de tu módem: ahí vienen la dirección para entrar (casi siempre 192.168.1.1 o 192.168.0.1), el usuario y la contraseña.',
  'Abre esa dirección en el navegador de un aparato conectado a tu WiFi y entra.',
  'Busca la parte que diga DHCP, LAN o DNS.',
  'Donde dice DNS primario, pon la dirección de tu SiBlock. Deja vacío el DNS secundario: si pones otro, los aparatos lo usan a ratos y se saltan SiBlock. Si te obliga a llenarlo, pon la misma dirección de tu SiBlock.',
  'Si ves algo de DNS por IPv6, apágalo o pon ahí también tu SiBlock.',
  'Guarda, y en cada aparato apaga y prende el WiFi.',
  'Abre tu panel desde ese aparato: arriba debe decir que este aparato usa SiBlock.',
]);

const OTRO_PROVEEDOR = Object.freeze({ id: 'otro', nombre: 'Otro proveedor' });
const MARCAS_SIN_NOMBRE = new Set(['proveedor', 'no-se']);
const PAISES_CONOCIDOS = new Set(PAISES.map((p) => p.id));

/** 'es-MX' → 'MX'. Sin región o país que no está en la lista → 'otro'. */
export function paisDesdeIdioma(idioma) {
  if (typeof idioma !== 'string') return 'otro';
  const partes = idioma.split(/[-_]/);
  if (partes.length < 2) return 'otro';
  const region = partes[partes.length - 1].toUpperCase();
  return region !== 'OTRO' && PAISES_CONOCIDOS.has(region) ? region : 'otro';
}

/** Proveedores del país más "Otro proveedor" al final (lista nueva cada vez). */
export function proveedoresDe(paisId) {
  const pais = PAISES.find((p) => p.id === paisId);
  return [...(pais ? pais.proveedores : []), OTRO_PROVEEDOR];
}

function buscarMarca(id) {
  return MARCAS.find((m) => m.id === id) ?? null;
}

function buscarProveedor(paisId, id) {
  if (id === OTRO_PROVEEDOR.id) return null;
  const pais = PAISES.find((p) => p.id === paisId);
  return pais?.proveedores.find((p) => p.id === id) ?? null;
}

function fichaValida(f) {
  return !!f && typeof f === 'object' && typeof f.fuente === 'string' && f.fuente.length > 0;
}

/**
 * Guía para lo que eligió la persona. Sin ficha comprobada → pasos generales y "falta verificar".
 * `fichas` solo se cambia en las pruebas.
 */
export function guiaPara(eleccion, fichas = FICHAS) {
  const { marca, pais, proveedor } = eleccion ?? {};
  const m = buscarMarca(marca);
  const marcaConNombre = m && !MARCAS_SIN_NOMBRE.has(m.id) ? m : null;
  const prov = buscarProveedor(pais, proveedor);

  let titulo = 'Cómo poner SiBlock en tu módem';
  let aviso = 'Esta es la guía general: sirve para casi todos los módems.';
  let ficha = null;
  if (marcaConNombre) {
    titulo = `Cómo poner SiBlock en un módem ${marcaConNombre.nombre}`;
    aviso = 'Falta verificar: esta guía es general para tu marca. Los nombres del menú pueden cambiar.';
    ficha = fichas[marcaConNombre.id];
  } else if (prov) {
    titulo = `Cómo poner SiBlock en el módem de ${prov.nombre}`;
    aviso = 'Falta verificar: esta guía es general para tu proveedor. Los nombres del menú pueden cambiar.';
    ficha = fichas[`${pais}:${prov.id}`];
  }

  if (fichaValida(ficha)) {
    const pasos = Array.isArray(ficha.pasos) ? [...ficha.pasos] : [...PASOS_GENERALES];
    if (!Array.isArray(ficha.pasos) && typeof ficha.menu === 'string') {
      pasos[2] = `Ve a: ${ficha.menu}.`;
    }
    return {
      titulo,
      aviso: null,
      ips: Array.isArray(ficha.ips) && ficha.ips.length ? [...ficha.ips] : [...IPS_GENERALES],
      pasos,
      verificada: true,
      fuente: ficha.fuente,
    };
  }
  return { titulo, aviso, ips: [...IPS_GENERALES], pasos: [...PASOS_GENERALES], verificada: false, fuente: null };
}

function nuevo(doc, tag, texto, clase) {
  const el = doc.createElement(tag);
  if (texto !== undefined) el.textContent = String(texto);
  if (clase) el.className = clase;
  return el;
}

/** Dibuja la guía en <section id="guia">. Solo texto. */
export function pintarGuia(doc, guia) {
  const sec = doc.getElementById('guia');
  if (!sec || !guia) return;
  const partes = [nuevo(doc, 'h2', guia.titulo)];
  if (!guia.verificada && guia.aviso) partes.push(nuevo(doc, 'p', guia.aviso, 'aviso aviso-falta'));

  const entrar = nuevo(doc, 'p', 'Para entrar a tu módem prueba primero con: ');
  (guia.ips ?? []).forEach((ip, i) => {
    if (i > 0) entrar.appendChild(nuevo(doc, 'span', ' o '));
    entrar.appendChild(nuevo(doc, 'strong', ip));
  });
  partes.push(entrar);

  const ol = nuevo(doc, 'ol', undefined, 'pasos-router');
  for (const paso of guia.pasos ?? []) ol.appendChild(nuevo(doc, 'li', paso));
  partes.push(ol);

  if (guia.fuente) partes.push(nuevo(doc, 'p', `Fuente: ${guia.fuente}`, 'nota'));
  sec.replaceChildren(...partes);
}

function llenar(doc, select, opciones, primera) {
  const lista = [];
  if (primera) lista.push(primera);
  lista.push(...opciones);
  select.replaceChildren(...lista.map(({ id, nombre }) => {
    const op = nuevo(doc, 'option', nombre);
    op.value = id;
    return op;
  }));
}

/**
 * Llena las listas, elige el país por el idioma y repinta la guía al cambiar algo.
 * Con `fetchFn`, pregunta el país al servidor: si es uno de la lista y la persona todavía no
 * cambió el país, se usa ese. Devuelve una promesa que termina cuando ya se sabe.
 */
export async function iniciar(doc, nav, fetchFn) {
  const marca = doc.getElementById('marca');
  const pais = doc.getElementById('pais');
  const proveedor = doc.getElementById('proveedor');
  if (!marca || !pais || !proveedor) return;

  const llenarProveedores = () => {
    llenar(doc, proveedor, proveedoresDe(pais.value), { id: '', nombre: 'Elige quién te da el internet' });
    proveedor.value = '';
  };
  const pintar = () => pintarGuia(doc, guiaPara({ marca: marca.value, pais: pais.value, proveedor: proveedor.value }));

  llenar(doc, marca, MARCAS, { id: '', nombre: 'Elige la marca de tu módem' });
  marca.value = '';
  llenar(doc, pais, PAISES);
  pais.value = paisDesdeIdioma(nav && typeof nav === 'object' ? nav.language : undefined);
  llenarProveedores();
  pintar();
  const caja = doc.getElementById('elegir');
  if (caja) caja.hidden = false;

  let tocado = false;
  marca.addEventListener('change', pintar);
  proveedor.addEventListener('change', () => { tocado = true; pintar(); });
  pais.addEventListener('change', () => { tocado = true; llenarProveedores(); pintar(); });

  if (typeof fetchFn !== 'function') return;
  const info = await pedirPais(fetchFn);
  const id = info?.pais;
  if (tocado || !id || id === 'otro' || !PAISES_CONOCIDOS.has(id) || id === pais.value) return;
  pais.value = id;
  llenarProveedores();
  pintar();
}

if (typeof document !== 'undefined' && typeof navigator !== 'undefined') {
  iniciar(document, navigator, globalThis.fetch);
}

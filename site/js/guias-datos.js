// Datos de las guías del router de SiBlock.
// Regla: sin fuente no se inventa. Las marcas y proveedores de aquí solo sirven para elegir;
// la guía que se muestra es la general hasta que haya una ficha comprobada en FICHAS.

const congelar = (xs) => Object.freeze(xs.map((x) => Object.freeze(x)));

/** Marcas de módem o router, en el orden en que salen en la lista. */
export const MARCAS = congelar([
  { id: 'tp-link', nombre: 'TP-Link' },
  { id: 'huawei', nombre: 'Huawei' },
  { id: 'zte', nombre: 'ZTE' },
  { id: 'technicolor', nombre: 'Technicolor' },
  { id: 'arris', nombre: 'Arris' },
  { id: 'netgear', nombre: 'Netgear' },
  { id: 'asus', nombre: 'ASUS' },
  { id: 'mikrotik', nombre: 'MikroTik' },
  { id: 'proveedor', nombre: 'El módem que me dio mi proveedor' },
  { id: 'no-se', nombre: 'No sé / otra' },
]);

/** Países y sus proveedores de internet más grandes. */
export const PAISES = Object.freeze([
  {
    id: 'MX',
    nombre: 'México',
    proveedores: congelar([
      { id: 'telmex', nombre: 'Telmex (Infinitum)' },
      { id: 'totalplay', nombre: 'Totalplay' },
      { id: 'izzi', nombre: 'izzi' },
      { id: 'megacable', nombre: 'Megacable' },
    ]),
  },
  {
    id: 'CO',
    nombre: 'Colombia',
    proveedores: congelar([
      { id: 'claro', nombre: 'Claro' },
      { id: 'movistar', nombre: 'Movistar' },
      { id: 'tigo', nombre: 'Tigo' },
      { id: 'etb', nombre: 'ETB' },
    ]),
  },
  {
    id: 'ES',
    nombre: 'España',
    proveedores: congelar([
      { id: 'movistar', nombre: 'Movistar' },
      { id: 'vodafone', nombre: 'Vodafone' },
      { id: 'orange', nombre: 'Orange' },
      { id: 'digi', nombre: 'Digi' },
    ]),
  },
  {
    id: 'US',
    nombre: 'Estados Unidos',
    proveedores: congelar([
      { id: 'xfinity', nombre: 'Xfinity (Comcast)' },
      { id: 'spectrum', nombre: 'Spectrum' },
      { id: 'att', nombre: 'AT&T' },
      { id: 'verizon', nombre: 'Verizon' },
    ]),
  },
  { id: 'otro', nombre: 'Otro país', proveedores: congelar([]) },
].map((p) => Object.freeze(p)));

/**
 * Fichas comprobadas. Vacío a propósito: aquí solo entra una ficha con su fuente.
 * Clave: id de marca (p. ej. "tp-link") o "PAÍS:proveedor" (p. ej. "MX:telmex").
 * Valor: { ips: ['192.168.1.1'], menu: 'Avanzado → Red → LAN', pasos?: [...], fuente: 'de dónde sale' }.
 */
export const FICHAS = Object.freeze({});

import { sha256Hex } from "./http";

export function esId(s: unknown): s is string {
  return typeof s === "string" && /^[0-9a-f]{32}$/.test(s);
}

/** Número que se guarda en D1 en lugar del de la instalación. */
export async function hashId(id: string, pepper: string): Promise<string> {
  return (await sha256Hex(pepper + id)).slice(0, 32);
}

const ETIQUETA = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/;

/** Misma regla que `validReportDomain` de la placa. */
export function esDominio(s: unknown): s is string {
  if (typeof s !== "string" || s.length < 3 || s.length > 253) return false;
  const partes = s.split(".");
  return partes.length >= 2 && partes.every((p) => ETIQUETA.test(p));
}

const PARTE = "(?:0|[1-9][0-9]{0,3})";
const VERSION = new RegExp(`^v${PARTE}\\.${PARTE}\\.${PARTE}$`);

export function esVersion(s: unknown): s is string {
  return typeof s === "string" && (s === "dev" || VERSION.test(s));
}

/**
 * Correo: ≤ 254, un solo @ con algo a cada lado, sin espacios ni controles, y sin los
 * signos que cambian el sentido de una cabecera (`<>",;()\[]`).
 */
export function esCorreo(s: unknown): s is string {
  if (typeof s !== "string" || s.length > 254) return false;
  if (/[\s\u0000-\u001f\u007f<>",;()\\[\]]/.test(s)) return false;
  const partes = s.split("@");
  return partes.length === 2 && partes[0].length > 0 && partes[1].length > 0;
}

/** Si el límite falla, no deja pasar. */
export async function dentroDelLimite(lim: RateLimit, clave: string): Promise<boolean> {
  try {
    const { success } = await lim.limit({ key: clave });
    return success === true;
  } catch {
    return false;
  }
}

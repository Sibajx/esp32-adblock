import { claveIp, dia, error, json } from "./http";
import { enLista, FABRICANTES, MENUS, MOTIVOS } from "./opciones";
import { verificarTurnstile } from "./turnstile";
import type { Deps, Env, Manejador } from "./tipos";
import { dentroDelLimite, esDominio, esId, hashId } from "./validar";

/** País (2 letras) y proveedor de quien pregunta, según Cloudflare. Nada de esto identifica una casa. */
export function datosRed(req: Request): { pais: string; asn: number | null; proveedor: string } {
  const cf = (req.cf ?? {}) as Record<string, unknown>;
  const pais = typeof cf.country === "string" && /^[A-Z][A-Z0-9]$/.test(cf.country) ? cf.country : "";
  const asn = typeof cf.asn === "number" && Number.isInteger(cf.asn) && cf.asn > 0 ? cf.asn : null;
  const proveedor =
    typeof cf.asOrganization === "string"
      ? cf.asOrganization.replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, 100)
      : "";
  return { pais, asn, proveedor };
}

/**
 * Pasos comunes a los dos reportes: límite por IP y `origen` válido.
 * Devuelve una respuesta de error o `null` si se puede seguir.
 */
export async function revisarOrigen(req: Request, env: Env, c: Record<string, unknown>): Promise<Response | null> {
  if (!(await dentroDelLimite(env.LIMITE, await claveIp(req, env, "reporte")))) return error(429, "limite");
  if (c.origen !== "pagina" && c.origen !== "panel") return error(400, "datos");
  return null;
}

/** Turnstile obligatorio si el reporte viene de la página. */
export async function revisarTurnstile(env: Env, deps: Deps, c: Record<string, unknown>): Promise<Response | null> {
  if (c.origen === "pagina" && !(await verificarTurnstile(c.turnstile, env, deps))) return error(403, "turnstile");
  return null;
}

export const reporteModem: Manejador = async (req, env, deps, c = {}) => {
  const previo = await revisarOrigen(req, env, c);
  if (previo) return previo;
  if (!esId(c.i) || !enLista(FABRICANTES, c.fabricante) || typeof c.dns_ok !== "boolean" || !enLista(MENUS, c.menu)) {
    return error(400, "datos");
  }
  const t = await revisarTurnstile(env, deps, c);
  if (t) return t;
  const { pais, asn, proveedor } = datosRed(req);
  await env.DB.prepare(
    `INSERT INTO reportes_modem (instalacion, origen, pais, asn, proveedor, fabricante, dns_ok, menu, dia)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
     ON CONFLICT(instalacion) DO UPDATE SET origen = excluded.origen, pais = excluded.pais, asn = excluded.asn,
       proveedor = excluded.proveedor, fabricante = excluded.fabricante, dns_ok = excluded.dns_ok,
       menu = excluded.menu, dia = excluded.dia`,
  )
    .bind(await hashId(c.i, env.ID_PEPPER), c.origen, pais, asn, proveedor, c.fabricante, c.dns_ok ? 1 : 0, c.menu, dia(deps.ahora()))
    .run();
  return json(200, { ok: true });
};

/** País y proveedor de quien pregunta, para la guía del router. No guarda nada. */
export const pais: Manejador = async (req) => {
  const { pais, proveedor } = datosRed(req);
  return json(200, { pais, proveedor });
};

/** "Esto no debió bloquearse": uno por número y dominio. */
export const reporteBloqueo: Manejador = async (req, env, deps, c = {}) => {
  const previo = await revisarOrigen(req, env, c);
  if (previo) return previo;
  if (!esId(c.i) || !esDominio(c.dominio) || !enLista(MOTIVOS, c.motivo)) return error(400, "datos");
  const t = await revisarTurnstile(env, deps, c);
  if (t) return t;
  await env.DB.prepare(
    `INSERT INTO reportes_bloqueo (instalacion, dominio, motivo, origen, dia) VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT(instalacion, dominio) DO UPDATE SET motivo = excluded.motivo, origen = excluded.origen, dia = excluded.dia`,
  )
    .bind(await hashId(c.i, env.ID_PEPPER), c.dominio, c.motivo, c.origen, dia(deps.ahora()))
    .run();
  return json(200, { ok: true });
};

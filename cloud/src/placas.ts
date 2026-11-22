import { claveIp, dia, error, json } from "./http";
import { enLista, PASOS } from "./opciones";
import { datosRed } from "./reportes";
import type { Manejador } from "./tipos";
import { dentroDelLimite, esId, esVersion, hashId } from "./validar";

const MAX_BLOQUEADAS = 100_000_000;
/** Una casa real no pasa de esto en un día; más alto se guarda como este tope para no inflar las estadísticas. */
export const TOPE_BLOQUEADAS_DIA = 1_000_000;

/** Aviso diario de la placa (solo con "Compartir datos anónimos" encendido): versión y bloqueadas en 24 h. */
export const avisoPlaca: Manejador = async (req, env, deps, c = {}) => {
  if (!(await dentroDelLimite(env.LIMITE, await claveIp(req, env, "placa")))) return error(429, "limite");
  const b = c.b;
  if (!esId(c.i) || !esVersion(c.v) || typeof b !== "number" || !Number.isInteger(b) || b < 0 || b > MAX_BLOQUEADAS) {
    return error(400, "datos");
  }
  const id = await hashId(c.i, env.ID_PEPPER);
  const hoy = dia(deps.ahora());
  const { pais } = datosRed(req);
  await env.DB.batch([
    env.DB.prepare(
      `INSERT INTO placas (instalacion, dia, version, pais) VALUES (?1, ?2, ?3, ?4)
       ON CONFLICT(instalacion) DO UPDATE SET dia = excluded.dia, version = excluded.version, pais = excluded.pais`,
    ).bind(id, hoy, c.v, pais),
    env.DB.prepare(
      `INSERT INTO bloqueadas (dia, instalacion, total) VALUES (?1, ?2, ?3)
       ON CONFLICT(dia, instalacion) DO UPDATE SET total = excluded.total`,
    ).bind(hoy, id, Math.min(b, TOPE_BLOQUEADAS_DIA)),
  ]);
  return json(200, { ok: true });
};

/** Embudo de la página: un contador por día y paso, sin número ni cookies. */
export const paso: Manejador = async (req, env, deps, c = {}) => {
  if (!(await dentroDelLimite(env.LIMITE, await claveIp(req, env, "paso")))) return error(429, "limite");
  if (!enLista(PASOS, c.p)) return error(400, "datos");
  await env.DB.prepare(
    `INSERT INTO pasos (dia, paso, n) VALUES (?1, ?2, 1) ON CONFLICT(dia, paso) DO UPDATE SET n = n + 1`,
  )
    .bind(dia(deps.ahora()), c.p)
    .run();
  return json(204, null);
};

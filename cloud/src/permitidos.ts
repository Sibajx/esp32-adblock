import { verificarAccess } from "./access";
import { dia, error, json } from "./http";
import type { Env, Manejador } from "./tipos";
import { esDominio } from "./validar";

/** Tope de la lista: las placas tampoco aceptan más (lib/siblock_core/community.h). */
export const MAX_PERMITIDOS = 500;

/** Llave fija de la caché (como la de estadísticas): solo vive en el borde. */
export const LLAVE_CACHE_PERMITIDOS = "https://cache.siblock.invalid/api/v1/permitidos";
const SEGUNDOS_CACHE = 600;

/**
 * Lista pública que bajan las placas con «Usar los permitidos de la comunidad» encendido.
 * Sin número, sin Turnstile y sin nada de quien pregunta: es igual para todos.
 */
export const permitidos: Manejador = async (_req, env, deps, _c, ctx) => {
  if (deps.cache) {
    const guardada = await deps.cache.match(LLAVE_CACHE_PERMITIDOS);
    if (guardada) return guardada;
  }
  const [filas, version] = await env.DB.batch([
    env.DB.prepare("SELECT dominio FROM permitidos ORDER BY dominio"),
    env.DB.prepare("SELECT n FROM permitidos_version WHERE id = 1"),
  ]);
  const respuesta = json(
    200,
    {
      version: (version.results[0] as { n: number } | undefined)?.n ?? 0,
      dominios: (filas.results as { dominio: string }[]).map((f) => f.dominio),
    },
    `public, max-age=${SEGUNDOS_CACHE}`,
  );
  if (deps.cache) ctx.waitUntil(deps.cache.put(LLAVE_CACHE_PERMITIDOS, respuesta.clone()));
  return respuesta;
};

/** Sube la versión y borra la copia de la caché de este borde (las demás vencen solas en 10 min). */
async function cambio(env: Env, cache: Cache | undefined): Promise<void> {
  await env.DB.prepare(
    "INSERT INTO permitidos_version (id, n) VALUES (1, 1) ON CONFLICT(id) DO UPDATE SET n = n + 1",
  ).run();
  if (cache) await cache.delete(LLAVE_CACHE_PERMITIDOS).catch(() => false);
}

/** El dueño aprueba un sitio. Ya aprobado → 200 sin cambiar nada; con 500 → 409 `tope`. */
export const aprobarPermitido: Manejador = async (req, env, deps, c = {}) => {
  if (!(await verificarAccess(req, env, deps))) return error(403, "acceso");
  if (!esDominio(c.dominio)) return error(400, "datos");
  // El tope va en la misma sentencia: dos aprobaciones a la vez no lo pasan.
  const r = await env.DB.prepare(
    `INSERT OR IGNORE INTO permitidos (dominio, aprobado_en)
     SELECT ?1, ?2 WHERE (SELECT COUNT(*) FROM permitidos) < ?3`,
  )
    .bind(c.dominio, dia(deps.ahora()), MAX_PERMITIDOS)
    .run();
  if (r.meta.changes > 0) {
    await cambio(env, deps.cache);
    return json(200, { ok: true });
  }
  const ya = await env.DB.prepare("SELECT 1 AS si FROM permitidos WHERE dominio = ?1").bind(c.dominio).first();
  return ya ? json(200, { ok: true }) : error(409, "tope");
};

/** El dueño quita un sitio. Si no estaba → 200 igual. */
export const quitarPermitido: Manejador = async (req, env, deps, c = {}) => {
  if (!(await verificarAccess(req, env, deps))) return error(403, "acceso");
  if (!esDominio(c.dominio)) return error(400, "datos");
  const r = await env.DB.prepare("DELETE FROM permitidos WHERE dominio = ?1").bind(c.dominio).run();
  if (r.meta.changes > 0) await cambio(env, deps.cache);
  return json(200, { ok: true });
};

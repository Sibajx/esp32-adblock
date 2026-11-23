import { verificarAccess } from "./access";
import { dia, error, json } from "./http";
import type { Manejador } from "./tipos";
import { esDominio } from "./validar";

/** Tope de la lista: las placas tampoco aceptan más (lib/siblock_core/community.h). */
export const MAX_PERMITIDOS = 500;
/**
 * Tope de lo que ocupan los nombres en el JSON público: la placa no acepta cuerpos de más de 32 KB
 * (kCommunityMaxBody); con 30 000 sobra lugar para `{"version":…,"dominios":[]}`.
 */
export const MAX_BYTES_PERMITIDOS = 30000;

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

/**
 * Sufijos públicos y servicios donde cualquiera tiene su sitio: aprobar uno de estos dejaría pasar
 * los subdominios de miles de desconocidos (anuncios incluidos) en todas las placas.
 */
const SUFIJOS = new Set([
  "com.mx", "com.ar", "com.co", "com.br", "com.pe", "com.ve", "com.ec", "com.gt", "com.uy", "com.bo",
  "com.py", "com.sv", "com.hn", "com.ni", "com.pa", "com.do", "co.cr", "co.uk", "com.es", "gob.mx",
  "edu.mx", "blogspot.com", "github.io", "herokuapp.com", "appspot.com", "cloudfront.net",
  "azurewebsites.net", "pages.dev", "workers.dev", "vercel.app", "netlify.app", "firebaseapp.com",
  "web.app", "s3.amazonaws.com", "amazonaws.com", "googleusercontent.com", "akamaized.net",
  "akamaihd.net", "fastly.net", "cdn.cloudflare.net",
]);
/** Segundo nivel de un país: "com.xx", "co.xx", "org.xx", "gob.xx", "gov.xx", "edu.xx", "net.xx", "ac.xx", "mil.xx". */
const SEGUNDO_NIVEL = /^(?:co|com|org|gob|gov|edu|net|ac|mil)\.[a-z]{2}$/;

/** true si es un sufijo público o un servicio compartido (ver SUFIJOS): no se aprueba. */
export function esSufijo(dominio: string): boolean {
  return SUFIJOS.has(dominio) || SEGUNDO_NIVEL.test(dominio);
}

/** Borra la copia de la caché de este borde (las demás vencen solas en 10 min). */
async function borrarCache(cache: Cache | undefined): Promise<void> {
  if (cache) await cache.delete(LLAVE_CACHE_PERMITIDOS).catch(() => false);
}

/**
 * El dueño aprueba un sitio. Ya aprobado → 200 sin cambiar nada; sufijo público → 400 `sufijo`;
 * lista llena (500 sitios o MAX_BYTES_PERMITIDOS) → 409 `tope`.
 * La versión la sube un disparador de D1 (migrations/0004) en la misma sentencia.
 */
export const aprobarPermitido: Manejador = async (req, env, deps, c = {}) => {
  if (!(await verificarAccess(req, env, deps))) return error(403, "acceso");
  if (!esDominio(c.dominio)) return error(400, "datos");
  if (esSufijo(c.dominio)) return error(400, "sufijo");
  // Los topes van en la misma sentencia: dos aprobaciones a la vez no los pasan. Cada sitio ocupa
  // su largo + 3 bytes en el JSON público ("", y la coma).
  const r = await env.DB.prepare(
    `INSERT OR IGNORE INTO permitidos (dominio, aprobado_en)
     SELECT ?1, ?2 WHERE (SELECT COUNT(*) FROM permitidos) < ?3
       AND (SELECT COALESCE(SUM(length(dominio) + 3), 0) FROM permitidos) + length(?1) + 3 <= ?4`,
  )
    .bind(c.dominio, dia(deps.ahora()), MAX_PERMITIDOS, MAX_BYTES_PERMITIDOS)
    .run();
  if (r.meta.changes > 0) {
    await borrarCache(deps.cache);
    return json(200, { ok: true });
  }
  const ya = await env.DB.prepare("SELECT 1 AS si FROM permitidos WHERE dominio = ?1").bind(c.dominio).first();
  return ya ? json(200, { ok: true }) : error(409, "tope");
};

/** El dueño quita un sitio. Si no estaba → 200 igual. La versión la sube el disparador. */
export const quitarPermitido: Manejador = async (req, env, deps, c = {}) => {
  if (!(await verificarAccess(req, env, deps))) return error(403, "acceso");
  if (!esDominio(c.dominio)) return error(400, "datos");
  const r = await env.DB.prepare("DELETE FROM permitidos WHERE dominio = ?1").bind(c.dominio).run();
  if (r.meta.changes > 0) await borrarCache(deps.cache);
  return json(200, { ok: true });
};

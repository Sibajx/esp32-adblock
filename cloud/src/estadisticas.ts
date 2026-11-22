import { dia, json } from "./http";
import type { Manejador } from "./tipos";

/** Un país sale solo con 5 placas o más; un grupo de módem, con 3 reportes iguales o más. */
export const MIN_PAIS = 5;
export const MIN_MODEM = 3;
const SEMANAS = 12;
const DIA_MS = 86400000;

/** Lunes (UTC) de la semana del día `d` (el día 0, 1970-01-01, fue jueves). */
export function lunes(d: number): number {
  return d - ((((d + 3) % 7) + 7) % 7);
}

function fecha(d: number): string {
  return new Date(d * DIA_MS).toISOString().slice(0, 10);
}

/**
 * Llave fija de la caché: sin la URL del pedido, así `?x=123` no obliga a consultar D1 otra vez.
 * Solo vive en la caché del borde, no es una dirección que se pida.
 */
export const LLAVE_CACHE = "https://cache.siblock.invalid/api/v1/estadisticas";
const SEGUNDOS_CACHE = 300;

export const estadisticas: Manejador = async (_req, env, deps, _c, ctx) => {
  if (deps.cache) {
    const guardada = await deps.cache.match(LLAVE_CACHE);
    if (guardada) return guardada;
  }
  const respuesta = await calcular(env, deps.ahora());
  if (deps.cache) ctx.waitUntil(deps.cache.put(LLAVE_CACHE, respuesta.clone()));
  return respuesta;
};

async function calcular(env: Parameters<Manejador>[1], ahora: number): Promise<Response> {
  const hoy = dia(ahora);
  const desde7 = hoy - 6;
  const primerLunes = lunes(hoy) - 7 * (SEMANAS - 1);

  const [versiones, paises, semanas, modems] = await env.DB.batch([
    env.DB.prepare("SELECT version AS v, COUNT(*) AS n FROM placas WHERE dia >= ?1 GROUP BY version ORDER BY n DESC, v").bind(desde7),
    env.DB.prepare("SELECT pais, COUNT(*) AS n FROM placas WHERE dia >= ?1 GROUP BY pais ORDER BY n DESC, pais").bind(desde7),
    env.DB.prepare(
      // D1 pasa los números como reales: CAST para que la división sea entera.
      "SELECT (dia - CAST(?1 AS INTEGER)) / 7 AS semana, SUM(total) AS n FROM bloqueadas WHERE dia >= ?1 AND dia <= ?2 GROUP BY semana",
    ).bind(primerLunes, hoy),
    env.DB.prepare(
      `SELECT pais, proveedor, fabricante, menu, COUNT(*) AS n FROM reportes_modem
       WHERE dns_ok = 1 AND menu <> 'otro' AND fabricante <> 'no-se'
       GROUP BY pais, proveedor, fabricante, menu HAVING COUNT(*) >= ?1
       ORDER BY n DESC, pais, proveedor, fabricante, menu`,
    ).bind(MIN_MODEM),
  ]);

  const filasPais = paises.results as { pais: string; n: number }[];
  const listaPaises = filasPais.filter((f) => f.pais !== "" && f.n >= MIN_PAIS);
  const otros = filasPais.filter((f) => f.pais === "" || f.n < MIN_PAIS).reduce((a, f) => a + f.n, 0);
  if (otros >= MIN_PAIS) listaPaises.push({ pais: "otros", n: otros });

  const porSemana = new Map((semanas.results as { semana: number; n: number }[]).map((f) => [f.semana, f.n]));
  const bloqueadasSemanas = Array.from({ length: SEMANAS }, (_, k) => ({
    desde: fecha(primerLunes + 7 * k),
    n: porSemana.get(k) ?? 0,
  }));

  return json(
    200,
    {
      placas7d: filasPais.reduce((a, f) => a + f.n, 0),
      versiones: versiones.results,
      paises: listaPaises,
      bloqueadasSemanas,
      modems: modems.results,
    },
    `public, max-age=${SEGUNDOS_CACHE}`,
  );
}

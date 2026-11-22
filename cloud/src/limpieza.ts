import { dia } from "./http";
import type { Env } from "./tipos";

export const DIAS_GUARDADOS = 400;
export const TABLAS_CON_DIA = ["bloqueadas", "pasos", "temas", "contacto_dia", "placas"] as const;

/**
 * Cron diario: borra los conteos de hace más de 400 días, y las marcas de respuesta automática
 * de días anteriores (solo sirven para el día en curso).
 */
export async function limpiar(env: Env, ahora: number): Promise<void> {
  const hoy = dia(ahora);
  const limite = hoy - DIAS_GUARDADOS;
  await env.DB.batch([
    ...TABLAS_CON_DIA.map((t) => env.DB.prepare(`DELETE FROM ${t} WHERE dia < ?1`).bind(limite)),
    env.DB.prepare("DELETE FROM respuestas_dia WHERE dia < ?1").bind(hoy),
  ]);
}

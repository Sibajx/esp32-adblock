import { contacto } from "./contacto";
import { resumenDueno } from "./dueno";
import { estadisticas } from "./estadisticas";
import { error, leerJson, origenValido } from "./http";
import { limpiar } from "./limpieza";
import { avisoPlaca, paso } from "./placas";
import { pais, reporteBloqueo, reporteModem } from "./reportes";
import type { Deps, Env, Manejador } from "./tipos";

const MIN_PEPPER = 16;

interface Ruta {
  metodo: "GET" | "POST";
  /** Tope del cuerpo en bytes (solo POST). */
  tope: number;
  manejar: Manejador;
}

const RUTAS: Record<string, Ruta> = {
  "/api/v1/reporte/modem": { metodo: "POST", tope: 1024, manejar: reporteModem },
  "/api/v1/reporte/bloqueo": { metodo: "POST", tope: 1024, manejar: reporteBloqueo },
  "/api/v1/placa": { metodo: "POST", tope: 256, manejar: avisoPlaca },
  "/api/v1/paso": { metodo: "POST", tope: 128, manejar: paso },
  "/api/v1/contacto": { metodo: "POST", tope: 8192, manejar: contacto },
  "/api/v1/pais": { metodo: "GET", tope: 0, manejar: pais },
  "/api/v1/estadisticas": { metodo: "GET", tope: 0, manejar: estadisticas },
  "/api/v1/dueno/resumen": { metodo: "GET", tope: 0, manejar: resumenDueno },
};

export async function manejar(req: Request, env: Env, ctx: ExecutionContext, deps: Deps): Promise<Response> {
  try {
    const camino = new URL(req.url).pathname;
    const ruta = Object.hasOwn(RUTAS, camino) ? RUTAS[camino] : undefined;
    if (!ruta) return error(404, "no_existe");
    if (req.method !== ruta.metodo) return error(405, "metodo");
    if (ruta.metodo === "GET") return await ruta.manejar(req, env, deps, undefined, ctx);
    if (!origenValido(req, env)) return error(403, "origen");
    // Sin pimienta, los números guardados y las claves de límite serían fáciles de adivinar: no se sigue.
    if (typeof env.ID_PEPPER !== "string" || env.ID_PEPPER.length < MIN_PEPPER) return error(500, "interno");
    const cuerpo = await leerJson(req, ruta.tope);
    if (cuerpo instanceof Response) return cuerpo;
    return await ruta.manejar(req, env, deps, cuerpo, ctx);
  } catch {
    return error(500, "interno");
  }
}

export default {
  fetch: (req, env, ctx) =>
    manejar(req, env, ctx, { fetch: (i, o) => fetch(i, o), ahora: Date.now, cache: caches.default }),
  scheduled: async (control, env, ctx) => {
    ctx.waitUntil(limpiar(env, control.scheduledTime));
  },
} satisfies ExportedHandler<Env>;

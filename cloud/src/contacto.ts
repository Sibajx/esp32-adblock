import { correoAlDueno, respuestaAutomatica, type Contacto } from "./correo";
import { claveIp, dia, error, json, sha256Hex } from "./http";
import { enLista, TEMAS } from "./opciones";
import { verificarTurnstile } from "./turnstile";
import type { Env, Manejador } from "./tipos";
import { dentroDelLimite, esCorreo } from "./validar";

export const TOPE_DIARIO = 200;
/** Respuestas automáticas al día (aparte del tope de mensajes al dueño). */
export const TOPE_RESPUESTAS = 50;
const CONTROLES = /[\u0000-\u001f\u007f]/;
const largo = (s: string) => [...s].length;

/** Mismas reglas que `validarContacto` de la página. */
export function validarContacto(c: Record<string, unknown>): Contacto | null {
  const { nombre, correo, tema, mensaje, diagnostico } = c;
  if (nombre !== undefined && (typeof nombre !== "string" || largo(nombre) > 80 || CONTROLES.test(nombre))) return null;
  if (!esCorreo(correo) || !enLista(TEMAS, tema)) return null;
  if (typeof mensaje !== "string" || largo(mensaje) < 10 || largo(mensaje) > 2000) return null;
  if (diagnostico !== undefined && (typeof diagnostico !== "string" || largo(diagnostico) > 1500)) return null;
  return {
    nombre: nombre?.trim() || undefined,
    correo,
    tema,
    mensaje,
    diagnostico: diagnostico || undefined,
  };
}

export const contacto: Manejador = async (req, env, deps, c = {}) => {
  if (!(await dentroDelLimite(env.LIMITE_CONTACTO, await claveIp(req, env, "contacto")))) {
    return error(429, "limite");
  }
  const datos = validarContacto(c);
  if (!datos) return error(400, "datos");
  if (!(await verificarTurnstile(c.turnstile, env, deps))) return error(403, "turnstile");

  const hoy = dia(deps.ahora());
  // Reserva un lugar del tope antes de mandar, en un solo paso: dos pedidos a la vez no pasan del tope.
  const lugar = await env.DB.prepare(
    `INSERT INTO contacto_dia (dia, n) VALUES (?1, 1)
     ON CONFLICT(dia) DO UPDATE SET n = n + 1 WHERE n < ?2 RETURNING n`,
  ).bind(hoy, TOPE_DIARIO).first<{ n: number }>();
  if (!lugar) return error(429, "limite");

  try {
    await env.CORREO.send(correoAlDueno(datos, env));
  } catch {
    // No se mandó: se devuelve el lugar reservado.
    await env.DB.batch([
      env.DB.prepare("UPDATE contacto_dia SET n = n - 1 WHERE dia = ?1").bind(hoy),
      env.DB.prepare("DELETE FROM contacto_dia WHERE dia = ?1 AND n <= 0").bind(hoy),
    ]);
    return error(500, "interno");
  }
  await env.DB.prepare(
    "INSERT INTO temas (dia, tema, n) VALUES (?1, ?2, 1) ON CONFLICT(dia, tema) DO UPDATE SET n = n + 1",
  ).bind(hoy, datos.tema).run();
  if (env.RESPUESTA_AUTOMATICA === "1" && (await reservarRespuesta(env, hoy, datos.correo))) {
    try {
      await env.CORREO.send(respuestaAutomatica(datos.correo, env));
    } catch {
      // La persona ya escribió y el dueño lo recibió: no se le muestra un error.
    }
  }
  return json(200, { ok: true });
};

/**
 * Una respuesta automática por dirección y día, y no más de TOPE_RESPUESTAS al día en total:
 * así el formulario no sirve para llenar de correos a otra persona. Se guarda solo un resumen
 * revuelto de la dirección, y se borra al día siguiente.
 */
export async function reservarRespuesta(env: Env, hoy: number, correo: string): Promise<boolean> {
  const destino = (await sha256Hex(`${env.ID_PEPPER}\u0000correo\u0000${correo.toLowerCase()}`)).slice(0, 32);
  const r = await env.DB.prepare(
    `INSERT INTO respuestas_dia (dia, destino)
     SELECT ?1, ?2 WHERE (SELECT COUNT(*) FROM respuestas_dia WHERE dia = ?1) < ?3
     ON CONFLICT(dia, destino) DO NOTHING`,
  ).bind(hoy, destino, TOPE_RESPUESTAS).run();
  return (r.meta.changes ?? 0) === 1;
}

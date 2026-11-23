import { verificarAccess } from "./access";
import { dia, error, json } from "./http";
import { MOTIVOS, PASOS, TEMAS } from "./opciones";
import type { Manejador } from "./tipos";

const DIAS = 30;
const MAX_BLOQUEOS = 50;
const DIA_MS = 86400000;

function enOrden<K extends string>(lista: readonly string[], clave: K, filas: { k: string; n: number }[]) {
  const m = new Map(filas.map((f) => [f.k, f.n]));
  return lista.map((v) => ({ [clave]: v, n: m.get(v) ?? 0 }) as Record<K, string> & { n: number });
}

/** Resumen para el dueño. Además de Access en el borde, aquí se revisa el JWT. */
export const resumenDueno: Manejador = async (req, env, deps) => {
  if (!(await verificarAccess(req, env, deps))) return error(403, "acceso");
  const desde = dia(deps.ahora()) - (DIAS - 1);
  const [pasos, temas, motivos, bloqueos, guias, placas, aprobados] = await env.DB.batch([
    env.DB.prepare("SELECT paso AS k, SUM(n) AS n FROM pasos WHERE dia >= ?1 GROUP BY paso").bind(desde),
    env.DB.prepare("SELECT tema AS k, SUM(n) AS n FROM temas WHERE dia >= ?1 GROUP BY tema").bind(desde),
    env.DB.prepare("SELECT motivo AS k, COUNT(*) AS n FROM reportes_bloqueo WHERE dia >= ?1 GROUP BY motivo").bind(desde),
    // `cubierto`: el permitido que ya lo deja pasar (el mismo sitio o uno de sus padres, como en
    // las placas); si hay varios, el más largo.
    env.DB.prepare(
      `SELECT r.dominio AS dominio, COUNT(*) AS n,
         (SELECT p.dominio FROM permitidos p
          WHERE p.dominio = r.dominio OR substr(r.dominio, -length(p.dominio) - 1) = '.' || p.dominio
          ORDER BY length(p.dominio) DESC LIMIT 1) AS cubierto
       FROM reportes_bloqueo r
       GROUP BY r.dominio ORDER BY n DESC, r.dominio LIMIT ?1`,
    ).bind(MAX_BLOQUEOS),
    // "Guías que faltan": quien no encontró el menú, no sabe su fabricante o no logró cambiar el DNS.
    env.DB.prepare(
      `SELECT fabricante, COUNT(*) AS n FROM reportes_modem
       WHERE menu = 'otro' OR fabricante = 'no-se' OR dns_ok = 0
       GROUP BY fabricante ORDER BY n DESC, fabricante`,
    ),
    env.DB.prepare("SELECT COUNT(*) AS n FROM placas"),
    env.DB.prepare("SELECT dominio, aprobado_en FROM permitidos ORDER BY dominio"),
  ]);
  return json(200, {
    embudo: enOrden(PASOS, "paso", pasos.results as { k: string; n: number }[]),
    temas: enOrden(TEMAS, "tema", temas.results as { k: string; n: number }[]),
    motivos: enOrden(MOTIVOS, "motivo", motivos.results as { k: string; n: number }[]),
    // aprobado: ya pasa en las placas. cubiertoPor: solo si lo deja pasar un padre aprobado.
    bloqueos: (bloqueos.results as { dominio: string; n: number; cubierto: string | null }[]).map((f) => ({
      dominio: f.dominio,
      n: f.n,
      aprobado: f.cubierto !== null,
      ...(f.cubierto !== null && f.cubierto !== f.dominio ? { cubiertoPor: f.cubierto } : {}),
    })),
    guiasFaltan: guias.results,
    placasTotal: (placas.results[0] as { n: number } | undefined)?.n ?? 0,
    // La lista de permitidos de la comunidad, con el día en que se aprobó cada sitio.
    permitidos: (aprobados.results as { dominio: string; aprobado_en: number }[]).map((f) => ({
      dominio: f.dominio,
      desde: new Date(f.aprobado_en * DIA_MS).toISOString().slice(0, 10),
    })),
  });
};

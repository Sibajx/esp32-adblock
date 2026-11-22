import type { Env } from "./tipos";

const DIA_MS = 86400000;

export function json(status: number, cuerpo: unknown, cache = "no-store"): Response {
  return new Response(status === 204 ? null : JSON.stringify(cuerpo), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "X-Content-Type-Options": "nosniff",
      "Cache-Control": cache,
    },
  });
}

export function error(status: number, codigo: string): Response {
  return json(status, { error: codigo });
}

/**
 * Lee el cuerpo como objeto JSON sin pasar de `max` bytes (no confía en Content-Length).
 * Devuelve el objeto o una respuesta de error lista: 413 si es grande, 400 si no es un objeto.
 */
export async function leerJson(req: Request, max: number): Promise<Record<string, unknown> | Response> {
  const largo = Number(req.headers.get("Content-Length"));
  if (Number.isFinite(largo) && largo > max) return error(413, "grande");
  if (!req.body) return error(400, "datos");
  const lector = req.body.getReader();
  const partes: Uint8Array[] = [];
  let total = 0;
  for (;;) {
    const { done, value } = await lector.read();
    if (done) break;
    total += value.byteLength;
    if (total > max) {
      await lector.cancel().catch(() => {});
      return error(413, "grande");
    }
    partes.push(value);
  }
  const bytes = new Uint8Array(total);
  let pos = 0;
  for (const p of partes) {
    bytes.set(p, pos);
    pos += p.byteLength;
  }
  let valor: unknown;
  try {
    valor = JSON.parse(new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(bytes));
  } catch {
    return error(400, "datos");
  }
  if (typeof valor !== "object" || valor === null || Array.isArray(valor)) return error(400, "datos");
  return valor as Record<string, unknown>;
}

/** Sin Origin (la placa, curl) → vale; con Origin → solo el de la página. */
export function origenValido(req: Request, env: Env): boolean {
  const origen = req.headers.get("Origin");
  return origen === null || origen === env.ORIGEN;
}

/** Días desde 1970 en UTC. */
export function dia(ms: number): number {
  return Math.floor(ms / DIA_MS);
}

export async function sha256Hex(texto: string): Promise<string> {
  const h = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(texto));
  return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/**
 * Parte de la IP que cuenta para el límite. En IPv6 una casa recibe un /64 entero y puede
 * cambiar de dirección en cada petición: se usa solo el /64. IPv4 queda igual.
 */
export function redDeIp(ip: string): string {
  if (!ip.includes(":")) return ip;
  const s = ip.toLowerCase().split("%")[0];
  // IPv4 dentro de IPv6 (::ffff:1.2.3.4): cuenta como esa IPv4.
  if (s.includes(".")) return s.slice(s.lastIndexOf(":") + 1);
  const [a, b] = s.split("::");
  const izq = a ? a.split(":") : [];
  const der = b !== undefined && b !== "" ? b.split(":") : [];
  const faltan = b !== undefined ? 8 - izq.length - der.length : 0;
  const grupos = [...izq, ...Array(Math.max(faltan, 0)).fill("0"), ...der];
  return grupos.slice(0, 4).map((g) => (g.replace(/^0+/, "") || "0")).join(":") + "::/64";
}

/** Clave para el límite por IP (o por /64 en IPv6): pasa por SHA-256 con la pimienta y nunca se guarda. */
export async function claveIp(req: Request, env: Env, ruta: string): Promise<string> {
  const ip = redDeIp(req.headers.get("CF-Connecting-IP") ?? "");
  return `${ruta}:${(await sha256Hex(`${env.ID_PEPPER}\u0000${ip}`)).slice(0, 32)}`;
}

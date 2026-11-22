import type { Deps, Env } from "./tipos";

const HOLGURA_S = 60;
const ALG = { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" } as const;

function deB64url(s: string): Uint8Array {
  if (!/^[A-Za-z0-9_-]*$/.test(s)) throw new Error("base64url");
  const b = atob(s.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - (s.length % 4)) % 4));
  return Uint8Array.from(b, (c) => c.charCodeAt(0));
}

function deJson(s: string): Record<string, unknown> {
  const v: unknown = JSON.parse(new TextDecoder().decode(deB64url(s)));
  if (typeof v !== "object" || v === null || Array.isArray(v)) throw new Error("json");
  return v as Record<string, unknown>;
}

/**
 * Revisa el JWT que pone Cloudflare Access (`Cf-Access-Jwt-Assertion`): RS256 con las llaves del equipo,
 * `aud`, `iss`, `exp` y `nbf`. Si falta la configuración o algo no cuadra → false.
 */
export async function verificarAccess(req: Request, env: Env, deps: Deps): Promise<boolean> {
  const equipo = env.ACCESS_TEAM_DOMAIN;
  if (!env.ACCESS_AUD || !/^https:\/\/[a-z0-9.-]+$/.test(equipo)) return false;
  const jwt = req.headers.get("Cf-Access-Jwt-Assertion");
  if (!jwt || jwt.length > 8192) return false;
  try {
    const partes = jwt.split(".");
    if (partes.length !== 3) return false;
    const [h, p, f] = partes;
    const cabecera = deJson(h);
    const datos = deJson(p);
    if (cabecera.alg !== "RS256" || typeof cabecera.kid !== "string") return false;

    const ahora = deps.ahora() / 1000;
    const aud = Array.isArray(datos.aud) ? datos.aud : [datos.aud];
    if (!aud.includes(env.ACCESS_AUD) || datos.iss !== equipo) return false;
    if (typeof datos.exp !== "number" || datos.exp + HOLGURA_S <= ahora) return false;
    if (datos.nbf !== undefined && (typeof datos.nbf !== "number" || datos.nbf - HOLGURA_S > ahora)) return false;

    const r = await deps.fetch(`${equipo}/cdn-cgi/access/certs`);
    if (!r.ok) return false;
    const { keys } = (await r.json()) as { keys?: unknown };
    if (!Array.isArray(keys)) return false;
    const jwk = keys.find(
      (k): k is JsonWebKey & { kid: string } =>
        typeof k === "object" && k !== null && (k as { kid?: unknown }).kid === cabecera.kid && (k as JsonWebKey).kty === "RSA",
    );
    if (!jwk) return false;
    const llave = await crypto.subtle.importKey(
      "jwk",
      { kty: "RSA", n: jwk.n, e: jwk.e, alg: "RS256", ext: true },
      ALG,
      false,
      ["verify"],
    );
    return await crypto.subtle.verify(ALG, llave, deB64url(f), new TextEncoder().encode(`${h}.${p}`));
  } catch {
    return false;
  }
}

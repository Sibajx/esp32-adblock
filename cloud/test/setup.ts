// Se corre antes de cada archivo de prueba: crea las tablas y deja ayudas comunes.
import { applyD1Migrations, createExecutionContext, env, type D1Migration } from "cloudflare:test";
import type { Deps, Env } from "../src/tipos";

declare global {
  namespace Cloudflare {
    interface Env {
      DB: D1Database;
      ID_PEPPER: string;
      TURNSTILE_SECRET: string;
      ORIGEN: string;
      HOST_PAGINA: string;
      CONTACTO_DESTINO: string;
      CONTACTO_REMITENTE: string;
      RESPUESTA_AUTOMATICA: string;
      ACCESS_TEAM_DOMAIN: string;
      ACCESS_AUD: string;
      TEST_MIGRATIONS: D1Migration[];
    }
  }
}

await applyD1Migrations(env.DB, env.TEST_MIGRATIONS);

export const TABLAS = [
  "reportes_modem",
  "reportes_bloqueo",
  "placas",
  "bloqueadas",
  "pasos",
  "temas",
  "contacto_dia",
  "respuestas_dia",
  "permitidos",
  "permitidos_version",
] as const;

export async function vaciarTablas(): Promise<void> {
  await env.DB.batch(TABLAS.map((t) => env.DB.prepare(`DELETE FROM ${t}`)));
}

/** 2026-11-21 12:00 UTC (sábado). */
export const AHORA = Date.UTC(2026, 10, 21, 12, 0, 0);
export const HOY = Math.floor(AHORA / 86400000);

export interface LimiteFalso extends RateLimit {
  claves: string[];
}
export function limiteFalso(deja = true): LimiteFalso {
  const claves: string[] = [];
  return {
    claves,
    async limit({ key }: RateLimitOptions) {
      claves.push(key);
      return { success: deja };
    },
  };
}

export interface CorreoFalso extends SendEmail {
  mandados: EmailMessageBuilder[];
}
export function correoFalso(falla: (n: number) => boolean = () => false): CorreoFalso {
  const mandados: EmailMessageBuilder[] = [];
  const send = async (m: EmailMessage | EmailMessageBuilder): Promise<EmailSendResult> => {
    if (falla(mandados.length)) throw new Error("no se pudo mandar");
    mandados.push(m as EmailMessageBuilder);
    return { messageId: `prueba-${mandados.length}` };
  };
  return { mandados, send } as CorreoFalso;
}

export function envPrueba(cambios: Partial<Env> = {}): Env {
  return {
    DB: env.DB,
    CORREO: correoFalso(),
    LIMITE: limiteFalso(true),
    LIMITE_CONTACTO: limiteFalso(true),
    ID_PEPPER: env.ID_PEPPER,
    TURNSTILE_SECRET: env.TURNSTILE_SECRET,
    ORIGEN: env.ORIGEN,
    HOST_PAGINA: env.HOST_PAGINA,
    CONTACTO_DESTINO: env.CONTACTO_DESTINO,
    CONTACTO_REMITENTE: env.CONTACTO_REMITENTE,
    RESPUESTA_AUTOMATICA: env.RESPUESTA_AUTOMATICA,
    ACCESS_TEAM_DOMAIN: env.ACCESS_TEAM_DOMAIN,
    ACCESS_AUD: env.ACCESS_AUD,
    ...cambios,
  };
}

/** fetch de mentira: falla la prueba si el código sale a internet sin que se espere. */
export const fetchProhibido: typeof fetch = async () => {
  throw new Error("salida a internet no esperada");
};

export function depsPrueba(cambios: Partial<Deps> = {}): Deps {
  return { fetch: fetchProhibido, ahora: () => AHORA, ...cambios };
}

/** Respuesta de Turnstile de mentira. */
export function fetchTurnstile(success: boolean, hostname = "siblock.s-dev.win"): typeof fetch {
  return async () => Response.json({ success, hostname });
}

export interface Pedido {
  metodo?: string;
  cuerpo?: unknown;
  crudo?: string;
  headers?: Record<string, string>;
  cf?: Record<string, unknown>;
}
export function pedido(ruta: string, p: Pedido = {}): Request {
  const metodo = p.metodo ?? (p.cuerpo !== undefined || p.crudo !== undefined ? "POST" : "GET");
  const headers: Record<string, string> = { "CF-Connecting-IP": "203.0.113.7", ...(p.headers ?? {}) };
  const body = p.crudo ?? (p.cuerpo !== undefined ? JSON.stringify(p.cuerpo) : undefined);
  if (body !== undefined) headers["Content-Type"] ??= "application/json";
  return new Request(`https://siblock.s-dev.win${ruta}`, {
    method: metodo,
    headers,
    body,
    cf: p.cf ?? {},
  } as RequestInit);
}

export function ctxPrueba(): ExecutionContext {
  return createExecutionContext();
}

export const ID = "0123456789abcdef0123456789abcdef";
export const ID2 = "fedcba9876543210fedcba9876543210";

// ---- Cloudflare Access de mentira (par RSA hecho en la prueba) ----

export const EQUIPO = "https://equipo-de-prueba.cloudflareaccess.com";
export const AUD = "aud-de-prueba-123";

const b64url = (b: ArrayBuffer | Uint8Array | string) => {
  const bytes = typeof b === "string" ? new TextEncoder().encode(b) : new Uint8Array(b as ArrayBuffer);
  let s = "";
  for (const x of bytes) s += String.fromCharCode(x);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
};

const ALG = { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" };

export interface Llaves { buena: CryptoKeyPair; otra: CryptoKeyPair; jwks: unknown }
export async function crearLlaves(): Promise<Llaves> {
  const buena = (await crypto.subtle.generateKey(ALG, true, ["sign", "verify"])) as CryptoKeyPair;
  const otra = (await crypto.subtle.generateKey(ALG, true, ["sign", "verify"])) as CryptoKeyPair;
  const pub = (await crypto.subtle.exportKey("jwk", buena.publicKey)) as JsonWebKey;
  return { buena, otra, jwks: { keys: [{ ...pub, kid: "llave-1", alg: "RS256", use: "sig" }] } };
}

export async function firmar(
  privada: CryptoKey,
  cambios: Record<string, unknown> = {},
  cabecera: Record<string, unknown> = { alg: "RS256", kid: "llave-1", typ: "JWT" },
): Promise<string> {
  const s = Math.floor(AHORA / 1000);
  const cuerpo = { aud: [AUD], iss: EQUIPO, exp: s + 3600, nbf: s - 10, iat: s - 10, ...cambios };
  const datos = `${b64url(JSON.stringify(cabecera))}.${b64url(JSON.stringify(cuerpo))}`;
  const firma = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", privada, new TextEncoder().encode(datos));
  return `${datos}.${b64url(firma)}`;
}

export function envAccess(cambios: Partial<Env> = {}): Env {
  return envPrueba({ ACCESS_TEAM_DOMAIN: EQUIPO, ACCESS_AUD: AUD, ...cambios });
}

export function depsAccess(jwks: unknown, urls: string[] = []): Deps {
  return depsPrueba({
    fetch: async (i) => {
      urls.push(String(i));
      return Response.json(jwks);
    },
  });
}


export interface Env {
  DB: D1Database;
  CORREO: SendEmail;
  LIMITE: RateLimit;
  LIMITE_CONTACTO: RateLimit;
  ID_PEPPER: string;
  TURNSTILE_SECRET: string;
  ORIGEN: string;
  HOST_PAGINA: string;
  CONTACTO_DESTINO: string;
  CONTACTO_REMITENTE: string;
  RESPUESTA_AUTOMATICA: string;
  ACCESS_TEAM_DOMAIN: string;
  ACCESS_AUD: string;
}

/** Todo lo que sale a internet y el reloj entran por aquí para poder probarlos. */
export interface Deps {
  fetch: typeof fetch;
  /** Milisegundos desde 1970. */
  ahora: () => number;
  /** Caché del borde (caches.default). Sin ella, cada pedido consulta D1 (pruebas). */
  cache?: Cache;
}

/** Manejador de una ruta: recibe el cuerpo ya leído (POST) o `undefined` (GET). */
export type Manejador = (
  req: Request,
  env: Env,
  deps: Deps,
  cuerpo: Record<string, unknown> | undefined,
  ctx: ExecutionContext,
) => Promise<Response>;

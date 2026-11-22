import type { Deps, Env } from "./tipos";

const URL_TURNSTILE = "https://challenges.cloudflare.com/turnstile/v0/siteverify";

/** true solo si Cloudflare dice que el token es bueno y es de nuestra página. */
export async function verificarTurnstile(token: unknown, env: Env, deps: Deps): Promise<boolean> {
  if (typeof token !== "string" || token.length === 0 || token.length > 2048) return false;
  if (!env.TURNSTILE_SECRET) return false;
  try {
    const r = await deps.fetch(URL_TURNSTILE, {
      method: "POST",
      body: new URLSearchParams({ secret: env.TURNSTILE_SECRET, response: token }),
    });
    const datos = (await r.json()) as { success?: unknown; hostname?: unknown };
    return datos.success === true && datos.hostname === env.HOST_PAGINA;
  } catch {
    return false;
  }
}

import { describe, expect, it } from "vitest";
import { verificarTurnstile } from "../src/turnstile";
import { depsPrueba, envPrueba, fetchTurnstile } from "./setup";

describe("verificarTurnstile", () => {
  const env = envPrueba();

  it("success y hostname bien → true, con el secreto y el token en el formulario", async () => {
    let url = "";
    let cuerpo = "";
    const deps = depsPrueba({
      fetch: async (i, o) => {
        url = String(i);
        cuerpo = String(o?.body);
        expect(o?.method).toBe("POST");
        return Response.json({ success: true, hostname: "siblock.s-dev.win" });
      },
    });
    expect(await verificarTurnstile("tok-123", env, deps)).toBe(true);
    expect(url).toBe("https://challenges.cloudflare.com/turnstile/v0/siteverify");
    const f = new URLSearchParams(cuerpo);
    expect(f.get("secret")).toBe("secreto-de-prueba");
    expect(f.get("response")).toBe("tok-123");
    expect(f.has("remoteip")).toBe(false);
  });

  it("hostname de otro sitio → false", async () => {
    expect(await verificarTurnstile("t", env, depsPrueba({ fetch: fetchTurnstile(true, "otro.com") }))).toBe(false);
  });

  it("success:false → false", async () => {
    expect(await verificarTurnstile("t", env, depsPrueba({ fetch: fetchTurnstile(false) }))).toBe(false);
  });

  it('success "true" (texto) → false', async () => {
    const deps = depsPrueba({ fetch: async () => Response.json({ success: "true", hostname: "siblock.s-dev.win" }) });
    expect(await verificarTurnstile("t", env, deps)).toBe(false);
  });

  it("fetch que lanza o respuesta rota → false", async () => {
    expect(await verificarTurnstile("t", env, depsPrueba({ fetch: async () => { throw new Error("red"); } }))).toBe(false);
    expect(await verificarTurnstile("t", env, depsPrueba({ fetch: async () => new Response("<html>") }))).toBe(false);
  });

  it("token de 3000, vacío o que no es texto → false sin llamar", async () => {
    let llamadas = 0;
    const deps = depsPrueba({ fetch: async () => { llamadas++; return Response.json({ success: true, hostname: "siblock.s-dev.win" }); } });
    for (const t of ["x".repeat(3000), "", undefined, null, 5, { a: 1 }]) {
      expect(await verificarTurnstile(t, env, deps)).toBe(false);
    }
    expect(llamadas).toBe(0);
  });

  it("sin secreto configurado → false sin llamar", async () => {
    let llamadas = 0;
    const deps = depsPrueba({ fetch: async () => { llamadas++; return Response.json({ success: true, hostname: "siblock.s-dev.win" }); } });
    expect(await verificarTurnstile("t", envPrueba({ TURNSTILE_SECRET: "" }), deps)).toBe(false);
    expect(llamadas).toBe(0);
  });
});

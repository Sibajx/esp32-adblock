import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { manejar } from "../src/index";
import type { Deps, Env } from "../src/tipos";
import { hashId } from "../src/validar";
import {
  ctxPrueba, depsPrueba, envPrueba, fetchTurnstile, HOY, ID, ID2, limiteFalso, pedido, vaciarTablas,
} from "./setup";

beforeEach(vaciarTablas);

const CF = { country: "MX", asn: 64500, asOrganization: "Ejemplo Telecom" };
const modem = (cambios: Record<string, unknown> = {}) => ({
  i: ID, origen: "panel", fabricante: "huawei", dns_ok: true, menu: "internet_wan", ...cambios,
});

async function mandar(cuerpo: unknown, e: Env = envPrueba(), d: Deps = depsPrueba(), cf: Record<string, unknown> = CF) {
  return manejar(pedido("/api/v1/reporte/modem", { cuerpo, cf }), e, ctxPrueba(), d);
}
async function filas() {
  return (await env.DB.prepare("SELECT * FROM reportes_modem").all<Record<string, unknown>>()).results;
}

describe("POST /api/v1/reporte/modem", () => {
  it("reporte de panel válido → 200 y una fila con el número cifrado", async () => {
    const r = await mandar(modem());
    expect(r.status).toBe(200);
    expect(await r.json()).toEqual({ ok: true });
    const f = await filas();
    expect(f).toHaveLength(1);
    expect(f[0]).toEqual({
      instalacion: await hashId(ID, "pimienta-de-prueba"),
      origen: "panel", pais: "MX", asn: 64500, proveedor: "Ejemplo Telecom",
      fabricante: "huawei", dns_ok: 1, menu: "internet_wan", dia: HOY,
    });
    expect(f[0].instalacion).not.toBe(ID);
  });

  it("mismo número dos veces → una sola fila con lo último", async () => {
    expect((await mandar(modem())).status).toBe(200);
    expect((await mandar(modem({ menu: "avanzado", dns_ok: false }))).status).toBe(200);
    const f = await filas();
    expect(f).toHaveLength(1);
    expect(f[0].menu).toBe("avanzado");
    expect(f[0].dns_ok).toBe(0);
    expect((await mandar(modem({ i: ID2 }))).status).toBe(200);
    expect(await filas()).toHaveLength(2);
  });

  it("desde la página: sin token → 403, token malo → 403, token bueno → 200", async () => {
    const sin = await mandar(modem({ origen: "pagina" }));
    expect(sin.status).toBe(403);
    expect(await sin.json()).toEqual({ error: "turnstile" });
    const malo = await mandar(modem({ origen: "pagina", turnstile: "t" }), envPrueba(), depsPrueba({ fetch: fetchTurnstile(false) }));
    expect(malo.status).toBe(403);
    expect(await filas()).toHaveLength(0);
    const bueno = await mandar(modem({ origen: "pagina", turnstile: "t" }), envPrueba(), depsPrueba({ fetch: fetchTurnstile(true) }));
    expect(bueno.status).toBe(200);
    expect((await filas())[0].origen).toBe("pagina");
  });

  it("datos malos → 400 y nada guardado", async () => {
    for (const malo of [
      modem({ fabricante: "linksys" }), modem({ dns_ok: "sí" }), modem({ dns_ok: 1 }), modem({ i: ID.slice(2) }),
      modem({ menu: "" }), modem({ menu: "Internet" }), modem({ origen: "otro" }), modem({ i: undefined }),
      modem({ fabricante: ["huawei"] }),
    ]) {
      const r = await mandar(malo);
      expect(r.status, JSON.stringify(malo)).toBe(400);
      expect(await r.json()).toEqual({ error: "datos" });
    }
    expect(await filas()).toHaveLength(0);
  });

  it("clave extra ip → se ignora y no se guarda", async () => {
    expect((await mandar(modem({ ip: "192.168.1.1" }))).status).toBe(200);
    expect(JSON.stringify(await filas())).not.toContain("192.168.1.1");
  });

  it("el límite dice que no → 429 sin guardar; la clave no lleva la IP", async () => {
    const lim = limiteFalso(false);
    const r = await mandar(modem(), envPrueba({ LIMITE: lim }));
    expect(r.status).toBe(429);
    expect(await r.json()).toEqual({ error: "limite" });
    expect(await filas()).toHaveLength(0);
    expect(lim.claves[0]).toMatch(/^reporte:[0-9a-f]{32}$/);
  });

  it("ninguna columna guarda la IP", async () => {
    await mandar(modem());
    for (const tabla of ["reportes_modem", "reportes_bloqueo", "placas", "bloqueadas", "pasos", "temas", "contacto_dia"]) {
      const { results } = await env.DB.prepare(`PRAGMA table_info(${tabla})`).all<{ name: string }>();
      for (const c of results) expect(c.name, `${tabla}.${c.name}`).not.toMatch(/ip|mac|correo|nombre|mensaje/i);
    }
    expect(JSON.stringify(await filas())).not.toContain("203.0.113.7");
  });

  it("sin datos de país ni proveedor → vacíos; proveedor largo se corta a 100", async () => {
    expect((await mandar(modem(), envPrueba(), depsPrueba(), {})).status).toBe(200);
    let f = (await filas())[0];
    expect(f.pais).toBe("");
    expect(f.proveedor).toBe("");
    expect(f.asn).toBe(null);
    await mandar(modem(), envPrueba(), depsPrueba(), { country: "MX", asOrganization: "x".repeat(300) + "\n", asn: "no" });
    f = (await filas())[0];
    expect(f.proveedor).toBe("x".repeat(100));
    expect(f.asn).toBe(null);
  });
});

describe("GET /api/v1/pais", () => {
  it("devuelve país y proveedor sin guardar nada", async () => {
    const r = await manejar(pedido("/api/v1/pais", { cf: { country: "MX", asOrganization: "Ejemplo" } }), envPrueba(), ctxPrueba(), depsPrueba());
    expect(r.status).toBe(200);
    expect(r.headers.get("Cache-Control")).toBe("no-store");
    expect(await r.json()).toEqual({ pais: "MX", proveedor: "Ejemplo" });
    expect(await filas()).toHaveLength(0);
  });

  it("sin cf → vacíos", async () => {
    const r = await manejar(pedido("/api/v1/pais", { cf: {} }), envPrueba(), ctxPrueba(), depsPrueba());
    expect(await r.json()).toEqual({ pais: "", proveedor: "" });
  });
});

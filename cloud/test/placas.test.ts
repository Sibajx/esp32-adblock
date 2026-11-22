import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { manejar } from "../src/index";
import type { Deps, Env } from "../src/tipos";
import { hashId } from "../src/validar";
import {
  AHORA, ctxPrueba, depsPrueba, envPrueba, fetchTurnstile, HOY, ID, ID2, limiteFalso, pedido, vaciarTablas,
} from "./setup";

beforeEach(vaciarTablas);

const CF = { country: "MX", asn: 64500, asOrganization: "Ejemplo" };
const ir = (ruta: string, cuerpo: unknown, e: Env = envPrueba(), d: Deps = depsPrueba()) =>
  manejar(pedido(ruta, { cuerpo, cf: CF }), e, ctxPrueba(), d);
const todas = async (tabla: string) =>
  (await env.DB.prepare(`SELECT * FROM ${tabla}`).all<Record<string, unknown>>()).results;

describe("POST /api/v1/reporte/bloqueo", () => {
  const bloqueo = (c: Record<string, unknown> = {}) => ({
    i: ID, origen: "panel", dominio: "ads.ejemplo.com", motivo: "pagina_no_abre", ...c,
  });

  it("válido → 200 y una fila con el número cifrado", async () => {
    const r = await ir("/api/v1/reporte/bloqueo", bloqueo());
    expect(r.status).toBe(200);
    expect(await r.json()).toEqual({ ok: true });
    expect(await todas("reportes_bloqueo")).toEqual([
      { instalacion: await hashId(ID, "pimienta-de-prueba"), dominio: "ads.ejemplo.com", motivo: "pagina_no_abre", origen: "panel", dia: HOY },
    ]);
  });

  it("dominio en mayúsculas o malo → 400", async () => {
    for (const dominio of ["ADS.ejemplo.com", "ejemplo", "a..com", "<b>.com", 5]) {
      const r = await ir("/api/v1/reporte/bloqueo", bloqueo({ dominio }));
      expect(r.status, String(dominio)).toBe(400);
    }
    for (const c of [{ motivo: "spam" }, { motivo: "" }, { i: "corto" }, { origen: "placa" }]) {
      expect((await ir("/api/v1/reporte/bloqueo", bloqueo(c))).status, JSON.stringify(c)).toBe(400);
    }
    expect(await todas("reportes_bloqueo")).toHaveLength(0);
  });

  it("mismo número y dominio dos veces → 1 fila; dos números → 2", async () => {
    await ir("/api/v1/reporte/bloqueo", bloqueo());
    await ir("/api/v1/reporte/bloqueo", bloqueo({ motivo: "otro" }));
    let f = await todas("reportes_bloqueo");
    expect(f).toHaveLength(1);
    expect(f[0].motivo).toBe("otro");
    await ir("/api/v1/reporte/bloqueo", bloqueo({ i: ID2 }));
    f = await todas("reportes_bloqueo");
    expect(f).toHaveLength(2);
    await ir("/api/v1/reporte/bloqueo", bloqueo({ dominio: "otro.ejemplo.com" }));
    expect(await todas("reportes_bloqueo")).toHaveLength(3);
  });

  it("desde la página necesita Turnstile", async () => {
    expect((await ir("/api/v1/reporte/bloqueo", bloqueo({ origen: "pagina" }))).status).toBe(403);
    const ok = await ir("/api/v1/reporte/bloqueo", bloqueo({ origen: "pagina", turnstile: "t" }), envPrueba(), depsPrueba({ fetch: fetchTurnstile(true) }));
    expect(ok.status).toBe(200);
  });

  it("límite → 429", async () => {
    const lim = limiteFalso(false);
    expect((await ir("/api/v1/reporte/bloqueo", bloqueo(), envPrueba({ LIMITE: lim }))).status).toBe(429);
    expect(lim.claves[0]).toMatch(/^reporte:/);
  });
});

describe("POST /api/v1/placa", () => {
  it("el último aviso del día gana; una fila en placas", async () => {
    expect((await ir("/api/v1/placa", { i: ID, v: "v1.2.3", b: 10 })).status).toBe(200);
    const r = await ir("/api/v1/placa", { i: ID, v: "v1.2.3", b: 12 });
    expect(r.status).toBe(200);
    expect(await r.json()).toEqual({ ok: true });
    const h = await hashId(ID, "pimienta-de-prueba");
    expect(await todas("placas")).toEqual([{ instalacion: h, dia: HOY, version: "v1.2.3", pais: "MX" }]);
    expect(await todas("bloqueadas")).toEqual([{ dia: HOY, instalacion: h, total: 12 }]);
  });

  it("al día siguiente: placas se actualiza y bloqueadas suma otra fila", async () => {
    await ir("/api/v1/placa", { i: ID, v: "v1.2.3", b: 10 });
    await ir("/api/v1/placa", { i: ID, v: "dev", b: 0 }, envPrueba(), depsPrueba({ ahora: () => AHORA + 86400000 }));
    const p = await todas("placas");
    expect(p).toHaveLength(1);
    expect(p[0]).toMatchObject({ dia: HOY + 1, version: "dev" });
    expect(await todas("bloqueadas")).toHaveLength(2);
  });

  it("datos malos → 400", async () => {
    for (const c of [
      { i: ID, v: "v1.2.3", b: -1 }, { i: ID, v: "v1.2.3", b: 1.5 }, { i: ID, v: "v1.2.3", b: 100000001 },
      { i: ID, v: "1.2.3", b: 1 }, { i: ID, v: "v1.2.3", b: "1" }, { i: ID, v: "v1.2.3" }, { i: "x", v: "v1.2.3", b: 1 },
    ]) {
      expect((await ir("/api/v1/placa", c)).status, JSON.stringify(c)).toBe(400);
    }
    expect(await todas("placas")).toHaveLength(0);
    expect((await ir("/api/v1/placa", { i: ID, v: "v1.2.3", b: 100000000 })).status).toBe(200);
    // Se acepta, pero se guarda con el tope diario para no inflar las estadísticas públicas.
    expect((await todas("bloqueadas"))[0]).toMatchObject({ total: 1000000 });
  });

  it("tope de 256 B y límite con clave placa", async () => {
    expect((await ir("/api/v1/placa", { i: ID, v: "v1.2.3", b: 1, relleno: "x".repeat(300) })).status).toBe(413);
    const lim = limiteFalso(false);
    expect((await ir("/api/v1/placa", { i: ID, v: "v1.2.3", b: 1 }, envPrueba({ LIMITE: lim }))).status).toBe(429);
    expect(lim.claves[0]).toMatch(/^placa:[0-9a-f]{32}$/);
  });
});

describe("POST /api/v1/paso", () => {
  it("suma por día y paso → 204", async () => {
    for (let k = 0; k < 3; k++) {
      const r = await ir("/api/v1/paso", { p: "entro" });
      expect(r.status).toBe(204);
      expect(await r.text()).toBe("");
    }
    await ir("/api/v1/paso", { p: "panel" });
    const f = await env.DB.prepare("SELECT paso, n, dia FROM pasos ORDER BY paso").all();
    expect(f.results).toEqual([
      { paso: "entro", n: 3, dia: HOY },
      { paso: "panel", n: 1, dia: HOY },
    ]);
  });

  it("paso raro → 400; tope 128 B; límite con clave paso", async () => {
    expect((await ir("/api/v1/paso", { p: "x" })).status).toBe(400);
    expect((await ir("/api/v1/paso", {})).status).toBe(400);
    expect((await ir("/api/v1/paso", { p: "entro", r: "x".repeat(200) })).status).toBe(413);
    const lim = limiteFalso(false);
    expect((await ir("/api/v1/paso", { p: "entro" }, envPrueba({ LIMITE: lim }))).status).toBe(429);
    expect(lim.claves[0]).toMatch(/^paso:/);
    expect(await todas("pasos")).toHaveLength(0);
  });
});

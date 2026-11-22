import { env, waitOnExecutionContext } from "cloudflare:test";
import { beforeAll, beforeEach, describe, expect, it } from "vitest";
import { manejar } from "../src/index";
import { LLAVE_CACHE_PERMITIDOS, MAX_PERMITIDOS } from "../src/permitidos";
import type { Deps } from "../src/tipos";
import {
  crearLlaves, ctxPrueba, depsAccess, depsPrueba, envAccess, envPrueba, firmar, HOY, type Llaves, pedido, vaciarTablas,
} from "./setup";

beforeEach(vaciarTablas);

let ll: Llaves;
let deps: Deps;
let jwt: string;
beforeAll(async () => {
  ll = await crearLlaves();
  deps = depsAccess(ll.jwks);
  jwt = await firmar(ll.buena.privateKey);
});

interface Lista {
  version: number;
  dominios: string[];
}

async function lista(d: Deps = depsPrueba()): Promise<{ r: Response; l: Lista }> {
  const r = await manejar(pedido("/api/v1/permitidos"), envPrueba(), ctxPrueba(), d);
  return { r, l: (await r.json()) as Lista };
}

function dueno(ruta: string, cuerpo: unknown, o: { jwt?: string | null; origen?: string; d?: Deps } = {}) {
  const headers: Record<string, string> = { Origin: o.origen ?? "https://siblock.s-dev.win" };
  const token = o.jwt === undefined ? jwt : o.jwt;
  if (token) headers["Cf-Access-Jwt-Assertion"] = token;
  return manejar(pedido(ruta, { cuerpo, headers }), envAccess(), ctxPrueba(), o.d ?? deps);
}
const aprobar = (dominio: unknown, o = {}) => dueno("/api/v1/dueno/permitidos", { dominio }, o);
const quitar = (dominio: unknown, o = {}) => dueno("/api/v1/dueno/permitidos/quitar", { dominio }, o);

async function filas() {
  return (await env.DB.prepare("SELECT * FROM permitidos ORDER BY dominio").all<Record<string, unknown>>()).results;
}

describe("GET /api/v1/permitidos (pública)", () => {
  it("vacía: versión 0, sin dominios, caché pública de 10 min y sin salir a internet", async () => {
    const { r, l } = await lista();
    expect(r.status).toBe(200);
    expect(l).toEqual({ version: 0, dominios: [] });
    expect(r.headers.get("Cache-Control")).toBe("public, max-age=600");
    expect(r.headers.get("Set-Cookie")).toBeNull();
  });

  it("POST → 405; no pide Access ni Turnstile", async () => {
    const r = await manejar(pedido("/api/v1/permitidos", { cuerpo: {} }), envPrueba(), ctxPrueba(), depsPrueba());
    expect(r.status).toBe(405);
  });

  it("solo nombres, en orden, y la versión sube con cada cambio", async () => {
    expect((await aprobar("zeta.ejemplo.com")).status).toBe(200);
    expect((await aprobar("alfa.ejemplo.com")).status).toBe(200);
    const { l } = await lista();
    expect(l).toEqual({ version: 2, dominios: ["alfa.ejemplo.com", "zeta.ejemplo.com"] });
    expect((await quitar("zeta.ejemplo.com")).status).toBe(200);
    expect((await lista()).l).toEqual({ version: 3, dominios: ["alfa.ejemplo.com"] });
  });

  it("con caché: la segunda vez no consulta D1, y aprobar borra la copia", async () => {
    const guardado = new Map<string, Response>();
    const cache = {
      async match(k: RequestInfo | URL) {
        return guardado.get(String(k))?.clone();
      },
      async put(k: RequestInfo | URL, r: Response) {
        guardado.set(String(k), r);
      },
      async delete(k: RequestInfo | URL) {
        return guardado.delete(String(k));
      },
    } as unknown as Cache;
    const ctx = ctxPrueba();
    const r1 = await manejar(pedido("/api/v1/permitidos"), envPrueba(), ctx, depsPrueba({ cache }));
    await waitOnExecutionContext(ctx);
    expect(((await r1.json()) as Lista).version).toBe(0);
    expect([...guardado.keys()]).toEqual([LLAVE_CACHE_PERMITIDOS]);
    await env.DB.prepare("INSERT INTO permitidos VALUES ('a.ejemplo.com', 1)").run();
    expect((await lista(depsPrueba({ cache }))).l.dominios).toEqual([]);
    expect((await aprobar("b.ejemplo.com", { d: { ...deps, cache } })).status).toBe(200);
    expect(guardado.size).toBe(0);
    expect((await lista(depsPrueba({ cache }))).l.dominios).toEqual(["a.ejemplo.com", "b.ejemplo.com"]);
  });
});

describe("POST /api/v1/dueno/permitidos (aprobar)", () => {
  it("sin JWT o con JWT de otra llave → 403 acceso y no guarda nada", async () => {
    for (const token of [null, await firmar(ll.otra.privateKey)]) {
      const r = await aprobar("ads.ejemplo.com", { jwt: token });
      expect(r.status).toBe(403);
      expect(await r.json()).toEqual({ error: "acceso" });
    }
    expect(await filas()).toEqual([]);
  });

  it("desde otra página (Origin ajeno) → 403 origen", async () => {
    const r = await aprobar("ads.ejemplo.com", { origen: "https://malo.example" });
    expect(r.status).toBe(403);
    expect(await r.json()).toEqual({ error: "origen" });
    expect(await filas()).toEqual([]);
  });

  it("válido → 200, una fila con el día de hoy; otra vez → 200 sin cambiar la versión", async () => {
    const r = await aprobar("ads.ejemplo.com");
    expect(r.status).toBe(200);
    expect(await r.json()).toEqual({ ok: true });
    expect(await filas()).toEqual([{ dominio: "ads.ejemplo.com", aprobado_en: HOY }]);
    expect((await aprobar("ads.ejemplo.com")).status).toBe(200);
    expect(await filas()).toHaveLength(1);
    expect((await lista()).l.version).toBe(1);
  });

  it("dominio con las mismas reglas que los reportes: lo demás → 400", async () => {
    for (const malo of ["ADS.ejemplo.com", "ejemplo", "a..b.com", "-a.com", "a b.com", "x".repeat(64) + ".com", "", 42, null, ["a.com"]]) {
      const r = await aprobar(malo);
      expect(r.status, String(malo)).toBe(400);
    }
    expect(await filas()).toEqual([]);
  });

  it(`tope de ${MAX_PERMITIDOS}: uno más → 409 tope; uno que ya estaba → 200`, async () => {
    const stmts = Array.from({ length: MAX_PERMITIDOS }, (_, k) =>
      env.DB.prepare("INSERT INTO permitidos VALUES (?, ?)").bind(`s${k}.ejemplo.com`, HOY),
    );
    await env.DB.batch(stmts);
    const r = await aprobar("nuevo.ejemplo.com");
    expect(r.status).toBe(409);
    expect(await r.json()).toEqual({ error: "tope" });
    expect(await filas()).toHaveLength(MAX_PERMITIDOS);
    expect((await aprobar("s7.ejemplo.com")).status).toBe(200);
    expect((await quitar("s7.ejemplo.com")).status).toBe(200);
    expect((await aprobar("nuevo.ejemplo.com")).status).toBe(200);
    expect(await filas()).toHaveLength(MAX_PERMITIDOS);
  });

  it("cuerpo de más de 512 B → 413", async () => {
    const r = await dueno("/api/v1/dueno/permitidos", { dominio: "a.com", relleno: "x".repeat(600) });
    expect(r.status).toBe(413);
  });
});

describe("POST /api/v1/dueno/permitidos/quitar", () => {
  it("sin JWT → 403 y no borra", async () => {
    await aprobar("ads.ejemplo.com");
    expect((await quitar("ads.ejemplo.com", { jwt: null })).status).toBe(403);
    expect(await filas()).toHaveLength(1);
  });

  it("uno que no estaba → 200 sin cambiar la versión; dominio malo → 400", async () => {
    await aprobar("ads.ejemplo.com");
    expect((await quitar("otro.ejemplo.com")).status).toBe(200);
    expect((await lista()).l.version).toBe(1);
    expect((await quitar("NO VALE")).status).toBe(400);
    expect(await filas()).toHaveLength(1);
  });
});

describe("resumen del dueño con los permitidos", () => {
  it("cada sitio reportado dice si ya está aprobado, y trae la lista con su fecha", async () => {
    const id = (k: number) => k.toString(16).padStart(32, "0");
    await env.DB.batch([
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'a.ejemplo.com', 'otro', 'panel', ?)").bind(id(1), HOY),
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'a.ejemplo.com', 'otro', 'pagina', ?)").bind(id(2), HOY),
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'b.ejemplo.com', 'otro', 'panel', ?)").bind(id(3), HOY),
    ]);
    await aprobar("a.ejemplo.com");
    await env.DB.prepare("INSERT INTO permitidos VALUES ('sin-reporte.ejemplo.com', 0)").run();
    const r = await manejar(
      pedido("/api/v1/dueno/resumen", { headers: { "Cf-Access-Jwt-Assertion": jwt } }),
      envAccess(), ctxPrueba(), deps,
    );
    const d = (await r.json()) as Record<string, unknown>;
    expect(d.bloqueos).toEqual([
      { dominio: "a.ejemplo.com", n: 2, aprobado: true },
      { dominio: "b.ejemplo.com", n: 1, aprobado: false },
    ]);
    expect(d.permitidos).toEqual([
      { dominio: "a.ejemplo.com", desde: "2026-11-21" },
      { dominio: "sin-reporte.ejemplo.com", desde: "1970-01-01" },
    ]);
  });
});

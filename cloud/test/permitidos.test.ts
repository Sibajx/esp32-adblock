import { env, waitOnExecutionContext } from "cloudflare:test";
import { beforeAll, beforeEach, describe, expect, it } from "vitest";
import { manejar } from "../src/index";
import { LLAVE_CACHE_PERMITIDOS, MAX_BYTES_PERMITIDOS, MAX_PERMITIDOS, esSufijo } from "../src/permitidos";
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

  it(`tope de bytes: el JSON público no pasa de ${MAX_BYTES_PERMITIDOS} B en nombres → 409 tope`, async () => {
    // 118 sitios de 250 letras: 118 × 253 = 29 854 B; uno más de 250 (+253) pasaría el tope.
    const largo = (k: number) => `${String(k).padStart(3, "0")}${"a".repeat(57)}.${"b".repeat(63)}.${"c".repeat(63)}.${"d".repeat(57)}.com`;
    expect(largo(1).length).toBe(250);
    await env.DB.batch(Array.from({ length: 118 }, (_, k) =>
      env.DB.prepare("INSERT INTO permitidos VALUES (?, ?)").bind(largo(k), HOY)));
    const antes = (await lista()).l.version;
    const r = await aprobar(largo(200));
    expect(r.status).toBe(409);
    expect(await r.json()).toEqual({ error: "tope" });
    expect((await lista()).l.version).toBe(antes);
    // Uno corto todavía cabe (29 854 + 20 ≤ 30 000), y el cuerpo público queda bajo los 32 KB.
    expect((await aprobar("corto.ejemplo.com")).status).toBe(200);
    const { l: pub } = await lista();
    expect(pub.dominios).toHaveLength(119);
    expect(JSON.stringify(pub).length).toBeLessThan(32 * 1024);
  });

  it("sufijos públicos y servicios compartidos → 400 sufijo y no guarda nada", async () => {
    for (const s of ["com.mx", "co.uk", "gob.mx", "github.io", "blogspot.com", "amazonaws.com", "s3.amazonaws.com",
      "cdn.cloudflare.net", "workers.dev", "com.zz", "org.ar", "edu.co", "net.pe", "gov.br"]) {
      const r = await aprobar(s);
      expect(r.status, s).toBe(400);
      expect(await r.json()).toEqual({ error: "sufijo" });
    }
    expect(await filas()).toEqual([]);
    // Un sitio concreto debajo de esos sí se puede aprobar.
    for (const ok of ["tienda.com.mx", "yo.github.io", "ejemplo.com", "x.co", "comercio.mx"]) {
      expect(esSufijo(ok), ok).toBe(false);
      expect((await aprobar(ok)).status, ok).toBe(200);
    }
  });
});

describe("versión de la lista (disparadores de D1)", () => {
  it("sube en la misma sentencia que cambia la lista, aunque falte la fila de la versión", async () => {
    // Sin la fila (como si se hubiera perdido): aprobar la crea con 1.
    expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM permitidos_version").first<{ n: number }>())?.n).toBe(0);
    expect((await aprobar("a.ejemplo.com")).status).toBe(200);
    expect((await lista()).l.version).toBe(1);
    // Un cambio hecho a mano en D1 también la sube: la versión nunca se queda atrás de los datos.
    await env.DB.prepare("INSERT INTO permitidos VALUES ('b.ejemplo.com', 0)").run();
    expect((await lista()).l).toEqual({ version: 2, dominios: ["a.ejemplo.com", "b.ejemplo.com"] });
    await env.DB.prepare("DELETE FROM permitidos WHERE dominio = 'b.ejemplo.com'").run();
    expect((await lista()).l.version).toBe(3);
    // Reintentar una aprobación que ya entró no la vuelve a subir.
    expect((await aprobar("a.ejemplo.com")).status).toBe(200);
    expect((await lista()).l.version).toBe(3);
  });

  it("si la sentencia falla, ni la lista ni la versión cambian", async () => {
    await aprobar("a.ejemplo.com");
    // Dos filas iguales en un mismo batch: D1 deshace todo el batch.
    await expect(env.DB.batch([
      env.DB.prepare("INSERT INTO permitidos VALUES ('c.ejemplo.com', 0)"),
      env.DB.prepare("INSERT INTO permitidos VALUES ('c.ejemplo.com', 0)"),
    ])).rejects.toThrow();
    expect((await lista()).l).toEqual({ version: 1, dominios: ["a.ejemplo.com"] });
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

  it("un subdominio de un sitio aprobado sale como cubierto (igual que en las placas)", async () => {
    const id = (k: number) => k.toString(16).padStart(32, "0");
    await env.DB.batch([
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'cdn.x.ejemplo.com', 'otro', 'panel', ?)").bind(id(1), HOY),
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'x.ejemplo.com', 'otro', 'panel', ?)").bind(id(2), HOY),
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'ox.ejemplo.com', 'otro', 'panel', ?)").bind(id(3), HOY),
      env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'ejemplo.com', 'otro', 'panel', ?)").bind(id(4), HOY),
    ]);
    await aprobar("x.ejemplo.com");
    const r = await manejar(
      pedido("/api/v1/dueno/resumen", { headers: { "Cf-Access-Jwt-Assertion": jwt } }),
      envAccess(), ctxPrueba(), deps,
    );
    const d = (await r.json()) as { bloqueos: unknown[] };
    expect(d.bloqueos).toEqual([
      { dominio: "cdn.x.ejemplo.com", n: 1, aprobado: true, cubiertoPor: "x.ejemplo.com" },
      { dominio: "ejemplo.com", n: 1, aprobado: false },
      { dominio: "ox.ejemplo.com", n: 1, aprobado: false },
      { dominio: "x.ejemplo.com", n: 1, aprobado: true },
    ]);
  });
});

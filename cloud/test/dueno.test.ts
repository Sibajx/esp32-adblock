import { env } from "cloudflare:test";
import { beforeAll, beforeEach, describe, expect, it } from "vitest";
import { manejar } from "../src/index";
import type { Deps } from "../src/tipos";
import { crearLlaves, ctxPrueba, depsAccess, envAccess, firmar, HOY, type Llaves, pedido, vaciarTablas } from "./setup";

beforeEach(vaciarTablas);

let ll: Llaves;
let deps: Deps;
beforeAll(async () => {
  ll = await crearLlaves();
  deps = depsAccess(ll.jwks);
});

async function resumen(jwt?: string) {
  const r = await manejar(
    pedido("/api/v1/dueno/resumen", { headers: jwt ? { "Cf-Access-Jwt-Assertion": jwt } : {} }),
    envAccess(), ctxPrueba(), deps,
  );
  return { r, d: (await r.json()) as Record<string, unknown> };
}

describe("GET /api/v1/dueno/resumen", () => {
  it("sin JWT → 403 acceso", async () => {
    const { r, d } = await resumen();
    expect(r.status).toBe(403);
    expect(d).toEqual({ error: "acceso" });
  });

  it("JWT de otra llave → 403", async () => {
    expect((await resumen(await firmar(ll.otra.privateKey))).r.status).toBe(403);
  });

  it("con JWT: embudo en el orden del contrato, solo 30 días", async () => {
    const ins = (d: number, paso: string, n: number) =>
      env.DB.prepare("INSERT INTO pasos (dia, paso, n) VALUES (?, ?, ?)").bind(d, paso, n).run();
    await ins(HOY, "panel", 2);
    await ins(HOY, "entro", 10);
    await ins(HOY - 29, "entro", 5);
    await ins(HOY - 30, "entro", 1000);
    await ins(HOY - 1, "wifi", 3);
    const { r, d } = await resumen(await firmar(ll.buena.privateKey));
    expect(r.status).toBe(200);
    expect(r.headers.get("Cache-Control")).toBe("no-store");
    expect(d.embudo).toEqual([
      { paso: "entro", n: 15 }, { paso: "conecto", n: 0 }, { paso: "instalo", n: 0 }, { paso: "wifi", n: 3 }, { paso: "panel", n: 2 },
    ]);
  });

  it("bloqueos ordenados por n, máximo 50; temas, motivos, guías que faltan y total de placas", async () => {
    const stmts = [];
    let k = 0;
    const id = () => (k++).toString(16).padStart(32, "0");
    for (let dom = 0; dom < 60; dom++) {
      const veces = dom === 7 ? 9 : dom === 3 ? 4 : 1;
      for (let v = 0; v < veces; v++) {
        stmts.push(env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, ?, 'pagina_no_abre', 'panel', ?)").bind(id(), `d${dom}.ejemplo.com`, HOY));
      }
    }
    stmts.push(env.DB.prepare("INSERT INTO reportes_bloqueo VALUES (?, 'viejo.ejemplo.com', 'otro', 'panel', ?)").bind(id(), HOY - 40));
    stmts.push(env.DB.prepare("INSERT INTO temas VALUES (?, 'mi_modem', 4)").bind(HOY));
    stmts.push(env.DB.prepare("INSERT INTO temas VALUES (?, 'no_instala', 2)").bind(HOY - 3));
    stmts.push(env.DB.prepare("INSERT INTO temas VALUES (?, 'otra', 99)").bind(HOY - 31));
    for (let p = 0; p < 3; p++) stmts.push(env.DB.prepare("INSERT INTO placas VALUES (?, ?, 'v1.0.0', 'MX')").bind(id(), HOY - 100 * p));
    stmts.push(env.DB.prepare("INSERT INTO reportes_modem VALUES (?, 'panel', 'MX', 1, 'E', 'huawei', 1, 'otro', ?)").bind(id(), HOY));
    stmts.push(env.DB.prepare("INSERT INTO reportes_modem VALUES (?, 'panel', 'MX', 1, 'E', 'no-se', 1, 'internet_wan', ?)").bind(id(), HOY));
    stmts.push(env.DB.prepare("INSERT INTO reportes_modem VALUES (?, 'panel', 'MX', 1, 'E', 'huawei', 0, 'internet_wan', ?)").bind(id(), HOY));
    stmts.push(env.DB.prepare("INSERT INTO reportes_modem VALUES (?, 'panel', 'MX', 1, 'E', 'zte', 1, 'internet_wan', ?)").bind(id(), HOY));
    await env.DB.batch(stmts);

    const { d } = await resumen(await firmar(ll.buena.privateKey));
    const bloqueos = d.bloqueos as { dominio: string; n: number }[];
    expect(bloqueos).toHaveLength(50);
    expect(bloqueos[0]).toEqual({ dominio: "d7.ejemplo.com", n: 9 });
    expect(bloqueos[1]).toEqual({ dominio: "d3.ejemplo.com", n: 4 });
    for (let j = 1; j < bloqueos.length; j++) expect(bloqueos[j].n).toBeLessThanOrEqual(bloqueos[j - 1].n);
    expect(d.temas).toEqual([
      { tema: "no_instala", n: 2 }, { tema: "no_bloquea", n: 0 }, { tema: "bloqueo_de_mas", n: 0 }, { tema: "mi_modem", n: 4 }, { tema: "otra", n: 0 },
    ]);
    expect(d.motivos).toEqual([
      { motivo: "pagina_no_abre", n: 71 }, { motivo: "app_no_funciona", n: 0 }, { motivo: "juego_no_funciona", n: 0 }, { motivo: "otro", n: 0 },
    ]);
    expect(d.guiasFaltan).toEqual([{ fabricante: "huawei", n: 2 }, { fabricante: "no-se", n: 1 }]);
    expect(d.placasTotal).toBe(3);
  });
});

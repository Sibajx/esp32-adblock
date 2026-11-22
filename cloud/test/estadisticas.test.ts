import { env, waitOnExecutionContext } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { manejar } from "../src/index";
import { ctxPrueba, depsPrueba, envPrueba, HOY, ID, pedido, vaciarTablas } from "./setup";

beforeEach(vaciarTablas);

interface Estadisticas {
  placas7d: number;
  versiones: { v: string; n: number }[];
  paises: { pais: string; n: number }[];
  bloqueadasSemanas: { desde: string; n: number }[];
  modems: { pais: string; proveedor: string; fabricante: string; menu: string; n: number }[];
}

let siguiente = 0;
const nuevoId = () => (siguiente++).toString(16).padStart(32, "0");

async function placa(pais: string, d = HOY, version = "v1.0.0") {
  await env.DB.prepare("INSERT INTO placas (instalacion, dia, version, pais) VALUES (?, ?, ?, ?)").bind(nuevoId(), d, version, pais).run();
}
async function modem(c: Partial<{ pais: string; proveedor: string; fabricante: string; menu: string; dns_ok: number; instalacion: string }> = {}) {
  await env.DB.prepare(
    "INSERT OR REPLACE INTO reportes_modem (instalacion, origen, pais, asn, proveedor, fabricante, dns_ok, menu, dia) VALUES (?, 'panel', ?, 1, ?, ?, ?, ?, ?)",
  ).bind(c.instalacion ?? nuevoId(), c.pais ?? "MX", c.proveedor ?? "Ejemplo", c.fabricante ?? "huawei", c.dns_ok ?? 1, c.menu ?? "internet_wan", HOY).run();
}
async function pedir(): Promise<{ r: Response; e: Estadisticas }> {
  const r = await manejar(pedido("/api/v1/estadisticas"), envPrueba(), ctxPrueba(), depsPrueba());
  return { r, e: (await r.json()) as Estadisticas };
}

describe("GET /api/v1/estadisticas", () => {
  it("vacío: ceros, 12 semanas y caché de 5 minutos", async () => {
    const { r, e } = await pedir();
    expect(r.status).toBe(200);
    expect(r.headers.get("Cache-Control")).toBe("public, max-age=300");
    expect(e.placas7d).toBe(0);
    expect(e.versiones).toEqual([]);
    expect(e.paises).toEqual([]);
    expect(e.modems).toEqual([]);
    expect(e.bloqueadasSemanas).toHaveLength(12);
    expect(e.bloqueadasSemanas.every((s) => s.n === 0)).toBe(true);
  });

  it("5 placas en MX y 4 en CO → MX:5, sin CO, sin otros", async () => {
    for (let k = 0; k < 5; k++) await placa("MX");
    for (let k = 0; k < 4; k++) await placa("CO");
    const { e } = await pedir();
    expect(e.placas7d).toBe(9);
    expect(e.paises).toEqual([{ pais: "MX", n: 5 }]);
  });

  it("4 en CO + 1 en PE → otros:5", async () => {
    for (let k = 0; k < 4; k++) await placa("CO");
    await placa("PE");
    const { e } = await pedir();
    expect(e.paises).toEqual([{ pais: "otros", n: 5 }]);
  });

  it("país desconocido cuenta en otros", async () => {
    for (let k = 0; k < 6; k++) await placa("MX");
    for (let k = 0; k < 3; k++) await placa("");
    await placa("PE");
    const { e } = await pedir();
    expect(e.paises).toEqual([{ pais: "MX", n: 6 }]);
    await placa("CL");
    expect((await pedir()).e.paises).toEqual([{ pais: "MX", n: 6 }, { pais: "otros", n: 5 }]);
  });

  it("placa de hace 7 días no cuenta; la de hace 6 sí", async () => {
    await placa("MX", HOY - 7);
    await placa("MX", HOY - 6);
    const { e } = await pedir();
    expect(e.placas7d).toBe(1);
  });

  it("versiones de las placas de la semana, la más usada primero", async () => {
    await placa("MX", HOY, "v1.0.0");
    await placa("MX", HOY, "v1.1.0");
    await placa("MX", HOY, "v1.1.0");
    await placa("MX", HOY - 10, "v0.9.0");
    const { e } = await pedir();
    expect(e.versiones).toEqual([{ v: "v1.1.0", n: 2 }, { v: "v1.0.0", n: 1 }]);
  });

  it("módems: 3 reportes iguales → sale; 2 → no", async () => {
    await modem();
    await modem();
    expect((await pedir()).e.modems).toEqual([]);
    await modem();
    expect((await pedir()).e.modems).toEqual([
      { pais: "MX", proveedor: "Ejemplo", fabricante: "huawei", menu: "internet_wan", n: 3 },
    ]);
  });

  it("un mismo número que reporta 5 veces cuenta 1", async () => {
    for (let k = 0; k < 5; k++) await modem({ instalacion: ID });
    await modem();
    expect((await pedir()).e.modems).toEqual([]);
  });

  it("menu otro, fabricante no-se o dns_ok 0 → nunca salen", async () => {
    for (let k = 0; k < 3; k++) {
      await modem({ menu: "otro" });
      await modem({ fabricante: "no-se" });
      await modem({ dns_ok: 0 });
    }
    expect((await pedir()).e.modems).toEqual([]);
  });

  it("12 semanas que empiezan en lunes, la más vieja primero, con la suma de bloqueadas", async () => {
    const ins = (d: number, total: number) =>
      env.DB.prepare("INSERT INTO bloqueadas (dia, instalacion, total) VALUES (?, ?, ?)").bind(d, nuevoId(), total).run();
    await ins(HOY, 10); // sábado 2026-11-21 → semana del lunes 2026-11-16
    await ins(HOY - 5, 5); // lunes 2026-11-16
    await ins(HOY - 6, 7); // domingo 2026-11-15 → semana anterior
    await ins(HOY - 7 * 12, 1000); // fuera de las 12 semanas
    const { e } = await pedir();
    expect(e.bloqueadasSemanas).toHaveLength(12);
    expect(e.bloqueadasSemanas[11]).toEqual({ desde: "2026-11-16", n: 15 });
    expect(e.bloqueadasSemanas[10]).toEqual({ desde: "2026-11-09", n: 7 });
    expect(e.bloqueadasSemanas[0].desde).toBe("2026-08-31");
    expect(e.bloqueadasSemanas.reduce((a, s) => a + s.n, 0)).toBe(22);
    for (const s of e.bloqueadasSemanas) expect(new Date(`${s.desde}T00:00:00Z`).getUTCDay()).toBe(1);
  });

  it("con caché: el segundo pedido (aunque cambie ?x=) no consulta D1 otra vez", async () => {
    const guardado = new Map<string, Response>();
    const cache = {
      async match(k: RequestInfo | URL) {
        return guardado.get(String(k))?.clone();
      },
      async put(k: RequestInfo | URL, r: Response) {
        guardado.set(String(k), r);
      },
    } as unknown as Cache;
    const ctx = ctxPrueba();
    const r1 = await manejar(pedido("/api/v1/estadisticas"), envPrueba(), ctx, depsPrueba({ cache }));
    await waitOnExecutionContext(ctx);
    expect(((await r1.json()) as Estadisticas).placas7d).toBe(0);
    expect(guardado.size).toBe(1);
    await placa("MX");
    const r2 = await manejar(pedido("/api/v1/estadisticas?x=123"), envPrueba(), ctxPrueba(), depsPrueba({ cache }));
    expect(((await r2.json()) as Estadisticas).placas7d).toBe(0);
  });
});

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { claveIp, dia, error, json, leerJson, origenValido, redDeIp } from "../src/http";
import { manejar } from "../src/index";
import { ctxPrueba, depsPrueba, envPrueba, ID, pedido, TABLAS, vaciarTablas } from "./setup";

beforeEach(vaciarTablas);

const cuerpo500 = (r: Response) => r.json();

async function cuerpo(r: Response): Promise<unknown> {
  return JSON.parse(await r.text());
}

describe("http", () => {
  it("json pone tipo, nosniff y no-store", async () => {
    const r = json(200, { ok: true });
    expect(r.status).toBe(200);
    expect(r.headers.get("Content-Type")).toBe("application/json; charset=utf-8");
    expect(r.headers.get("X-Content-Type-Options")).toBe("nosniff");
    expect(r.headers.get("Cache-Control")).toBe("no-store");
    expect(await cuerpo(r)).toEqual({ ok: true });
    expect(json(200, {}, "public, max-age=300").headers.get("Cache-Control")).toBe("public, max-age=300");
  });

  it("error usa el código del contrato", async () => {
    const r = error(429, "limite");
    expect(r.status).toBe(429);
    expect(await cuerpo(r)).toEqual({ error: "limite" });
  });

  it("leerJson: tope, JSON roto y no objeto", async () => {
    const grande = await leerJson(pedido("/x", { crudo: `{"a":"${"x".repeat(9000)}"}` }), 1024);
    expect(grande).toBeInstanceOf(Response);
    expect((grande as Response).status).toBe(413);
    for (const crudo of ["[1]", "{roto", "", "null", '"hola"', "3"]) {
      const r = await leerJson(pedido("/x", { crudo }), 1024);
      expect(r, crudo).toBeInstanceOf(Response);
      expect((r as Response).status, crudo).toBe(400);
    }
    expect(await leerJson(pedido("/x", { cuerpo: { a: 1 } }), 1024)).toEqual({ a: 1 });
  });

  it("leerJson no confía en un Content-Length chico", async () => {
    const r = await leerJson(
      pedido("/x", { crudo: `{"a":"${"x".repeat(5000)}"}`, headers: { "Content-Length": "10" } }),
      1024,
    );
    expect((r as Response).status).toBe(413);
  });

  it("origenValido", () => {
    const e = envPrueba();
    expect(origenValido(pedido("/x"), e)).toBe(true);
    expect(origenValido(pedido("/x", { headers: { Origin: "https://siblock.s-dev.win" } }), e)).toBe(true);
    expect(origenValido(pedido("/x", { headers: { Origin: "https://malo.example" } }), e)).toBe(false);
    expect(origenValido(pedido("/x", { headers: { Origin: "null" } }), e)).toBe(false);
  });

  it("dia cuenta días UTC desde 1970", () => {
    expect(dia(0)).toBe(0);
    expect(dia(86400000 - 1)).toBe(0);
    expect(dia(Date.UTC(2026, 10, 21, 23, 59))).toBe(Math.floor(Date.UTC(2026, 10, 21) / 86400000));
  });

  it("claveIp: ruta + 32 hex, sin la IP", async () => {
    const e = envPrueba();
    const a = await claveIp(pedido("/x"), e, "reporte");
    expect(a).toMatch(/^reporte:[0-9a-f]{32}$/);
    expect(a).not.toContain("203.0.113.7");
    expect(await claveIp(pedido("/x"), e, "reporte")).toBe(a);
    const otra = await claveIp(pedido("/x", { headers: { "CF-Connecting-IP": "198.51.100.1" } }), e, "reporte");
    expect(otra).not.toBe(a);
    expect(await claveIp(pedido("/x"), envPrueba({ ID_PEPPER: "otra" }), "reporte")).not.toBe(a);
  });

  it("redDeIp: IPv4 igual; IPv6 solo su /64", () => {
    expect(redDeIp("203.0.113.7")).toBe("203.0.113.7");
    expect(redDeIp("")).toBe("");
    expect(redDeIp("2001:db8:1:2:aaaa:bbbb:cccc:dddd")).toBe("2001:db8:1:2::/64");
    expect(redDeIp("2001:DB8:0001:0002::1")).toBe("2001:db8:1:2::/64");
    expect(redDeIp("2001:db8::1")).toBe("2001:db8:0:0::/64");
    expect(redDeIp("::1")).toBe("0:0:0:0::/64");
    expect(redDeIp("fe80::1%eth0")).toBe("fe80:0:0:0::/64");
    expect(redDeIp("::ffff:198.51.100.1")).toBe("198.51.100.1");
  });

  it("claveIp: dos IPv6 del mismo /64 dan la misma clave; otro /64, otra", async () => {
    const e = envPrueba();
    const k = (ip: string) => claveIp(pedido("/x", { headers: { "CF-Connecting-IP": ip } }), e, "reporte");
    const a = await k("2001:db8:1:2::1");
    expect(await k("2001:db8:1:2:ffff:ffff:ffff:ffff")).toBe(a);
    expect(await k("2001:db8:1:3::1")).not.toBe(a);
  });
});

describe("enrutador", () => {
  const ir = (req: Request) => manejar(req, envPrueba(), ctxPrueba(), depsPrueba());

  it("ruta desconocida → 404", async () => {
    for (const ruta of ["/api/v1/nada", "/api/v2/pais", "/", "/api/v1/pais/extra"]) {
      const r = await ir(pedido(ruta));
      expect(r.status, ruta).toBe(404);
      expect(await cuerpo(r)).toEqual({ error: "no_existe" });
    }
  });

  it("método equivocado → 405", async () => {
    const r = await ir(pedido("/api/v1/contacto"));
    expect(r.status).toBe(405);
    expect(await cuerpo(r)).toEqual({ error: "metodo" });
    expect((await ir(pedido("/api/v1/pais", { metodo: "POST", cuerpo: {} }))).status).toBe(405);
  });

  it("cuerpo de 9000 B a una ruta con tope 1024 → 413", async () => {
    const r = await ir(pedido("/api/v1/reporte/modem", { crudo: `{"i":"${"a".repeat(9000)}"}` }));
    expect(r.status).toBe(413);
    expect(await cuerpo(r)).toEqual({ error: "grande" });
  });

  it('"[1]" → 400', async () => {
    const r = await ir(pedido("/api/v1/reporte/modem", { crudo: "[1]" }));
    expect(r.status).toBe(400);
    expect(await cuerpo(r)).toEqual({ error: "datos" });
  });

  it("Origin ajeno en POST → 403 origen", async () => {
    const r = await ir(
      pedido("/api/v1/reporte/modem", {
        cuerpo: { i: ID, origen: "panel", fabricante: "huawei", dns_ok: true, menu: "internet_wan" },
        headers: { Origin: "https://malo.example" },
      }),
    );
    expect(r.status).toBe(403);
    expect(await cuerpo(r)).toEqual({ error: "origen" });
  });

  it("sin Origin pasa a la ruta", async () => {
    const r = await ir(
      pedido("/api/v1/reporte/modem", {
        cuerpo: { i: ID, origen: "panel", fabricante: "huawei", dns_ok: true, menu: "internet_wan" },
      }),
    );
    expect([403, 404, 405]).not.toContain(r.status);
  });

  it("sin ID_PEPPER (o muy corta) los POST no guardan nada → 500", async () => {
    const cuerpo = { i: ID, origen: "panel", fabricante: "huawei", dns_ok: true, menu: "internet_wan" };
    for (const ID_PEPPER of ["", "corta", undefined as unknown as string]) {
      const r = await manejar(pedido("/api/v1/reporte/modem", { cuerpo }), envPrueba({ ID_PEPPER }), ctxPrueba(), depsPrueba());
      expect(r.status).toBe(500);
      expect(await cuerpo500(r)).toEqual({ error: "interno" });
    }
    const n = await env.DB.prepare("SELECT COUNT(*) AS n FROM reportes_modem").first<{ n: number }>();
    expect(n?.n).toBe(0);
  });

  it("las tablas existen", async () => {
    const { results } = await env.DB.prepare(
      "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name",
    ).all<{ name: string }>();
    const nombres = results.map((f) => f.name);
    for (const t of TABLAS) expect(nombres).toContain(t);
  });
});

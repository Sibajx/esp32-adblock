import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { correoAlDueno, respuestaAutomatica, sinHtml } from "../src/correo";
import { TOPE_RESPUESTAS } from "../src/contacto";
import { manejar } from "../src/index";
import type { Deps, Env } from "../src/tipos";
import {
  ctxPrueba, correoFalso, type CorreoFalso, depsPrueba, envPrueba, fetchTurnstile, HOY, limiteFalso, pedido, vaciarTablas,
} from "./setup";

beforeEach(vaciarTablas);

const NOMBRE = "Rosa Pérez";
const MENSAJE = "Hola, no me deja instalarlo <script>alert(1)</script> en mi casa.";
const valido = (c: Record<string, unknown> = {}) => ({
  nombre: NOMBRE, correo: "persona@example.com", tema: "no_instala", mensaje: MENSAJE, turnstile: "tok", ...c,
});

async function mandar(cuerpo: unknown, cambios: Partial<Env> = {}, deps: Deps = depsPrueba({ fetch: fetchTurnstile(true) })) {
  const correo = (cambios.CORREO as CorreoFalso | undefined) ?? correoFalso();
  const e = envPrueba({ CORREO: correo, ...cambios });
  const r = await manejar(pedido("/api/v1/contacto", { cuerpo }), e, ctxPrueba(), deps);
  return { r, correo };
}
const temas = async () => (await env.DB.prepare("SELECT * FROM temas").all()).results;
const porDia = async () => (await env.DB.prepare("SELECT * FROM contacto_dia").all()).results;

describe("sinHtml", () => {
  it("quita etiquetas, decodifica lo básico y deja solo saltos de línea", () => {
    expect(sinHtml("<b>hola</b> &lt;3")).toBe("hola <3");
    expect(sinHtml("a\r\nb\rc")).toBe("a\nb\nc");
    expect(sinHtml("x\u0000y\u0007z\tw")).toBe("xyzw");
    expect(sinHtml("&quot;si&quot; &#39;no&#39; &amp;lt;")).toBe("\"si\" 'no' &lt;");
    expect(sinHtml("<a href='x'>enlace</a><img src=x onerror=alert(1)>")).toBe("enlace");
  });
});

describe("correoAlDueno y respuestaAutomatica", () => {
  const e = envPrueba();
  it("asunto fijo, Reply-To con el correo de la persona, sin HTML", () => {
    const m = correoAlDueno({ nombre: "Malo\r\nBcc: x@y.z", correo: "p@example.com", tema: "mi_modem", mensaje: "<i>hola</i> mundo" }, e);
    expect(m.to).toBe("contacto@s-dev.win");
    expect(m.from).toEqual({ email: "contacto@s-dev.win", name: "SiBlock" });
    expect(m.replyTo).toBe("p@example.com");
    expect(m.subject).toBe("[SiBlock] Ayuda con mi módem");
    expect(m.text).toContain("hola mundo");
    expect(m.text).not.toContain("<i>");
    expect(m).not.toHaveProperty("html");
  });
  it("la respuesta automática es texto fijo", () => {
    const m = respuestaAutomatica("p@example.com", e);
    expect(m.to).toBe("p@example.com");
    expect(m.subject).toBe("Recibimos tu mensaje");
    expect(m.text).toContain("Gracias por usar SiBlock.");
    expect(m.text).toContain("https://siblock.s-dev.win/guia-router.html");
    expect(m.text).toBe(respuestaAutomatica("otro@example.org", e).text);
    expect(m).not.toHaveProperty("replyTo");
    expect(m).not.toHaveProperty("html");
  });
});

describe("POST /api/v1/contacto", () => {
  it("válido → 200, dos correos, cuenta el tema y el día", async () => {
    const { r, correo } = await mandar(valido({ diagnostico: "Versión: v1.2.3\nMemoria: 100" }));
    expect(r.status).toBe(200);
    expect(await r.json()).toEqual({ ok: true });
    expect(correo.mandados).toHaveLength(2);
    const [dueno, auto] = correo.mandados;
    expect(dueno.replyTo).toBe("persona@example.com");
    expect(dueno.to).toBe("contacto@s-dev.win");
    expect(dueno.subject).toBe("[SiBlock] No puedo instalarlo");
    expect(dueno.subject).not.toContain(NOMBRE);
    expect(dueno.text).not.toContain("<script>");
    expect(dueno.text).toContain(NOMBRE);
    expect(dueno.text).toContain("Versión: v1.2.3");
    expect(auto.to).toBe("persona@example.com");
    expect(auto.text).not.toContain(NOMBRE);
    expect(auto.text).not.toContain("instalarlo");
    expect(auto.subject).not.toContain(NOMBRE);
    expect(await temas()).toEqual([{ dia: HOY, tema: "no_instala", n: 1 }]);
    expect(await porDia()).toEqual([{ dia: HOY, n: 1 }]);
  });

  it("sin nombre también vale", async () => {
    const { r } = await mandar(valido({ nombre: undefined }));
    expect(r.status).toBe(200);
  });

  it("correo con salto de línea → 400 y 0 correos", async () => {
    for (const c of ["a@b.c\r\nBcc: x@y.z", "a@b.c\nBcc: x", "no es correo", "", 3]) {
      const { r, correo } = await mandar(valido({ correo: c }));
      expect(r.status, JSON.stringify(c)).toBe(400);
      expect(correo.mandados).toHaveLength(0);
    }
  });

  it("nombre con saltos o de más de 80 → 400", async () => {
    for (const nombre of ["Ana\r\nBcc: x@y.z", "x".repeat(81), 5]) {
      const { r, correo } = await mandar(valido({ nombre }));
      expect(r.status, JSON.stringify(nombre)).toBe(400);
      expect(correo.mandados).toHaveLength(0);
    }
    expect((await mandar(valido({ nombre: "x".repeat(80) }))).r.status).toBe(200);
  });

  it("mensaje de 9 o de 2001 → 400; de 10 y de 2000 → 200", async () => {
    expect((await mandar(valido({ mensaje: "x".repeat(9) }))).r.status).toBe(400);
    expect((await mandar(valido({ mensaje: "x".repeat(2001) }))).r.status).toBe(400);
    expect((await mandar(valido({ mensaje: "x".repeat(10) }))).r.status).toBe(200);
    expect((await mandar(valido({ mensaje: "😀".repeat(2000) }))).r.status).toBe(200);
  });

  it("tema raro → 400; diagnóstico > 1500 → 400", async () => {
    expect((await mandar(valido({ tema: "spam" }))).r.status).toBe(400);
    expect((await mandar(valido({ diagnostico: "x".repeat(1501) }))).r.status).toBe(400);
    expect((await mandar(valido({ diagnostico: 5 }))).r.status).toBe(400);
  });

  it("sin Turnstile o con uno malo → 403 y 0 correos", async () => {
    const sin = await mandar(valido({ turnstile: undefined }));
    expect(sin.r.status).toBe(403);
    expect(await sin.r.json()).toEqual({ error: "turnstile" });
    expect(sin.correo.mandados).toHaveLength(0);
    const malo = await mandar(valido(), {}, depsPrueba({ fetch: fetchTurnstile(false) }));
    expect(malo.r.status).toBe(403);
    expect(malo.correo.mandados).toHaveLength(0);
  });

  it("límite por IP → 429 con la clave contacto", async () => {
    const lim = limiteFalso(false);
    const { r, correo } = await mandar(valido(), { LIMITE_CONTACTO: lim });
    expect(r.status).toBe(429);
    expect(correo.mandados).toHaveLength(0);
    expect(lim.claves[0]).toMatch(/^contacto:[0-9a-f]{32}$/);
  });

  it("tope de 200 al día → 429 sin mandar", async () => {
    await env.DB.prepare("INSERT INTO contacto_dia (dia, n) VALUES (?, 200)").bind(HOY).run();
    const { r, correo } = await mandar(valido());
    expect(r.status).toBe(429);
    expect(correo.mandados).toHaveLength(0);
    await env.DB.prepare("UPDATE contacto_dia SET n = 199").run();
    expect((await mandar(valido())).r.status).toBe(200);
  });

  it('RESPUESTA_AUTOMATICA="0" → un solo correo', async () => {
    const { r, correo } = await mandar(valido(), { RESPUESTA_AUTOMATICA: "0" });
    expect(r.status).toBe(200);
    expect(correo.mandados).toHaveLength(1);
    expect(correo.mandados[0].to).toBe("contacto@s-dev.win");
  });

  it("si el correo al dueño falla → 500 y no cuenta", async () => {
    const { r } = await mandar(valido(), { CORREO: correoFalso(() => true) });
    expect(r.status).toBe(500);
    expect(await r.json()).toEqual({ error: "interno" });
    expect(await temas()).toEqual([]);
    expect(await porDia()).toEqual([]);
  });

  it("si solo falla la respuesta automática → igual 200 y cuenta", async () => {
    const { r, correo } = await mandar(valido(), { CORREO: correoFalso((n) => n === 1) });
    expect(r.status).toBe(200);
    expect(correo.mandados).toHaveLength(1);
    expect(await porDia()).toEqual([{ dia: HOY, n: 1 }]);
  });

  it("una sola respuesta automática al día por dirección (sin importar mayúsculas)", async () => {
    const a = await mandar(valido());
    expect(a.correo.mandados).toHaveLength(2);
    const b = await mandar(valido({ correo: "Persona@Example.com" }));
    expect(b.r.status).toBe(200);
    expect(b.correo.mandados).toHaveLength(1);
    expect(b.correo.mandados[0].to).toBe("contacto@s-dev.win");
    const filas = (await env.DB.prepare("SELECT * FROM respuestas_dia").all()).results;
    expect(filas).toHaveLength(1);
    expect(JSON.stringify(filas)).not.toMatch(/persona|example/i);
    expect((await mandar(valido({ correo: "otra@example.com" }))).correo.mandados).toHaveLength(2);
  });

  it("tope propio de respuestas automáticas: al llegar, solo va el correo al dueño", async () => {
    const lote = [];
    for (let k = 0; k < TOPE_RESPUESTAS; k++) {
      lote.push(env.DB.prepare("INSERT INTO respuestas_dia VALUES (?, ?)").bind(HOY, `${k}`.padStart(32, "0")));
    }
    await env.DB.batch(lote);
    const { r, correo } = await mandar(valido());
    expect(r.status).toBe(200);
    expect(correo.mandados).toHaveLength(1);
    expect(correo.mandados[0].to).toBe("contacto@s-dev.win");
    expect(await porDia()).toEqual([{ dia: HOY, n: 1 }]);
  });

  it("el tope diario se cuenta en un solo paso: con 198, de 5 a la vez solo pasan 2", async () => {
    await env.DB.prepare("INSERT INTO contacto_dia (dia, n) VALUES (?, 198)").bind(HOY).run();
    const rs = await Promise.all(Array.from({ length: 5 }, (_, k) => mandar(valido({ correo: `p${k}@example.com` }))));
    expect(rs.filter((x) => x.r.status === 200)).toHaveLength(2);
    expect(rs.filter((x) => x.r.status === 429)).toHaveLength(3);
    expect(await porDia()).toEqual([{ dia: HOY, n: 200 }]);
  });

  it("si el correo al dueño falla con otros ya contados, devuelve su lugar", async () => {
    await env.DB.prepare("INSERT INTO contacto_dia (dia, n) VALUES (?, 7)").bind(HOY).run();
    const { r } = await mandar(valido(), { CORREO: correoFalso(() => true) });
    expect(r.status).toBe(500);
    expect(await porDia()).toEqual([{ dia: HOY, n: 7 }]);
  });

  it("tope de 8192 B", async () => {
    const { r } = await mandar(valido({ relleno: "x".repeat(9000) }));
    expect(r.status).toBe(413);
  });
});

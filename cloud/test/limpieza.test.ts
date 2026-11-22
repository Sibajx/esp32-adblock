import { createExecutionContext, createScheduledController, env, waitOnExecutionContext } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import worker from "../src/index";
import { limpiar, TABLAS_CON_DIA } from "../src/limpieza";
import { AHORA, envPrueba, HOY, ID, vaciarTablas } from "./setup";

beforeEach(vaciarTablas);

async function sembrar() {
  const s = [];
  for (const d of [HOY - 401, HOY - 399]) {
    s.push(env.DB.prepare("INSERT INTO bloqueadas VALUES (?, ?, 1)").bind(d, ID));
    s.push(env.DB.prepare("INSERT INTO pasos VALUES (?, 'entro', 1)").bind(d));
    s.push(env.DB.prepare("INSERT INTO temas VALUES (?, 'otra', 1)").bind(d));
    s.push(env.DB.prepare("INSERT INTO contacto_dia VALUES (?, 1)").bind(d));
    s.push(env.DB.prepare("INSERT INTO placas VALUES (?, ?, 'v1.0.0', 'MX')").bind(`${d}`.padStart(32, "0"), d));
  }
  await env.DB.batch(s);
}

describe("limpieza diaria", () => {
  it("borra lo de hace más de 400 días y deja lo de 399", async () => {
    await sembrar();
    await limpiar(envPrueba(), AHORA);
    for (const t of TABLAS_CON_DIA) {
      const { results } = await env.DB.prepare(`SELECT dia FROM ${t}`).all<{ dia: number }>();
      expect(results, t).toEqual([{ dia: HOY - 399 }]);
    }
  });

  it("deja exactamente 400 días", async () => {
    await env.DB.prepare("INSERT INTO pasos VALUES (?, 'entro', 1)").bind(HOY - 400).run();
    await limpiar(envPrueba(), AHORA);
    expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM pasos").first<{ n: number }>())?.n).toBe(1);
  });

  it("las marcas de respuesta automática duran solo el día", async () => {
    await env.DB.batch([
      env.DB.prepare("INSERT INTO respuestas_dia VALUES (?, ?)").bind(HOY - 1, "a".repeat(32)),
      env.DB.prepare("INSERT INTO respuestas_dia VALUES (?, ?)").bind(HOY, "b".repeat(32)),
    ]);
    await limpiar(envPrueba(), AHORA);
    const { results } = await env.DB.prepare("SELECT dia FROM respuestas_dia").all<{ dia: number }>();
    expect(results).toEqual([{ dia: HOY }]);
  });

  it("el cron del Worker la corre", async () => {
    await sembrar();
    const ctx = createExecutionContext();
    await worker.scheduled(createScheduledController({ scheduledTime: AHORA, cron: "17 3 * * *" }), envPrueba(), ctx);
    await waitOnExecutionContext(ctx);
    const { results } = await env.DB.prepare("SELECT dia FROM placas").all<{ dia: number }>();
    expect(results).toEqual([{ dia: HOY - 399 }]);
  });
});

import { beforeAll, describe, expect, it } from "vitest";
import { verificarAccess } from "../src/access";
import { AHORA, AUD, crearLlaves, depsAccess, depsPrueba, envAccess, EQUIPO, firmar, type Llaves, pedido } from "./setup";

const conJwt = (jwt?: string) => pedido("/api/v1/dueno/resumen", { headers: jwt ? { "Cf-Access-Jwt-Assertion": jwt } : {} });

describe("verificarAccess", () => {
  let ll: Llaves;
  beforeAll(async () => {
    ll = await crearLlaves();
  });

  it("JWT bueno → true y busca las llaves en el dominio del equipo", async () => {
    const urls: string[] = [];
    expect(await verificarAccess(conJwt(await firmar(ll.buena.privateKey)), envAccess(), depsAccess(ll.jwks, urls))).toBe(true);
    expect(urls).toEqual([`${EQUIPO}/cdn-cgi/access/certs`]);
  });

  it("aud como texto también vale", async () => {
    expect(await verificarAccess(conJwt(await firmar(ll.buena.privateKey, { aud: AUD })), envAccess(), depsAccess(ll.jwks))).toBe(true);
  });

  it("holgura de 60 s en exp y nbf", async () => {
    const s = Math.floor(AHORA / 1000);
    expect(await verificarAccess(conJwt(await firmar(ll.buena.privateKey, { exp: s - 30 })), envAccess(), depsAccess(ll.jwks))).toBe(true);
    expect(await verificarAccess(conJwt(await firmar(ll.buena.privateKey, { nbf: s + 30 })), envAccess(), depsAccess(ll.jwks))).toBe(true);
    expect(await verificarAccess(conJwt(await firmar(ll.buena.privateKey, { nbf: s + 120 })), envAccess(), depsAccess(ll.jwks))).toBe(false);
  });

  it("vencido, otra aud, otro iss, otra llave, alg none, sin header → false", async () => {
    const s = Math.floor(AHORA / 1000);
    const casos: [string, string | undefined][] = [
      ["vencido", await firmar(ll.buena.privateKey, { exp: s - 120 })],
      ["sin exp", await firmar(ll.buena.privateKey, { exp: undefined })],
      ["otra aud", await firmar(ll.buena.privateKey, { aud: ["otra"] })],
      ["otro iss", await firmar(ll.buena.privateKey, { iss: "https://malo.cloudflareaccess.com" })],
      ["otra llave", await firmar(ll.otra.privateKey)],
      ["alg none", await firmar(ll.buena.privateKey, {}, { alg: "none", kid: "llave-1" })],
      ["alg HS256", await firmar(ll.buena.privateKey, {}, { alg: "HS256", kid: "llave-1" })],
      ["kid desconocido", await firmar(ll.buena.privateKey, {}, { alg: "RS256", kid: "otra" })],
      ["sin firma", (await firmar(ll.buena.privateKey)).split(".").slice(0, 2).join(".") + "."],
      ["basura", "a.b.c"],
      ["sin header", undefined],
    ];
    for (const [nombre, jwt] of casos) {
      expect(await verificarAccess(conJwt(jwt), envAccess(), depsAccess(ll.jwks)), nombre).toBe(false);
    }
  });

  it("vars vacías → false sin salir a internet", async () => {
    const jwt = await firmar(ll.buena.privateKey);
    expect(await verificarAccess(conJwt(jwt), envAccess({ ACCESS_AUD: "" }), depsPrueba())).toBe(false);
    expect(await verificarAccess(conJwt(jwt), envAccess({ ACCESS_TEAM_DOMAIN: "" }), depsPrueba())).toBe(false);
    expect(await verificarAccess(conJwt(jwt), envAccess({ ACCESS_TEAM_DOMAIN: "http://equipo.example" }), depsPrueba())).toBe(false);
  });

  it("si las llaves no se pueden traer → false", async () => {
    const jwt = await firmar(ll.buena.privateKey);
    expect(await verificarAccess(conJwt(jwt), envAccess(), depsPrueba({ fetch: async () => { throw new Error("red"); } }))).toBe(false);
    expect(await verificarAccess(conJwt(jwt), envAccess(), depsAccess({ nada: 1 }))).toBe(false);
  });
});

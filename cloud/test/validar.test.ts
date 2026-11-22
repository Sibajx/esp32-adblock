import { describe, expect, it } from "vitest";
import { FABRICANTES, MENUS, MOTIVOS, PASOS, TEMAS } from "../src/opciones";
import { dentroDelLimite, esCorreo, esDominio, esId, esVersion, hashId } from "../src/validar";
import { ID, limiteFalso } from "./setup";

describe("listas fijas del contrato", () => {
  it("son exactamente las del contrato", () => {
    expect([...FABRICANTES]).toEqual([
      "tp-link", "huawei", "zte", "technicolor", "arris", "netgear", "asus", "mikrotik", "proveedor", "no-se",
    ]);
    expect([...MENUS]).toEqual(["internet_wan", "red_local", "avanzado", "otro"]);
    expect([...MOTIVOS]).toEqual(["pagina_no_abre", "app_no_funciona", "juego_no_funciona", "otro"]);
    expect([...TEMAS]).toEqual(["no_instala", "no_bloquea", "bloqueo_de_mas", "mi_modem", "otra"]);
    expect([...PASOS]).toEqual(["entro", "conecto", "instalo", "wifi", "panel"]);
    expect(Object.isFrozen(FABRICANTES)).toBe(true);
  });
});

describe("esId", () => {
  it("acepta 32 hex en minúsculas", () => {
    expect(esId(ID)).toBe(true);
    expect(esId("f".repeat(32))).toBe(true);
  });
  it("rechaza lo demás", () => {
    for (const malo of [ID.slice(1), ID + "a", ID.toUpperCase(), "g" + ID.slice(1), "", ID.slice(0, 16) + "\0" + ID.slice(17), 42, null, undefined, [ID]]) {
      expect(esId(malo), String(malo)).toBe(false);
    }
  });
});

describe("esDominio (misma regla que validReportDomain)", () => {
  it("acepta", () => {
    for (const d of ["ads.ejemplo.com", "a-b.c0", "x.y", "a".repeat(63) + ".com"]) expect(esDominio(d), d).toBe(true);
  });
  it("rechaza", () => {
    const largo = ("a".repeat(63) + ".").repeat(4).slice(0, 254);
    expect(largo.length).toBe(254);
    for (const d of ["ejemplo", "-a.com", "a-.com", "a..com", "ADS.com", "a_b.com", largo, "a".repeat(64) + ".com", "a.com.", ".a.com", "", "a b.com", "ab", 7, null]) {
      expect(esDominio(d), String(d)).toBe(false);
    }
  });
});

describe("esVersion", () => {
  it("acepta vX.Y.Z y dev", () => {
    for (const v of ["v0.0.0", "v1.20.3", "v9999.0.1", "dev"]) expect(esVersion(v), v).toBe(true);
  });
  it("rechaza", () => {
    for (const v of ["1.2.3", "v1.2", "v1.2.3.4", "v1.2.x", "v10000.0.0", "v1.2.3 ", "v01.2.3", "DEV", "", 1]) {
      expect(esVersion(v), String(v)).toBe(false);
    }
  });
});

describe("esCorreo", () => {
  it("acepta", () => {
    for (const c of ["a@b.c", "nombre.apellido+x@example.com.mx", "x".repeat(64) + "@" + "d".repeat(185) + ".com"]) {
      expect(esCorreo(c), c).toBe(true);
    }
  });
  it("rechaza", () => {
    for (const c of [
      "a@b.c\r\nBcc: x@y.z", "a@b.c\nBcc: x", "a b@c.d", "sin-arroba", "a@@b.c", "a@b@c.d", "@b.c", "a@",
      "x".repeat(250) + "@b.cd", "a@b.c\t", "<a@b.c>", "a@b.c,otro@x.y", "a@b.c;x", '"a"@b.c', "", 5, null,
    ]) {
      expect(esCorreo(c), JSON.stringify(c)).toBe(false);
    }
  });
});

describe("hashId", () => {
  it("es estable, de 32 hex y distinto del número", async () => {
    const a = await hashId(ID, "pimienta");
    expect(a).toMatch(/^[0-9a-f]{32}$/);
    expect(a).not.toBe(ID);
    expect(await hashId(ID, "pimienta")).toBe(a);
    expect(await hashId(ID, "otra")).not.toBe(a);
    expect(await hashId("f".repeat(32), "pimienta")).not.toBe(a);
  });
});

describe("dentroDelLimite", () => {
  it("dice lo que dice el límite y pasa la clave", async () => {
    const si = limiteFalso(true);
    expect(await dentroDelLimite(si, "reporte:abc")).toBe(true);
    expect(si.claves).toEqual(["reporte:abc"]);
    expect(await dentroDelLimite(limiteFalso(false), "x")).toBe(false);
  });
  it("si el límite falla, no deja pasar", async () => {
    const roto: RateLimit = { limit: async () => { throw new Error("roto"); } };
    expect(await dentroDelLimite(roto, "x")).toBe(false);
  });
});

// Listas fijas del contrato entre carriles (mismos valores en la placa y en la página).

export const FABRICANTES: readonly string[] = Object.freeze([
  "tp-link", "huawei", "zte", "technicolor", "arris", "netgear", "asus", "mikrotik", "proveedor", "no-se",
]);
export const MENUS: readonly string[] = Object.freeze(["internet_wan", "red_local", "avanzado", "otro"]);
export const MOTIVOS: readonly string[] = Object.freeze([
  "pagina_no_abre", "app_no_funciona", "juego_no_funciona", "otro",
]);
export const TEMAS: readonly string[] = Object.freeze([
  "no_instala", "no_bloquea", "bloqueo_de_mas", "mi_modem", "otra",
]);
export const PASOS: readonly string[] = Object.freeze(["entro", "conecto", "instalo", "wifi", "panel"]);

export function enLista(lista: readonly string[], v: unknown): v is string {
  return typeof v === "string" && lista.includes(v);
}

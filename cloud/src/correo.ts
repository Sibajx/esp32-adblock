import type { Env } from "./tipos";

export interface Contacto {
  nombre?: string;
  correo: string;
  tema: string;
  mensaje: string;
  diagnostico?: string;
}

/** Nombre de cada tema para el asunto (fijo: nunca lleva texto de quien escribe). */
export const ETIQUETAS_TEMA: Readonly<Record<string, string>> = Object.freeze({
  no_instala: "No puedo instalarlo",
  no_bloquea: "No bloquea anuncios",
  bloqueo_de_mas: "Bloquea algo que no debería",
  mi_modem: "Ayuda con mi módem",
  otra: "Otra cosa",
});

export const TEXTO_RESPUESTA = [
  "Hola.",
  "",
  "Recibimos tu mensaje y te contestamos pronto.",
  "",
  "Mientras tanto, esto ayuda en los casos más comunes:",
  "",
  "Guía del router: https://siblock.s-dev.win/guia-router.html",
  "Problemas comunes: https://siblock.s-dev.win/obstaculos.html",
  'Si un sitio no abre, en tu panel puedes ponerlo en "Permitir".',
  "",
  "Gracias por usar SiBlock.",
].join("\n");

/** Texto plano: sin etiquetas, con lo básico decodificado y sin controles (salvo el salto de línea). */
export function sinHtml(s: string): string {
  return s
    .replace(/\r\n?/g, "\n")
    .replace(/<[^>]*>/g, "")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&amp;/g, "&")
    .replace(/[\u0000-\u0009\u000b-\u001f\u007f]/g, "");
}

export function correoAlDueno(d: Contacto, env: Env) {
  const partes = [
    `Tema: ${ETIQUETAS_TEMA[d.tema] ?? d.tema}`,
    `Nombre: ${d.nombre ? sinHtml(d.nombre) : "(sin nombre)"}`,
    `Correo: ${d.correo}`,
    "",
    "Mensaje:",
    sinHtml(d.mensaje),
  ];
  if (d.diagnostico) partes.push("", "Diagnóstico de su SiBlock:", sinHtml(d.diagnostico));
  return {
    from: { email: env.CONTACTO_REMITENTE, name: "SiBlock" },
    to: env.CONTACTO_DESTINO,
    replyTo: d.correo,
    subject: `[SiBlock] ${ETIQUETAS_TEMA[d.tema] ?? "Mensaje"}`,
    text: partes.join("\n"),
  };
}

/** Respuesta automática: texto fijo, nunca repite nada de lo que escribió la persona. */
export function respuestaAutomatica(correo: string, env: Env) {
  return {
    from: { email: env.CONTACTO_REMITENTE, name: "SiBlock" },
    to: correo,
    subject: "Recibimos tu mensaje",
    text: TEXTO_RESPUESTA,
  };
}

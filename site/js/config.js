// Configuración pública de la página. CI reescribe los tres últimos valores al publicar
// (token de Cloudflare Web Analytics, clave pública de Turnstile y enlace de apoyo).
// Vacío = no se carga nada y no se muestra el botón de apoyo.
export const CONFIG = Object.freeze({ api: '/api/v1', analiticaToken: '', turnstileClave: '', apoyoUrl: '' });

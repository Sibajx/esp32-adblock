// Contáctanos. El panel puede mandar su diagnóstico en el fragmento (#diag=<base64url de JSON>):
// el fragmento no viaja a ningún servidor, y solo se adjunta si la persona marca la casilla.
// Todo lo que viene del fragmento se revisa y se muestra con textContent.
import { CONFIG } from './config.js';
import { enviar, mensajeDeError, montarTurnstile } from './nube.js';

export const TEMAS = Object.freeze([
  { id: 'no_instala', nombre: 'No puedo instalar SiBlock' },
  { id: 'no_bloquea', nombre: 'No quita los anuncios' },
  { id: 'bloqueo_de_mas', nombre: 'Bloquea algo que no debía' },
  { id: 'mi_modem', nombre: 'No sé cómo hacerlo en mi módem' },
  { id: 'otra', nombre: 'Otra cosa' },
].map((t) => Object.freeze(t)));

const MAX_FRAGMENTO = 600;
const CONTROLES = /[\u0000-\u001f\u007f]/g;
const CODIGO_RE = /^[a-z_]{1,40}$/;
const CORREO_RE = /^[^\s@\u0000-\u001f\u007f]+@[^\s@\u0000-\u001f\u007f]+$/;
const ETIQUETAS = Object.freeze([['v', 'Versión'], ['heap', 'Memoria libre'], ['listErr', 'Último error al bajar la lista']]);
// Los avisos de «Algo no funciona» del panel, en palabras simples. Un código que no está aquí sale como «otro aviso».
const AVISOS = Object.freeze({
  usa_siblock: 'este aparato usa SiBlock',
  navegador_dns_propio: 'el navegador usa su propio DNS',
  no_usa_siblock: 'este aparato no pregunta a SiBlock',
  ipv6: 'el módem reparte otro DNS por IPv6',
  sin_consultas: 'el aparato no preguntó nada en 10 minutos',
  dns_cifrado: 'el aparato usa un DNS cifrado',
  private_relay: 'el aparato usa iCloud Private Relay',
  en_pausa: 'SiBlock estaba en pausa',
  aparato_en_pausa: 'este aparato tenía una pausa propia',
  horario_todo: 'un horario le cortaba todo internet al aparato',
  horario_activo: 'un horario le cortaba redes sociales o juegos al aparato',
  sin_lista: 'SiBlock no tenía su lista',
  sin_internet: 'SiBlock no llegaba a internet',
  router_dns: 'todo llega desde el módem',
  todo_bien: 'nada raro',
  windows_dns_cifrado: 'pasos de Windows: DNS cifrado',
  antivirus_dns: 'pasos: antivirus con DNS seguro',
  vpn: 'pasos: VPN',
  apple_perfil_dns: 'pasos de iPhone o Mac: perfil de DNS',
  android_dns_privado: 'pasos de Android: DNS privado',
  revisar_aparato: 'pasos: revisar VPN, antivirus y DNS cifrado',
});

function valorDe(clave, v) {
  if (clave === 'heap' && typeof v === 'number') return `${Math.round(v / 1024)} KB`;
  return v;
}

function aviso(codigo) {
  return Object.hasOwn(AVISOS, codigo) ? AVISOS[codigo] : `otro aviso (${codigo})`;
}

function valorCorto(x) {
  if (typeof x === 'number') return Number.isSafeInteger(x) && x >= 0 ? x : undefined;
  if (typeof x !== 'string') return undefined;
  const s = x.replace(CONTROLES, '');
  return s.length > 0 && s.length <= 100 ? s : undefined;
}

function decodificar(b64url) {
  if (!/^[A-Za-z0-9_-]+$/.test(b64url) || b64url.length % 4 === 1) return null;
  const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/') + '='.repeat((4 - (b64url.length % 4)) % 4);
  const bin = atob(b64);
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
}

/** "#diag=<base64url>" → {v, heap, listErr, codes} (solo esas claves y solo valores sanos) o null. */
export function diagDesdeFragmento(hash) {
  if (typeof hash !== 'string' || !hash.startsWith('#diag=')) return null;
  const valor = hash.slice('#diag='.length);
  if (valor.length === 0 || valor.length > MAX_FRAGMENTO) return null;
  let crudo;
  try {
    crudo = JSON.parse(decodificar(valor));
  } catch {
    return null;
  }
  if (!crudo || typeof crudo !== 'object' || Array.isArray(crudo)) return null;
  const diag = {};
  for (const [clave] of ETIQUETAS) {
    if (!Object.hasOwn(crudo, clave)) continue;
    const v = valorCorto(crudo[clave]);
    if (v !== undefined) diag[clave] = v;
  }
  if (Object.hasOwn(crudo, 'codes') && Array.isArray(crudo.codes)) {
    const codes = crudo.codes.filter((c) => typeof c === 'string' && CODIGO_RE.test(c)).slice(0, 20);
    if (codes.length) diag.codes = codes;
  }
  return Object.keys(diag).length ? diag : null;
}

/** Líneas "Versión: …" en palabras simples, para mostrar y adjuntar (texto, nunca HTML). */
export function textoDiag(diag) {
  if (!diag || typeof diag !== 'object') return '';
  const lineas = [];
  for (const [clave, etiqueta] of ETIQUETAS) {
    if (diag[clave] !== undefined) lineas.push(`${etiqueta}: ${valorDe(clave, diag[clave])}`);
  }
  if (Array.isArray(diag.codes) && diag.codes.length) lineas.push(`Avisos: ${diag.codes.map(aviso).join('; ')}`);
  return lineas.join('\n');
}

/** Mismas reglas que el Worker. → {ok:true, cuerpo} | {ok:false, campo}. */
export function validarContacto(campos) {
  const c = campos && typeof campos === 'object' ? campos : {};
  const nombre = typeof c.nombre === 'string' ? c.nombre.trim() : '';
  if (nombre.length > 80 || /[\u0000-\u001f\u007f]/.test(nombre)) return { ok: false, campo: 'nombre' };
  const correo = typeof c.correo === 'string' ? c.correo.trim() : '';
  if (correo.length > 254 || !CORREO_RE.test(correo)) return { ok: false, campo: 'correo' };
  if (typeof c.tema !== 'string' || !TEMAS.some((t) => t.id === c.tema)) return { ok: false, campo: 'tema' };
  const mensaje = typeof c.mensaje === 'string' ? c.mensaje.trim() : '';
  if (mensaje.length < 10 || mensaje.length > 2000) return { ok: false, campo: 'mensaje' };

  const cuerpo = {};
  if (nombre) cuerpo.nombre = nombre;
  cuerpo.correo = correo;
  cuerpo.tema = c.tema;
  cuerpo.mensaje = mensaje;
  const diagnostico = c.adjuntar === true ? textoDiag(c.diag) : '';
  if (diagnostico) cuerpo.diagnostico = diagnostico;
  if (typeof c.turnstile === 'string' && c.turnstile) cuerpo.turnstile = c.turnstile;
  return { ok: true, cuerpo };
}

/** Muestra el diagnóstico (si llegó) y la casilla para adjuntarlo. */
export function pintarDiag(doc, diag) {
  const caja = doc.getElementById('diag');
  const texto = doc.getElementById('diag-texto');
  if (!caja || !texto) return;
  texto.textContent = textoDiag(diag);
  caja.hidden = !diag;
}

const AVISOS_CAMPO = Object.freeze({
  nombre: 'El nombre puede tener hasta 80 letras, en una sola línea.',
  correo: 'Revisa tu correo: debe verse como nombre@ejemplo.com.',
  tema: 'Elige de qué se trata.',
  mensaje: 'Cuéntanos un poco más: el mensaje debe tener entre 10 y 2000 letras.',
});

/** Prepara la página. */
export function iniciar(doc, win, config = CONFIG) {
  const form = doc.getElementById('form-contacto');
  if (!form) return;
  const diag = diagDesdeFragmento(win.location.hash);
  if (win.location.hash) {
    try { win.history.replaceState(null, '', win.location.pathname + win.location.search); } catch { /* sin historial */ }
  }
  pintarDiag(doc, diag);

  const tema = doc.getElementById('tema');
  const op = (valor, texto) => {
    const o = doc.createElement('option');
    o.value = valor;
    o.textContent = texto;
    return o;
  };
  tema.replaceChildren(op('', 'Elige de qué se trata'), ...TEMAS.map((t) => op(t.id, t.nombre)));

  const estado = doc.getElementById('contacto-estado');
  const boton = doc.getElementById('mandar-contacto');
  const ts = montarTurnstile(doc, doc.getElementById('turnstile-contacto'), config);
  if (!ts.listo) {
    const aviso = doc.getElementById('aviso-listo');
    if (aviso) aviso.hidden = false;
    boton.disabled = true;
  }

  let ocupado = false;
  form.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    if (ocupado) return;
    const fd = new FormData(form);
    const r = validarContacto({
      nombre: fd.get('nombre'),
      correo: fd.get('correo'),
      tema: fd.get('tema'),
      mensaje: fd.get('mensaje'),
      adjuntar: fd.get('adjuntar') === 'si',
      diag,
      turnstile: ts.token(),
    });
    if (!r.ok) {
      estado.textContent = AVISOS_CAMPO[r.campo];
      const campo = doc.getElementById(r.campo);
      if (campo && typeof campo.focus === 'function') campo.focus();
      return;
    }
    if (!r.cuerpo.turnstile) {
      estado.textContent = 'Marca la casilla de abajo para mostrar que eres una persona.';
      return;
    }
    ocupado = true;
    boton.disabled = true;
    estado.textContent = 'Mandando…';
    const res = await enviar('/contacto', r.cuerpo);
    if (res.ok) {
      form.reset();
      form.hidden = true;
      estado.textContent = 'Recibimos tu mensaje. Te contestamos pronto.';
    } else {
      estado.textContent = mensajeDeError(res.error);
      boton.disabled = false;
    }
    ts.reiniciar();
    ocupado = false;
  });
}

if (typeof document !== 'undefined' && typeof window !== 'undefined') {
  iniciar(document, window);
}

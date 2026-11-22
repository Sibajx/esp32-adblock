// Textos que ve la persona, por idioma. El idioma sale de <html lang="…"> de la página.
// Si falta una clave en inglés, se usa la de español.
const es = Object.freeze({
  fallo:
    'Recién instalada, tu SiBlock tarda un par de minutos en prepararse. Espera un poco, ' +
    'desconecta y vuelve a conectar la placa y prueba otra vez. También la ves en tu panel.',
  preparando:
    'Tu SiBlock se está preparando (tarda un par de minutos la primera vez). ' +
    'Espera aquí sin desconectarla.',
  leyendo: 'Leyendo la placa… no la desconectes.',
  sinPanel:
    'Tu SiBlock todavía se está conectando a tu WiFi. Espera un minuto y pulsa el botón otra vez. ' +
    'También puedes probar abriendo http://siblock.local en la computadora.',
  ocupada: 'La placa está ocupada. Cierra la ventana del instalador y prueba otra vez.',
  apoyo: 'Invítame un café',
});

const en = Object.freeze({
  fallo:
    'Right after installing, your SiBlock needs a couple of minutes to get ready. Wait a bit, ' +
    'unplug the board, plug it back in and try again. You can also see it in your panel.',
  preparando:
    'Your SiBlock is getting ready (the first time takes a couple of minutes). ' +
    'Wait here and keep it plugged in.',
  leyendo: 'Reading the board… keep it plugged in.',
  sinPanel:
    'Your SiBlock is still joining your WiFi. Wait a minute and press the button again. ' +
    'You can also try opening http://siblock.local on the computer.',
  ocupada: 'The board is busy. Close the installer window and try again.',
  apoyo: 'Buy me a coffee',
});

export const TEXTOS = Object.freeze({ es, en });

/** 'en' si <html lang> empieza con "en" (en, en-US…); si no, 'es'. */
export function idioma(doc = globalThis.document) {
  const lang = doc?.documentElement?.lang;
  return typeof lang === 'string' && /^en(-|$)/i.test(lang) ? 'en' : 'es';
}

/** El texto de `clave` en el idioma de la página, o el español si falta. '' si no existe. */
export function texto(clave, doc = globalThis.document) {
  const lengua = TEXTOS[idioma(doc)];
  if (Object.hasOwn(lengua, clave)) return lengua[clave];
  return Object.hasOwn(TEXTOS.es, clave) ? TEXTOS.es[clave] : '';
}

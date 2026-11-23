// Panel de la placa (src/web_page.h): las tarjetas «Compartir con SiBlock» y «Avisos por Telegram».
// La consulta de cada minuto no puede borrar lo que la persona está eligiendo y todavía no guardó.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const PANEL = readFileSync(new URL('../../src/web_page.h', import.meta.url), 'utf8');
const desde = PANEL.indexOf('// ---- Compartir con SiBlock');
const hasta = PANEL.indexOf('// Escríbenos:');
assert.ok(desde > 0 && hasta > desde, 'no se encontró el código de las tarjetas en web_page.h');
const CODIGO = PANEL.slice(desde, hasta);

function elemento() {
  const oyentes = {};
  return {
    checked: false, value: '', textContent: '', hidden: false, disabled: false, href: '',
    addEventListener(ev, f) { (oyentes[ev] ??= []).push(f); },
    tocar(ev) { for (const f of oyentes[ev] ?? []) f(); },
  };
}

/** Carga el código de las tarjetas con un DOM y una red de mentira. */
function panel(red) {
  const els = {};
  const $ = (id) => (els[id] ??= elemento());
  const document = { activeElement: null };
  const f = new Function('$', 'get', 'post', 'fmt', 'el', 'tk', 'document', `${CODIGO}
    return { pintaTg, cargaTg, pintaNube, cargaNube, opcionNube,
             estado: () => ({ tgSucio, tgOcupado, nubeOcupado }) };`);
  const api = f($, red.get, red.post, (n) => String(n), () => elemento(), 'sesion', document);
  return { $, api };
}

const TG = { on: true, reboot: true, listFail: false, weekly: false, chat: '123', tokenSaved: true, last: 'never', pending: false };

test('Telegram: una casilla cambiada sin guardar no se pisa al refrescar; al guardar, sí se repinta', async () => {
  let servidor = { ...TG };
  const enviados = [];
  const red = {
    get: async () => ({ ...servidor }),
    post: async (ruta, campos) => {
      enviados.push([ruta, campos]);
      servidor = { ...servidor, reboot: campos.reboot === '1', listFail: campos.listFail === '1', weekly: campos.weekly === '1' };
      if (campos.chat) servidor.chat = campos.chat;
      return { ...servidor };
    },
  };
  const { $, api } = panel(red);
  await api.cargaTg();
  assert.equal($('tgr').checked, true);
  assert.equal($('tgchat').value, '123');
  // La persona quita «se reinició», marca «resumen semanal» y escribe otro chat… y llega la consulta.
  $('tgr').checked = false; $('tgr').tocar('change');
  $('tgw').checked = true; $('tgw').tocar('change');
  $('tgchat').value = '999'; $('tgchat').tocar('input');
  await api.cargaTg();
  assert.equal($('tgr').checked, false, 'la consulta deshizo la casilla');
  assert.equal($('tgw').checked, true);
  assert.equal($('tgchat').value, '999', 'la consulta borró el chat escrito');
  assert.equal(api.estado().tgSucio, true);
  // «Guardar» manda lo elegido y deja la tarjeta limpia.
  $('tgtok').value = '';
  await $('tgsave').onclick();
  assert.deepEqual(enviados.at(-1), ['/api/telegram', { reboot: '0', listFail: '0', weekly: '1', chat: '999' }]);
  assert.equal(api.estado().tgSucio, false);
  // Ya sin cambios pendientes, lo que cambie en la placa se ve.
  servidor.reboot = true;
  await api.cargaTg();
  assert.equal($('tgr').checked, true);
});

test('Telegram: si «Guardar» falla, lo elegido se queda en pantalla', async () => {
  const red = {
    get: async () => ({ ...TG }),
    post: async () => { throw new Error('guardar'); },
  };
  const { $, api } = panel(red);
  await api.cargaTg();
  $('tgl').checked = true; $('tgl').tocar('change');
  await $('tgsave').onclick();
  assert.equal($('tgl').checked, true);
  assert.match($('tgmsg').textContent, /No se pudo guardar/);
  assert.equal(api.estado().tgSucio, true);
});

test('Telegram: los nombres nuevos del último envío tienen texto en el panel', async () => {
  for (const [last, frase] of [['poca_ram', /memoria libre/], ['sin_datos', /token o el número de chat/], ['sin_internet', /cada minuto/]]) {
    const { $, api } = panel({ get: async () => ({ ...TG, last }), post: async () => ({}) });
    await api.cargaTg();
    assert.match($('tglast').textContent, frase, last);
  }
});

test('Compartir: mientras se guarda una opción, la consulta no la deshace', async () => {
  let servidor = { share: false, versionCheck: false, community: false };
  let soltar;
  const red = {
    get: async () => ({ ...servidor }),
    post: (ruta, campos) => new Promise((ok) => {
      soltar = () => { servidor = { ...servidor, share: campos.share === '1' }; ok({ ...servidor }); };
    }),
  };
  const { $, api } = panel(red);
  await api.cargaNube();
  assert.equal($('share').checked, false);
  $('share').checked = true;
  const guardando = api.opcionNube('share', $('share'));
  assert.equal(api.estado().nubeOcupado, 1);
  await api.cargaNube();  // llega la consulta de cada minuto con lo viejo
  assert.equal($('share').checked, true, 'la consulta deshizo el toque');
  soltar();
  await guardando;
  assert.equal(api.estado().nubeOcupado, 0);
  assert.equal($('share').checked, true);
  assert.equal($('nmsg').textContent, 'Listo: guardado.');
  servidor.share = false;  // ya guardado: lo que diga la placa se ve
  await api.cargaNube();
  assert.equal($('share').checked, false);
});

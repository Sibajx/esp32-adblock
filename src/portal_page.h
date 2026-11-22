// Página del modo de configuración por WiFi (se incluye dentro de web.cpp). Todo lo que viene de
// la red (nombres de WiFi) se pinta con textContent, nunca con innerHTML.
#pragma once

const char kPortalPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configura el WiFi de SiBlock</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--fg:#1d2129;--mut:#667085;--acc:#155eef;--warn:#b54708;--ok:#067647;--line:#e4e7ec;--btn:#f2f4f7}
@media (prefers-color-scheme:dark){:root{--bg:#101114;--card:#1a1c21;--fg:#eceef2;--mut:#98a2b3;--line:#2a2d34;--btn:#24272e;--acc:#84adff;--warn:#fdb022;--ok:#47cd89}}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0 auto;font:16px/1.5 system-ui,sans-serif;background:var(--bg);color:var(--fg);padding:16px;max-width:520px}
h1{font-size:21px;margin:4px 0 6px}p{margin:6px 0 14px;color:var(--mut)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px;margin:10px 0}
ul{list-style:none;padding:0;margin:0}li+li{border-top:1px solid var(--line)}
.net{display:flex;width:100%;gap:8px;align-items:center;justify-content:space-between;text-align:left;border:0;background:none;padding:12px 4px;border-radius:0;font:inherit;color:inherit;cursor:pointer}
.net[aria-pressed=true]{color:var(--acc);font-weight:650}.net span:first-child{overflow-wrap:anywhere}
.sig{color:var(--mut);font-size:13px;white-space:nowrap}li.sig{white-space:normal;padding:12px 4px}
label{display:block;font-size:14px;color:var(--mut);margin:10px 0 4px}
input,button{font:inherit;width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--fg)}
button.main{background:var(--acc);border-color:var(--acc);color:#fff;font-weight:650;margin-top:12px;cursor:pointer}
button.main:disabled{opacity:.5}.small{width:auto;padding:6px 10px;font-size:14px;background:var(--btn);cursor:pointer}
#msg{margin-top:10px;min-height:1.5em}.warn{color:var(--warn)}.good{color:var(--ok)}
</style></head><body>
<h1>Configura el WiFi de SiBlock</h1>
<p>Elige el WiFi de tu casa y escribe su contraseña. SiBlock se conecta solo y esta red desaparece.</p>
<div class="card"><div style="display:flex;justify-content:space-between;align-items:center;gap:8px">
<strong>Redes cerca</strong><button id="re" class="small">Buscar otra vez</button></div>
<ul id="nets"><li class="sig">Buscando redes…</li></ul></div>
<p>¿No ves tu red? SiBlock solo se conecta a WiFi de 2.4 GHz. Casi todos los módems lo tienen; a veces aparece como otra red con un nombre parecido.</p>
<div class="card">
<label for="ssid">Nombre del WiFi</label><input id="ssid" maxlength="32" autocapitalize="off" autocomplete="off" spellcheck="false">
<label for="pass">Contraseña del WiFi</label><input id="pass" type="password" maxlength="63" autocomplete="off">
<button id="go" class="main">Conectar</button><div id="msg" role="status"></div></div>
<script>
const $=id=>document.getElementById(id);
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
const H={'X-Requested-With':'adblock'};
const bytes=s=>new TextEncoder().encode(s).length;
function say(t,c){$('msg').textContent=t;$('msg').className=c||''}
function bars(r){return r>-60?'señal fuerte':r>-75?'señal media':'señal débil'}
async function scan(){$('re').disabled=true;
 try{const r=await fetch('/api/wifi/scan',{headers:H});const j=await r.json();
  const items=(j.networks||[]).map(n=>{const li=el('li'),b=el('button','net');b.type='button';b.setAttribute('aria-pressed','false');
   b.append(el('span','',n.ssid),el('span','sig',(n.open?'abierta · ':'')+bars(n.rssi)));
   b.onclick=()=>{document.querySelectorAll('.net').forEach(x=>x.setAttribute('aria-pressed','false'));b.setAttribute('aria-pressed','true');
    $('ssid').value=n.ssid;$('pass').value='';$('pass').disabled=n.open;if(!n.open)$('pass').focus()};
   li.append(b);return li});
  $('nets').replaceChildren(...(items.length?items:[el('li','sig','No se ven redes. Acerca SiBlock al módem y busca otra vez. Recuerda: solo ve WiFi de 2.4 GHz.')]))}
 catch(e){$('nets').replaceChildren(el('li','sig','No se pudieron buscar redes. Prueba otra vez.'))}
 $('re').disabled=false}
$('re').onclick=scan;
$('ssid').oninput=()=>{$('pass').disabled=false};
$('go').onclick=async()=>{const s=$('ssid').value,p=$('pass').disabled?'':$('pass').value;
 if(!s||bytes(s)>32){say('Escribe el nombre del WiFi.','warn');return}
 if(p&&(bytes(p)<8||bytes(p)>63)){say('La contraseña del WiFi tiene entre 8 y 63 caracteres.','warn');return}
 $('go').disabled=true;say('Guardando…');
 try{const r=await fetch('/api/wifi',{method:'POST',headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H),body:new URLSearchParams({ssid:s,pass:p})});
  let j={};try{j=await r.json()}catch(e){}
  if(!r.ok)throw new Error(j.error||'No se pudo guardar. Prueba otra vez.');
  say('Listo. SiBlock se está conectando a tu WiFi. Esta red se va a cerrar; vuelve a tu WiFi de siempre. Si en unos minutos esta red aparece otra vez, la contraseña no era la correcta.','good')}
 catch(e){say(e.message,'warn');$('go').disabled=false}};
scan();
</script></body></html>)HTML";

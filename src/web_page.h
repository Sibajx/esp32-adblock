// Página del panel (se incluye dentro de web.cpp). Todo lo que viene de la red se pinta con
// textContent, nunca con innerHTML.
#pragma once

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SiBlock</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--fg:#1d2129;--mut:#667085;--acc:#d92d20;--ok:#12b76a;--warn:#b54708;--line:#e4e7ec;--btn:#f2f4f7;--cq:#7d8799;--cb:#d92d20}
@media (prefers-color-scheme:dark){:root{--bg:#101114;--card:#1a1c21;--fg:#eceef2;--mut:#98a2b3;--line:#2a2d34;--btn:#24272e;--acc:#f97066;--ok:#32d583;--warn:#fdb022;--cq:#7a8394;--cb:#f04438}}
*{box-sizing:border-box}[hidden]{display:none!important}body{margin:0 auto;font:16px/1.45 system-ui,sans-serif;background:var(--bg);color:var(--fg);padding:16px;max-width:780px;overflow-wrap:anywhere}
h1{font-size:20px;margin:4px 0 14px}h2{font-size:15px;margin:22px 0 8px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.n{font-size:26px;font-weight:650;font-variant-numeric:tabular-nums}.l{color:var(--mut);font-size:13px}
ul,ol{list-style:none;padding:0;margin:0}li{display:flex;gap:8px;align-items:center;padding:6px 0;border-bottom:1px solid var(--line);overflow-wrap:anywhere}
li:last-child{border-bottom:0}.tag{font-size:12px;font-weight:600;min-width:70px}
.lista,.tuyo{color:var(--acc)}.ok,.permitido{color:var(--ok)}.pausa{color:var(--warn)}
.d{flex:1;min-width:0}button,input{font:inherit;font-size:16px;padding:7px 12px;border-radius:8px;border:1px solid var(--line);background:var(--btn);color:var(--fg)}
button{cursor:pointer;min-height:44px}input{min-height:44px}button[aria-pressed=true]{border-color:var(--fg);font-weight:600}button:disabled{opacity:.5;cursor:default}input{background:var(--card);min-width:0}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.grow{flex:1 1 200px}
#msg,#status{color:var(--mut);font-size:13px}#status{margin:10px 0}.warn{color:var(--warn);font-weight:600}
.x{padding:2px 12px;font-size:14px;min-width:44px}
.scroll{overflow-x:auto;-webkit-overflow-scrolling:touch}
#cmp{font-weight:600;font-size:17px;margin:0 0 8px}.ico{flex:none;width:1.4em;text-align:center}
#hallazgos{margin-top:8px}a{color:inherit}#hallazgos a{margin-left:6px;white-space:nowrap}
.chk{display:flex;gap:10px;align-items:center;min-height:44px;cursor:pointer}.chk input{width:22px;height:22px;min-height:0;flex:none;margin:0}
#grafica{display:block;width:100%;height:170px;margin-top:8px}.bq{fill:var(--cq)}.bb{fill:var(--cb)}.hit{fill:transparent}.ax{fill:var(--mut);font-size:11px}.base{stroke:var(--line)}
.ley{display:flex;gap:14px;font-size:13px;color:var(--mut);margin-top:6px}.ley i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:5px}.ley .q{background:var(--cq)}.ley .k{background:var(--cb)}
.n2{font-variant-numeric:tabular-nums;color:var(--mut)}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}th,td{text-align:right;padding:6px 8px;white-space:nowrap;border-bottom:1px solid var(--line)}th:first-child,td:first-child{text-align:left}th{color:var(--mut);font-weight:500;font-size:13px}tr:last-child td{border-bottom:0}
.ip{color:var(--mut);font-size:12px;font-variant-numeric:tabular-nums}
.stack{display:grid;gap:8px;max-width:420px}.big{font:600 20px/1.3 ui-monospace,monospace;letter-spacing:.04em;overflow-wrap:anywhere}
select{font:inherit;font-size:16px;padding:7px 12px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg);min-height:44px;max-width:100%}
.fld{display:grid;gap:4px}.fld>span{font-size:14px}#upd{margin:0 0 12px;border-color:var(--warn)}#rep,#esc{display:inline-block;padding:10px 0}.nb{display:flex;flex-wrap:wrap;gap:6px;align-items:center;width:100%}
footer{color:var(--mut);font-size:12px;margin:28px 0 8px}
.horario{color:var(--acc)}.pausa-aparato{color:var(--warn)}.dv{flex-wrap:wrap}.dv .d small{display:block;color:var(--mut);font-size:13px}
.me{font-size:12px;font-weight:600;color:var(--ok);margin-left:6px}.days{display:flex;gap:2px;flex-wrap:wrap}.days .chk{min-width:42px;gap:3px}
fieldset{border:0;padding:0;margin:0;display:grid;gap:4px}legend{font-size:14px;padding:0;margin-bottom:4px}
</style></head><body>
<h1>SiBlock</h1>
<div id="upd" class="card" role="status" hidden>Hay una versión nueva de SiBlock (<span id="updv"></span>). Se instala desde la página con el cable USB. <a href="https://siblock.s-dev.win/#paso-instalar" target="_blank" rel="noopener noreferrer">Ver cómo</a></div>
<div id="comprobar" class="card" style="margin-bottom:12px">
<p id="cmp" aria-live="polite">Revisando si este aparato usa SiBlock…</p>
<div class="row"><button id="recheck">Comprobar otra vez</button><button id="algo">Algo no funciona</button></div>
<ul id="hallazgos" aria-live="polite"></ul>
</div>
<div class="grid">
<div class="card"><div class="n" id="q">–</div><div class="l">consultas</div></div>
<div class="card"><div class="n" id="b">–</div><div class="l">bloqueadas</div></div>
<div class="card"><div class="n" id="p">–</div><div class="l">% bloqueado</div></div>
<div class="card"><div class="n" id="d">–</div><div class="l">sitios de anuncios conocidos</div></div>
<div class="card"><div class="n" id="c">–</div><div class="l">contestadas al instante</div></div>
</div>
<p id="hcut" class="l" hidden></p>
<div id="status"></div>
<details id="tecd" class="l"><summary>Detalles técnicos</summary><span id="tec"></span></details>

<div id="setup" class="card stack" hidden>
<h2 style="margin:0">Crea tu contraseña</h2>
<span class="l">Con ella entras a este panel. Usa entre 12 y 64 caracteres y guárdala en un lugar seguro.</span>
<input id="np1" type="password" placeholder="Contraseña nueva" autocomplete="new-password">
<input id="np2" type="password" placeholder="Repite la contraseña" autocomplete="new-password">
<button id="mk">Guardar</button><span id="smsg" class="l"></span></div>

<div id="closed" class="card" hidden><span class="warn">Pulsa el botón BOOT de tu SiBlock una vez y vuelve a intentar.</span>
<div class="l">Por seguridad, la contraseña solo se puede crear en los 30 minutos después de encender SiBlock o de pulsar ese botón.</div></div>

<div id="login" class="card row" hidden>
<input id="pw" class="grow" type="password" placeholder="Contraseña del panel" autocomplete="current-password">
<button id="go">Entrar</button><span id="lmsg" class="l"></span>
<p class="l" style="flex-basis:100%;margin:0">¿Olvidaste tu contraseña? Mantén el botón BOOT de la placa 10 segundos: se borra la configuración (tus reglas se quedan) y vuelves a darle tu WiFi. <a href="https://siblock.s-dev.win/#traba" target="_blank" rel="noopener noreferrer">Ver cómo</a></p></div>

<div id="admin" hidden>
<h2>Bloqueo</h2>
<div class="row"><span id="pst" class="grow"></span>
<button data-min="5">Pausar 5 min</button><button data-min="30">30 min</button><button data-min="60">1 h</button>
<button data-min="0" id="resume">Reanudar</button><button id="up">Actualizar lista</button></div>
<div class="card" style="margin-top:8px">
<label class="chk"><input type="checkbox" id="relay"><span>Apagar iCloud Private Relay en esta casa</span></label>
<span class="l">Safari en iPhone y Mac dejará de usar Private Relay en tu WiFi: SiBlock podrá bloquear sus anuncios, pero pierdes la privacidad extra de Private Relay.</span>
<div id="rmsg" class="l"></div></div>
<div class="card" style="margin-top:8px">
<label class="chk" id="lsc"><input type="checkbox" id="lscam"><span>Cuidar de estafas y tiendas falsas (recomendado para abuelos y niños)</span></label>
<label class="chk" id="lse"><input type="checkbox" id="lspa"><span>Quitar algunos anuncios más de sitios en español (por ahora son pocos)</span></label>
<span class="l">Al cambiarlas, SiBlock baja su lista otra vez; tarda unos minutos.</span>
<div id="lsmsg" class="l" aria-live="polite"></div><div id="lskip" class="warn" hidden></div></div>
<h2>Aparatos</h2>
<div class="card"><span class="l">Ponle nombre a cada aparato para reconocerlo, pausar el bloqueo solo en él o ponerle horarios.</span>
<ul id="devs"></ul><div id="dmsg" class="l" aria-live="polite"></div></div>
<h2>Horarios</h2>
<div class="card stack" style="max-width:none">
<span class="l">Bloquea redes sociales, juegos o todo internet en ciertos aparatos y a ciertas horas (por ejemplo, a la hora de dormir). Los horarios siguen aunque pongas una pausa. Es una ayuda, no un candado: un aparato que cambia su dirección en el WiFi (los celulares lo hacen si tienen la «dirección privada» puesta), usa DNS cifrado o una VPN se lo salta.</span>
<p id="sinhora" class="warn" hidden>Los horarios empiezan cuando SiBlock tenga la hora de internet.</p>
<p id="tzaviso" class="warn" hidden>La hora de tu casa cambió. Guarda otra vez un horario para corregirla.</p>
<ul id="hlist"></ul>
<fieldset><legend>Aparatos</legend><div id="hdev" class="row"></div></fieldset>
<fieldset><legend>Qué bloquear</legend><div class="row">
<label class="chk"><input type="checkbox" id="hg1"><span>Redes sociales</span></label>
<label class="chk"><input type="checkbox" id="hg2"><span>Juegos</span></label>
<label class="chk"><input type="checkbox" id="hg4"><span>Todo internet</span></label></div></fieldset>
<fieldset><legend>Días</legend><div class="days" id="hdays"></div></fieldset>
<div class="row"><label class="fld"><span>Desde</span><input type="time" id="hini" value="21:00"></label>
<label class="fld"><span>Hasta</span><input type="time" id="hfin" value="07:00"></label></div>
<div class="row"><button id="hsave">Guardar horario</button><button id="hcancel" hidden>Cancelar</button><span id="hmsg" class="l" aria-live="polite"></span></div>
</div>
<h2>Compartir con SiBlock</h2>
<div class="card stack" style="max-width:none">
<span class="l">Apagado de fábrica. Tú decides si lo activas.</span>
<label class="chk"><input type="checkbox" id="share"><span>Compartir datos anónimos</span></label>
<span class="l">Si lo activas, tu SiBlock manda a siblock.s-dev.win una vez al día un número al azar de esta instalación (no dice quién eres), la versión de SiBlock y cuántos anuncios quitó ese día. Y, solo cuando tú lo mandes, la marca de tu módem, si pudiste cambiar el DNS y en qué menú, o el sitio que no debió bloquearse. Nunca manda tu IP, la MAC, nombres de aparatos ni qué sitios visitas. El servidor ve tu país y tu compañía de internet por la conexión, pero no guarda tu IP. Con esta opción apagada, SiBlock no manda nada tuyo a siblock.s-dev.win (solo revisa si hay versión nueva, si activas «Avisarme de versiones nuevas»).</span>
<label class="chk"><input type="checkbox" id="vchk"><span>Avisarme de versiones nuevas</span></label>
<span class="l">Si lo activas, SiBlock revisa una vez al día si hay una versión nueva. Solo pide un archivo público; no manda nada tuyo.</span>
<div id="nmsg" class="l" aria-live="polite"></div>
<h2 style="margin:8px 0 0">¿Qué módem tienes?</h2>
<span class="l">Ayuda a que otras personas con tu compañía de internet sepan dónde cambiar el DNS. Necesita «Compartir datos anónimos».</span>
<label class="fld"><span>Marca de tu módem o router</span><select id="mfab"><option value="" selected disabled>Elige la marca</option><option value="tp-link">TP-Link</option><option value="huawei">Huawei</option><option value="zte">ZTE</option><option value="technicolor">Technicolor</option><option value="arris">Arris</option><option value="netgear">Netgear</option><option value="asus">ASUS</option><option value="mikrotik">MikroTik</option><option value="proveedor">El módem que me dio mi proveedor</option><option value="no-se">No sé / otra</option></select></label>
<label class="fld"><span>¿Pudiste cambiar el DNS?</span><select id="mdns"><option value="" selected disabled>Elige una opción</option><option value="1">Sí</option><option value="0">No</option></select></label>
<label class="fld"><span>¿En qué menú?</span><select id="mmenu"><option value="" selected disabled>Elige el menú</option><option value="internet_wan">Internet o WAN</option><option value="red_local">Red local, LAN o DHCP</option><option value="avanzado">Avanzado</option><option value="otro">Otro o no lo encontré</option></select></label>
<div class="row"><button id="msend">Mandar</button><span id="mmsg" class="l" aria-live="polite"></span></div>
<div class="row"><a id="rep" href="https://siblock.s-dev.win/reporte.html" target="_blank" rel="noopener noreferrer">Reportar desde la página</a><a id="esc" href="https://siblock.s-dev.win/contacto.html" target="_blank" rel="noopener noreferrer">Escríbenos</a></div>
</div>
<h2>Mis reglas</h2>
<div class="row"><input id="dom" class="grow" placeholder="ejemplo.com" autocapitalize="off" spellcheck="false">
<button id="blk">Bloquear</button><button id="alw">Permitir</button></div>
<div class="card" style="margin-top:8px"><ul id="rules"></ul></div>
<div id="msg"></div>
<h2>Respaldo</h2>
<div class="card stack" style="max-width:none">
<span class="l">El respaldo guarda tus reglas, los nombres de los aparatos, los horarios, las listas elegidas y la opción de Private Relay. No guarda el WiFi ni la contraseña. Es un archivo privado: no lo compartas.</span>
<div class="row"><button id="bdown">Bajar respaldo</button>
<label class="fld"><span>Subir respaldo</span><input type="file" id="bfile" accept=".txt,text/plain"></label></div>
<p id="bres" aria-live="polite"></p>
<div class="row" id="bconf" hidden><button id="bsi">Sí, reemplazar</button><button id="bno">Cancelar</button></div>
</div>
</div>

<div id="priv" hidden>
<div id="uso">
<h2>Uso</h2>
<div class="card">
<div class="row" role="group" aria-label="Periodo"><button id="ver24" aria-pressed="true">24 horas</button><button id="ver7" aria-pressed="false">7 días</button></div>
<svg id="grafica" role="img" aria-label="Gráfica de uso" xmlns="http://www.w3.org/2000/svg"></svg>
<p id="usovacio" class="l" hidden>Todavía no hay datos. Aparecen cuando SiBlock tenga la hora de internet.</p>
<div class="ley" id="ley"><span><i class="q"></i>consultas</span><span><i class="k"></i>bloqueadas</span></div>
<p id="usores" class="l"></p>
<p id="ahorro" class="l" hidden></p>
<p class="l"><a href="https://siblock.s-dev.win/#luz" target="_blank" rel="noopener noreferrer">¿Qué dice la luz?</a></p>
</div></div>
<h2>Más bloqueados</h2>
<div class="card"><ol id="top"></ol></div>
<select id="motivos" hidden><option value="pagina_no_abre">Una página no abre</option><option value="app_no_funciona">Una app no funciona</option><option value="juego_no_funciona">Un juego no funciona</option><option value="otro">Otro</option></select>
<div id="saltan" hidden>
<h2>Aparatos que se saltan SiBlock</h2>
<div class="card scroll"><table><thead><tr><th>Aparato</th><th>qué usa</th><th>visto</th></tr></thead><tbody id="saltanb"></tbody></table>
<p class="l">Así no pasan por SiBlock y ven anuncios. <a href="https://siblock.s-dev.win/obstaculos.html" target="_blank" rel="noopener noreferrer">Ver qué hacer</a></p></div>
</div>
<h2>Últimas consultas</h2>
<div class="card"><ul id="rec"></ul></div>
<h2>Contraseña de respaldo del WiFi de SiBlock</h2>
<div class="card stack"><span class="l">Si un día cambias tu WiFi, SiBlock abre su propia red para que le des el nuevo. Estos son su nombre y su contraseña.</span>
<div>Red: <span id="apn" class="big">–</span></div><div>Contraseña: <span id="app" class="big">–</span></div>
<span class="warn">Guárdala: la necesitas si un día cambias tu WiFi.</span></div>

<h2>Cambiar contraseña</h2>
<div class="card stack">
<input id="cp0" type="password" placeholder="Contraseña actual" autocomplete="current-password">
<input id="cp1" type="password" placeholder="Contraseña nueva (12 a 64 caracteres)" autocomplete="new-password">
<input id="cp2" type="password" placeholder="Repite la nueva" autocomplete="new-password">
<button id="chg">Cambiar</button><span id="cmsg" class="l"></span></div>
<p class="l"><button id="out">Cerrar sesión</button></p>
</div>
<footer>SiBlock <span id="ver"></span></footer>
<script>
const $=id=>document.getElementById(id),fmt=n=>n.toLocaleString('es');
let tk='';try{tk=sessionStorage.getItem('tk')||'';sessionStorage.removeItem('pw')}catch(e){}
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
function hdr(extra){return Object.assign({'X-Requested-With':'adblock','X-Token':tk},extra||{})}
function drop(){tk='';try{sessionStorage.removeItem('tk')}catch(e){};showAuth()}
async function call(path,opt){const r=await fetch(path,opt);let j={};try{j=await r.json()}catch(e){}
 if(r.status==401&&tk)drop();if(!r.ok)throw new Error(j.error||('error '+r.status));return j}
const post=(path,body)=>call(path,{method:'POST',headers:hdr({'Content-Type':'application/x-www-form-urlencoded'}),body:new URLSearchParams(body||{})});
const get=path=>call(path,{headers:hdr()});
let pwSet=true,setupOk=false;
function showAuth(){$('setup').hidden=!!tk||pwSet||!setupOk;$('closed').hidden=!!tk||pwSet||setupOk;$('login').hidden=!!tk||!pwSet;$('admin').hidden=!tk;$('priv').hidden=!tk}
const b64=s=>btoa(String.fromCharCode(...new TextEncoder().encode(s)));
const bytes=s=>new TextEncoder().encode(s).length;
function login(j){tk=j.token;try{sessionStorage.setItem('tk',tk)}catch(e){};showAuth();tick();rules();backup();options();cargaNube();loadHist();prepDiag();loadDevs().then(loadSched)}
$('go').onclick=async()=>{const pw=$('pw').value;$('pw').value='';
 try{login(await call('/api/login',{method:'POST',headers:{'X-Requested-With':'adblock','Authorization':'Basic '+b64('admin:'+pw)}}));$('lmsg').textContent=''}
 catch(e){$('lmsg').textContent=e.message}};
$('mk').onclick=async()=>{const a=$('np1').value,b=$('np2').value;
 if(a!==b){$('smsg').textContent='Las dos contraseñas no son iguales.';return}
 if(bytes(a)<12||bytes(a)>64){$('smsg').textContent='Usa entre 12 y 64 caracteres.';return}
 try{const j=await call('/api/setup',{method:'POST',headers:{'X-Requested-With':'adblock','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({password:a})});
  $('np1').value=$('np2').value='';$('smsg').textContent='';pwSet=true;login(j)}
 catch(e){$('smsg').textContent=e.message}};
$('chg').onclick=async()=>{const c=$('cp0').value,a=$('cp1').value,b=$('cp2').value;$('cmsg').textContent='';
 if(a!==b){$('cmsg').textContent='Las dos contraseñas nuevas no son iguales.';return}
 if(bytes(a)<12||bytes(a)>64){$('cmsg').textContent='Usa entre 12 y 64 caracteres.';return}
 try{const j=await post('/api/password',{current:c,new:a});$('cp0').value=$('cp1').value=$('cp2').value='';
  tk=j.token;try{sessionStorage.setItem('tk',tk)}catch(e){};$('cmsg').textContent='Listo: contraseña cambiada.'}
 catch(e){$('cmsg').textContent=e.message}};
async function backup(){if(!tk)return;try{const a=await get('/api/backup-ap');$('apn').textContent=a.ssid;$('app').textContent=a.pass}catch(e){}}
$('pw').onkeydown=e=>{if(e.key==='Enter')$('go').click()};
$('out').onclick=async()=>{try{await post('/api/logout')}catch(e){};drop()};
async function act(fn){$('msg').textContent='';try{await fn();await Promise.all([tick(),rules()])}catch(e){$('msg').textContent=e.message}}
document.querySelectorAll('[data-min]').forEach(b=>b.onclick=()=>act(()=>post('/api/pause',{minutes:b.dataset.min})));
$('up').onclick=()=>act(()=>post('/api/update'));
const add=a=>act(async()=>{const d=$('dom').value.trim();if(!d)return;await post('/api/rules',{action:a,domain:d});$('dom').value=''});
$('blk').onclick=()=>add('block');$('alw').onclick=()=>add('allow');
$('dom').onkeydown=e=>{if(e.key==='Enter')add('block')};
async function rules(){
 if(!tk)return;try{const r=await get('/api/rules'),ul=$('rules'),items=[];
 const row=(d,kind)=>{const li=el('li'),x=el('button','x','quitar');x.onclick=()=>act(()=>post('/api/rules',{action:'remove',domain:d}));
  li.append(el('span','tag '+(kind=='b'?'tuyo':'permitido'),kind=='b'?'bloqueo':'permitido'),el('span','d',d),x);return li};
 r.blocked.forEach(d=>items.push(row(d,'b')));r.allowed.forEach(d=>items.push(row(d,'a')));
 if(!items.length)items.push(el('li','l','Sin reglas. Agrega un dominio para bloquearlo o permitirlo siempre.'));
 ul.replaceChildren(...items)}catch(e){}}
async function tick(){
 try{const s=await (await fetch('/api/stats')).json();
  $('q').textContent=fmt(s.queries);$('b').textContent=fmt(s.blocked);
  $('p').textContent=s.queries?(100*s.blocked/s.queries).toFixed(1):'0';$('d').textContent=fmt(s.domains);$('c').textContent=fmt(s.cached);
  $('hcut').hidden=!s.schedule;if(s.schedule)$('hcut').textContent='Además, los horarios cortaron '+fmt(s.schedule)+' consultas (no cuentan como anuncios).';
  const st=$('status');st.replaceChildren();
  let m='Funcionando desde hace '+(s.uptime<3600?Math.floor(s.uptime/60)+' min':Math.floor(s.uptime/3600)+' h');
  $('tec').textContent=' DNS de afuera: '+s.upstream+' · memoria libre: '+Math.round(s.heap/1024)+' KB';
  if(s.updating)m+=' · actualizando lista…';st.append(el('span','',m));
  if(s.error)st.append(el('span','warn',' · error: '+s.error));
  if(s.skipped)st.append(el('span','warn',' · la lista no cupo completa: '+fmt(s.skipped)+' dominios quedaron fuera '));
  pwSet=s.passwordSet;setupOk=s.setupOpen;$('ver').textContent=s.version||'';showAuth();
  $('pst').textContent=s.paused?'En pausa: quedan '+Math.ceil(s.paused/60)+' min':'Activo';
  $('pst').className='grow '+(s.paused?'warn':'');$('resume').disabled=!s.paused;
  if(tk){detail();if(++hTick%5==0)loadDevs();if(hTick%20==0){loadHist();cargaNube()}}
 }catch(e){$('status').textContent='Sin conexión con SiBlock'}
}
async function detail(){
 try{const s=await get('/api/detail');
  const ul=$('rec');const tags={lista:'bloqueo',tuyo:'bloqueo tuyo',permitido:'permitido',pausa:'en pausa',horario:'horario','pausa-aparato':'aparato en pausa',ok:'pasó'};
  ul.replaceChildren(...(s.recent.length?s.recent.map(r=>{const li=el('li');li.append(el('span','tag '+r.v,tags[r.v]||r.v),el('span','d',r.d),el('span','ip',devNames[r.c]||r.c));return li}):[el('li','l','Todavía no hay consultas.')]));
  const ago=t=>t<60?t+' s':t<3600?Math.floor(t/60)+' min':Math.floor(t/3600)+' h';
  const by=s.bypass||[],K={doh:'DNS cifrado',relay:'Private Relay'};$('saltan').hidden=!by.length;
  $('saltanb').replaceChildren(...by.map(x=>{const tr=el('tr');[devNames[x.ip]||x.ip,x.k.map(k=>K[k]||k).join(' y '),ago(x.ago)].forEach(v=>tr.append(el('td','',v)));return tr}));
 }catch(e){}
}
// ---- ¿Este aparato usa SiBlock? Carga una imagen desde un nombre que solo SiBlock contesta.
const GUIA='https://siblock.s-dev.win/guia-router.html',OBST='https://siblock.s-dev.win/obstaculos.html';
function rnd(){const a='abcdefghijklmnopqrstuvwxyz0123456789',u=new Uint8Array(12);
 try{crypto.getRandomValues(u)}catch(e){for(let i=0;i<12;i++)u[i]=Math.random()*256}
 return Array.from(u,x=>a[x%36]).join('')}
function probe(){return new Promise(res=>{const img=new Image();let done=false;
 const fin=v=>{if(done)return;done=true;clearTimeout(t);img.onload=img.onerror=null;res(v)};
 const t=setTimeout(()=>fin(false),4000);img.onload=()=>fin(true);img.onerror=()=>fin(false);
 img.src='http://'+rnd()+'.prueba.siblock.internal'+(location.port?':'+location.port:'')+'/siblock-prueba.gif'})}
let lastProbe=null;
async function check(){const c=$('cmp');c.textContent='Revisando si este aparato usa SiBlock…';c.className='';
 const ok=await probe();lastProbe=ok;prepDiag();
 c.textContent=ok?'✅ Este aparato está usando SiBlock.':'❌ Este aparato todavía no usa SiBlock.';c.className=ok?'ok':'warn';return ok}
const TXT={usa_siblock:'Este aparato usa SiBlock.',
 navegador_dns_propio:'Este aparato pregunta a SiBlock, pero esta revisión no pasó por SiBlock. Puede ser que este navegador use su propio DNS ("DNS seguro"): apágalo en sus ajustes. O que tu módem o router reparta otro DNS por IPv6 (mira el aviso de IPv6).',
 no_usa_siblock:'Este aparato no pregunta a SiBlock. Revisa que tu módem o router dé la dirección de SiBlock como DNS y desconecta y vuelve a conectar el WiFi del aparato.',
 ipv6:'Si tu módem o router tiene IPv6, apaga su DNS por IPv6: los aparatos lo usan para saltarse SiBlock.',
 sin_consultas:'Este aparato no ha preguntado nada a SiBlock en los últimos 10 minutos.',
 dns_cifrado:'Este aparato usa un DNS cifrado y se salta SiBlock. Apaga "DNS privado" o "DNS seguro" en sus ajustes.',
 private_relay:'Este aparato usa iCloud Private Relay y se salta SiBlock. Apágalo en el iPhone o Mac, o quien tenga la contraseña del panel puede activar «Apagar iCloud Private Relay en esta casa».',
 en_pausa:'SiBlock está en pausa: no bloquea nada hasta que termine.',
 aparato_en_pausa:'Este aparato tiene una pausa propia: ve anuncios hasta que termine. Quien tenga la contraseña del panel puede quitarla en «Aparatos».',
 horario_todo:'Ahora mismo un horario le corta todo internet a este aparato. Quien tenga la contraseña del panel puede cambiarlo en «Horarios».',
 horario_activo:'Ahora mismo un horario le corta redes sociales o juegos a este aparato. Si lo que no abre es de eso, es por el horario.',
 sin_lista:'SiBlock todavía no tiene su lista de anuncios. Revisa que tenga internet y espera unos minutos.',
 sin_internet:'SiBlock no logra hablar con internet. Revisa el módem.',
 router_dns:'Todo llega desde tu módem o router: así SiBlock no ve cada aparato. Lo mejor es que reparta la dirección de SiBlock directo.',
 todo_bien:'No encontré nada raro.'};
const LINK={navegador_dns_propio:OBST,no_usa_siblock:GUIA,ipv6:GUIA,router_dns:GUIA,dns_cifrado:OBST,private_relay:OBST};
const ICO={bad:'⛔',warn:'⚠️',info:'ℹ️',ok:'✅'},RANK={bad:0,warn:1,info:2,ok:3};
async function algo(){const b=$('algo'),ul=$('hallazgos');b.disabled=true;ul.replaceChildren(el('li','l','Revisando…'));
 try{const ok=await check();
  const j=await call('/api/diagnose?probe='+(ok?'ok':'fail'),{headers:{'X-Requested-With':'adblock'}});
  const f=(j.findings||[]).filter(x=>TXT[x.code]).sort((a,b)=>(RANK[a.level]??4)-(RANK[b.level]??4));
  ul.replaceChildren(...f.map(x=>{const li=el('li'),t=el('span','d',TXT[x.code]);
   if(LINK[x.code]){const a=el('a','',LINK[x.code]==OBST?'Ver qué hacer':'Ver la guía');a.href=LINK[x.code];a.target='_blank';a.rel='noopener noreferrer';t.append(' ',a)}
   li.append(el('span','ico '+(x.level=='bad'?'warn':x.level),ICO[x.level]||'•'),t);return li}))}
 catch(e){ul.replaceChildren(el('li','warn','No pude revisar: '+e.message))}
 b.disabled=false}
$('recheck').onclick=check;$('algo').onclick=algo;

// ---- Aparatos (nombre, pausa solo en ese aparato) y horarios
let devs=[],devNames={},hItems=[],hTz=0,hEdit=-1;
const DIAS=['L','M','M','J','V','S','D'],DIASL=['lunes','martes','miércoles','jueves','viernes','sábado','domingo'];
const GRUPOS={1:'Redes sociales',2:'Juegos',4:'Todo internet'};
const hhmm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
const mins=v=>{const p=/^(\d\d):(\d\d)$/.exec(v||'');return p?(+p[1])*60+(+p[2]):-1};
const tzAqui=()=>-new Date().getTimezoneOffset();
DIAS.forEach((d,i)=>{const l=el('label','chk'),c=el('input');c.type='checkbox';c.id='hd'+i;c.checked=true;c.setAttribute('aria-label',DIASL[i]);l.append(c,el('span','',d));$('hdays').append(l)});
async function devAct(body,ok){$('dmsg').textContent='';try{await post('/api/devices',body);if(ok)$('dmsg').textContent=ok;$('devs').querySelectorAll('.nb').forEach(b=>b.remove());await loadDevs();loadSched()}catch(e){$('dmsg').textContent=e.message}}
function nameBox(li,d){if(li.querySelector('.nb'))return;const box=el('div','nb'),inp=el('input','grow'),ok=el('button','','Guardar'),no=el('button','','Cancelar');
 inp.value=d.n||'';inp.placeholder='Por ejemplo: Tele sala';inp.setAttribute('aria-label','Nombre del aparato');
 ok.onclick=()=>{const v=inp.value.trim();if(!v||bytes(v)>32){$('dmsg').textContent=v?'El nombre es muy largo: usa uno más corto (los acentos y los emoji ocupan más).':'Escribe un nombre.';return}devAct({action:'name',h:d.h,name:v},'Listo: nombre guardado.')};
 no.onclick=()=>box.remove();inp.onkeydown=e=>{if(e.key==='Enter')ok.click()};box.append(inp,ok,no);li.append(box);inp.focus()}
async function loadDevs(){if(!tk)return;try{const r=await get('/api/devices');devs=r.devices||[];devNames={};
 devs.forEach(d=>{if(d.ip&&d.n)devNames[d.ip]=d.n});$('sinhora').hidden=!!r.hora;
 const ul=$('devs');if(ul.querySelector('.nb input'))return;
 ul.replaceChildren(...(devs.length?devs.map(d=>{const li=el('li','dv'),t=el('span','d',d.n||'Aparato sin nombre');
  if(d.yo)t.append(el('span','me','Este aparato'));
  const info=[];if(d.ip)info.push(d.ip);if(d.ago>=0)info.push(fmt(d.q)+' consultas, '+fmt(d.b)+' bloqueadas');else info.push('no se ve ahora');
  if(d.pausa>0)info.push('en pausa: quedan '+Math.ceil(d.pausa/60)+' min');t.append(el('small','',info.join(' · ')));li.append(t);
  if(!d.h){li.append(el('span','l','Todavía no lo reconozco; vuelve a intentar en un minuto'));return li}
  const nb=el('button','',d.n?'Cambiar nombre':'Ponerle nombre');nb.onclick=()=>nameBox(li,d);li.append(nb);
  if(d.s>=0){const p=el('button','',d.pausa>0?'Reanudar':'Pausar 1 h');p.onclick=()=>devAct({action:'pause',h:d.h,minutes:d.pausa>0?'0':'60'});
   const f=el('button','','Olvidar');f.onclick=()=>{if(confirm('¿Olvidar «'+d.n+'»? Se borra su nombre y sale de sus horarios.'))devAct({action:'forget',h:d.h},'Listo: aparato olvidado.')};li.append(p,f)}
  return li}):[el('li','l','Todavía no hay aparatos. Aparecen cuando usan internet.')]));
 pintaDevForm()}catch(e){}}
function pintaDevForm(){const box=$('hdev'),was={};box.querySelectorAll('input').forEach(c=>was[c.value]=c.checked);
 const named=devs.filter(d=>d.s>=0);
 box.replaceChildren(...(named.length?named.map(d=>{const l=el('label','chk'),c=el('input');c.type='checkbox';c.value=d.s;c.checked=!!was[d.s];l.append(c,el('span','',d.n));return l}):[el('span','l','Primero ponle nombre a un aparato.')]))}
async function loadSched(){if(!tk)return;try{const r=await get('/api/schedules');hItems=r.items||[];hTz=r.tz;
 $('tzaviso').hidden=!hItems.length||hTz===tzAqui();
 const nombre=s=>{const d=devs.find(x=>x.s===s);return d?d.n:'aparato '+(s+1)};
 $('hlist').replaceChildren(...hItems.map(h=>{const li=el('li','dv'),bits=parseInt(h.dev,16)>>>0,who=[],dias=[],g=[];
  for(let s=0;s<32;s++)if(bits&(1<<s))who.push(nombre(s));DIAS.forEach((d,i)=>{if(h.d&(1<<i))dias.push(d)});[1,2,4].forEach(k=>{if(h.g&k)g.push(GRUPOS[k])});
  const t=el('span','d',who.join(', '));t.append(el('small','',g.join(' y ')+' · '+dias.join(' ')+' · '+hhmm(h.ini)+' a '+hhmm(h.fin)));
  const c=el('button','','Cambiar'),x=el('button','','Borrar');c.onclick=()=>editSched(h);
  x.onclick=async()=>{$('hmsg').textContent='';try{await post('/api/schedules',{action:'delete',i:h.i});loadSched()}catch(e){$('hmsg').textContent=e.message}};
  li.append(t,c,x);return li}))}catch(e){}}
function editSched(h){hEdit=h.i;const bits=parseInt(h.dev,16)>>>0;$('hdev').querySelectorAll('input').forEach(c=>c.checked=!!(bits&(1<<+c.value)));
 [1,2,4].forEach(k=>$('hg'+k).checked=!!(h.g&k));DIAS.forEach((d,i)=>$('hd'+i).checked=!!(h.d&(1<<i)));
 $('hini').value=hhmm(h.ini);$('hfin').value=hhmm(h.fin);$('hcancel').hidden=false;$('hsave').textContent='Guardar cambios';$('hmsg').textContent=''}
function finEdit(){hEdit=-1;$('hcancel').hidden=true;$('hsave').textContent='Guardar horario'}
$('hcancel').onclick=finEdit;
$('hsave').onclick=async()=>{const m=$('hmsg');m.textContent='';let dev=0,g=0,d=0;
 $('hdev').querySelectorAll('input').forEach(c=>{if(c.checked)dev|=1<<+c.value});[1,2,4].forEach(k=>{if($('hg'+k).checked)g|=k});DIAS.forEach((x,i)=>{if($('hd'+i).checked)d|=1<<i});
 const ini=mins($('hini').value),fin=mins($('hfin').value);
 if(!(dev>>>0)){m.textContent='Elige al menos un aparato.';return}if(!g){m.textContent='Elige qué bloquear.';return}if(!d){m.textContent='Elige al menos un día.';return}
 if(ini<0||fin<0||ini===fin){m.textContent='La hora de inicio y la de fin deben ser distintas.';return}
 try{await post('/api/schedules',{action:'save',i:hEdit,dev:(dev>>>0).toString(16).padStart(8,'0'),g,d,ini,fin,tz:tzAqui()});
  m.textContent='Listo: horario guardado.';finEdit();loadSched()}catch(e){m.textContent=e.message}};

// ---- Respaldo: bajar y subir (en pedazos de hasta 3 KB, partidos en finales de línea)
$('bdown').onclick=async()=>{const m=$('bres');m.textContent='';m.className='';
 try{const r=await fetch('/api/backup',{headers:hdr()});if(r.status==401&&tk)drop();if(!r.ok){let e='';try{e=(await r.json()).error||''}catch(x){}throw new Error(e?'No se pudo bajar el respaldo: '+e+'.':'No se pudo bajar el respaldo.')}
  const name=(/filename="(siblock-respaldo[0-9-]*\.txt)"/.exec(r.headers.get('Content-Disposition')||'')||[])[1]||'siblock-respaldo.txt';
  const url=URL.createObjectURL(await r.blob()),a=el('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();
  setTimeout(()=>URL.revokeObjectURL(url),1000);m.textContent='Listo: guárdalo en un lugar seguro y no lo compartas: trae los nombres de tus aparatos y datos que sirven para reconocerlos.'}
 catch(e){m.textContent=e.message}};
function pedazos(t){const out=[];let cur='',n=0;for(const l of t.split(/(?<=\n)/)){const b=bytes(l);
 if(n+b>3072&&cur){out.push(cur);cur='';n=0}cur+=l;n+=b}if(cur)out.push(cur);return out}
$('bfile').onchange=async()=>{const f=$('bfile').files[0],m=$('bres');m.textContent='';m.className='';$('bconf').hidden=true;if(!f)return;
 try{if(f.size>24576)throw new Error('Ese archivo es demasiado grande para ser un respaldo de SiBlock.');
  const t=await f.text(),ps=pedazos(t);if(!ps.length)throw new Error('El archivo está vacío.');
  m.textContent='Revisando…';
  for(let i=0;i<ps.length;i++){const r=await fetch('/api/restore/part',{method:'POST',headers:hdr({'Content-Type':'text/plain; charset=utf-8','X-Parte':String(i)}),body:ps[i]});
   let j={};try{j=await r.json()}catch(e){}if(r.status==401&&tk)drop();if(!r.ok)throw new Error(j.error||('error '+r.status))}
  const c=await post('/api/restore/check');
  const pl=(n,a,b)=>n+' '+(n==1?a:b);
  m.textContent='Este respaldo tiene '+pl(c.reglas,'regla','reglas')+', '+pl(c.aparatos,'aparato con nombre','aparatos con nombre')+' y '+pl(c.horarios,'horario','horarios')+'. Si sigues, reemplaza tus reglas, aparatos, horarios, listas y la opción de Private Relay.';
  $('bconf').hidden=false}
 catch(e){m.textContent=e.message;m.className='warn'}finally{$('bfile').value=''}};
$('bno').onclick=()=>{$('bconf').hidden=true;$('bres').textContent=''};
$('bsi').onclick=async()=>{const m=$('bres');$('bconf').hidden=true;
 try{await post('/api/restore/apply',{confirm:'1'});m.textContent='Listo: respaldo restaurado.';m.className='';rules();options();loadDevs().then(loadSched)}
 catch(e){m.textContent=e.message;m.className='warn'}};

// ---- Opción de Private Relay
async function options(){if(!tk)return;try{const o=await get('/api/options');$('relay').checked=!!o.relayBlock;
 $('lsc').hidden=!(o.listsOffer&2);$('lse').hidden=!(o.listsOffer&4);$('lscam').checked=!!(o.lists&2);$('lspa').checked=!!(o.lists&4);
 const k=$('lskip');k.hidden=!(o.skipped>0);k.textContent=o.skipped>0?'No cupieron '+fmt(o.skipped)+' dominios de anuncios'+(o.lists&2?'; las estafas sí están todas':'')+'.':''}catch(e){}}
async function cambiaListas(c){const on=c.checked,m=1|($('lscam').checked?2:0)|($('lspa').checked?4:0);$('lsmsg').textContent='';
 try{await post('/api/options',{lists:m});$('lsmsg').textContent='Listo: SiBlock va a bajar la lista otra vez.';tick()}
 catch(e){c.checked=!on;$('lsmsg').textContent=e.message}}
$('lscam').onchange=()=>cambiaListas($('lscam'));$('lspa').onchange=()=>cambiaListas($('lspa'));
$('relay').onchange=async()=>{const c=$('relay'),on=c.checked;$('rmsg').textContent='';
 try{await post('/api/options',{relayBlock:on?'1':'0'});$('rmsg').textContent=on?'Listo: Private Relay apagado en esta casa.':'Listo: Private Relay vuelve a funcionar.'}
 catch(e){c.checked=!on;$('rmsg').textContent=e.message}};

// ---- Gráficas de uso (SVG a mano) y top 10
const NS='http://www.w3.org/2000/svg';let hist=null,modo=24,hTick=0;
function sv(tag,attrs,cls){const e=document.createElementNS(NS,tag);for(const k in attrs)e.setAttribute(k,attrs[k]);if(cls)e.setAttribute('class',cls);return e}
function serie(){const h=hist.h,out=[];
 if(modo==24){for(let i=144;i<168;i++){const d=new Date((hist.hour-167+i)*3600000);
  out.push({q:h[i][0],b:h[i][1],t:String(d.getHours()).padStart(2,'0')+':00',ax:d.getHours()%6==0?d.getHours()+' h':''})}return out}
 const days=[];let key=null;
 for(let i=0;i<168;i++){const d=new Date((hist.hour-167+i)*3600000),k=d.toDateString();
  if(k!==key){key=k;const w=d.toLocaleDateString('es',{weekday:'short',day:'numeric'});days.push({q:0,b:0,t:w,ax:d.toLocaleDateString('es',{weekday:'short'})})}
  days[days.length-1].q+=h[i][0];days[days.length-1].b+=h[i][1]}
 return days.slice(-7)}
function draw(){const g=$('grafica');if(!hist)return;
 $('ver24').setAttribute('aria-pressed',modo==24);$('ver7').setAttribute('aria-pressed',modo!=24);
 const s=serie(),tq=s.reduce((a,x)=>a+x.q,0),tb=s.reduce((a,x)=>a+x.b,0),empty=!hist.clock||!tq;
 g.hidden=$('ley').hidden=empty;$('usovacio').hidden=!empty;g.replaceChildren();
 const per=modo==24?'Últimas 24 horas':'Últimos 7 días';
 $('usores').textContent=empty?'':per+': '+fmt(tq)+' consultas, '+fmt(tb)+' bloqueadas ('+(100*tb/tq).toFixed(0)+' %).';
 if(empty)return;
 const W=Math.max(200,Math.round(g.getBoundingClientRect().width)||320),H=170,top=8,bot=20,ph=H-top-bot;
 g.setAttribute('viewBox','0 0 '+W+' '+H);g.setAttribute('aria-label',$('usores').textContent);
 const max=Math.max(1,...s.map(x=>x.q)),slot=W/s.length,bw=Math.max(2,slot-2);
 s.forEach((x,i)=>{const gx=sv('g',{}),X=i*slot+(slot-bw)/2,y=v=>top+ph-ph*v/max,hq=ph*x.q/max,hb=ph*x.b/max;
  const t=sv('title',{});t.textContent=x.t+': '+fmt(x.q)+' consultas, '+fmt(x.b)+' bloqueadas';gx.append(t);
  gx.append(sv('rect',{x:i*slot,y:top,width:slot,height:ph},'hit'));
  if(x.q)gx.append(sv('rect',{x:X,y:y(x.q),width:bw,height:Math.max(1,hq),rx:Math.min(2,bw/2)},'bq'));
  if(x.b)gx.append(sv('rect',{x:X,y:y(x.b),width:bw,height:Math.max(1,hb),rx:Math.min(2,bw/2)},'bb'));
  if(x.ax){const a=sv('text',{x:X+bw/2,y:H-5,'text-anchor':'middle'},'ax');a.textContent=x.ax;gx.append(a)}
  g.append(gx)});
 g.append(sv('line',{x1:0,x2:W,y1:top+ph+.5,y2:top+ph+.5},'base'))}
$('ver24').onclick=()=>{modo=24;draw()};$('ver7').onclick=()=>{modo=7;draw()};
let rz;addEventListener('resize',()=>{clearTimeout(rz);rz=setTimeout(draw,150)});
async function loadHist(){if(!tk)return;try{hist=await get('/api/history');draw();
 const mb=hist.ahorroMb|0;$('ahorro').hidden=!mb;$('ahorro').textContent=mb?'Aproximado: en 7 días tu casa se ahorró unos '+fmt(mb)+' MB de anuncios':'';
 const ol=$('top');if(ol.querySelector('.nb select:enabled'))return;ol.replaceChildren(...(hist.top.length?hist.top.map(t=>{const li=el('li'),x=el('button','x','No debió bloquearse');li.style.flexWrap='wrap';x.onclick=()=>noDebio(li,t.d);li.append(el('span','d',t.d),el('span','n2',fmt(t.n)),x);return li}):[el('li','l','Todavía nada bloqueado.')]))}catch(e){}}

// ---- Compartir con SiBlock (apagado de fábrica): opciones, aviso de versión y reportes
const NUBE='https://siblock.s-dev.win/';let nube=null;
const NMSG={sin_internet:'SiBlock no pudo llegar a internet. Prueba más tarde.',limite:'Mandaste muchos seguidos. Espera un minuto.',
 rechazado:'No se pudo guardar. Prueba más tarde.',apagado:'Primero activa «Compartir datos anónimos».',
 ocupado:'Espera a que termine el envío anterior.',datos:'Revisa lo que elegiste.',guardar:'No se pudo guardar; prueba otra vez.'};
const nerr=e=>NMSG[e.message]||e.message;
function pintaNube(){if(!nube)return;$('share').checked=!!nube.share;$('vchk').checked=!!nube.versionCheck;
 $('upd').hidden=!nube.update;$('updv').textContent=nube.latest||'';
 if(/^[0-9a-f]{32}$/.test(nube.installId||''))$('rep').href=NUBE+'reporte.html#i='+nube.installId}
async function cargaNube(){if(!tk)return;try{nube=await get('/api/cloud');pintaNube()}catch(e){}}
async function opcionNube(campo,c){const on=c.checked;$('nmsg').textContent='';
 try{nube=await post('/api/cloud',{[campo]:on?'1':'0'});pintaNube();$('nmsg').textContent='Listo: guardado.'}
 catch(e){c.checked=!on;$('nmsg').textContent=nerr(e)}}
$('share').onchange=()=>opcionNube('share',$('share'));$('vchk').onchange=()=>opcionNube('versionCheck',$('vchk'));
$('msend').onclick=async()=>{const b=$('msend'),m=$('mmsg');m.textContent='';
 if(!nube||!nube.share){m.textContent=NMSG.apagado;return}
 if(!$('mfab').value||!$('mdns').value||!$('mmenu').value){m.textContent='Revisa lo que elegiste.';return}
 b.disabled=true;m.textContent='Mandando…';
 try{await post('/api/report/modem',{fabricante:$('mfab').value,dnsOk:$('mdns').value,menu:$('mmenu').value});m.textContent='Listo: gracias por ayudar.'}
 catch(e){m.textContent=nerr(e)}b.disabled=false};
function noDebio(li,d){if(li.querySelector('.nb'))return;const box=el('div','nb');
 if(!nube||!nube.share){box.append(el('span','l','Para mandarlo, primero activa «Compartir datos anónimos» en «Compartir con SiBlock».'));li.append(box);return}
 const sel=$('motivos').cloneNode(true);sel.hidden=false;sel.removeAttribute('id');sel.setAttribute('aria-label','Qué pasa con '+d);
 const ok=el('button','','Mandar'),no=el('button','','Cancelar'),m=el('span','l');m.setAttribute('aria-live','polite');
 no.onclick=()=>box.remove();
 ok.onclick=async()=>{ok.disabled=true;m.textContent='Mandando…';
  try{await post('/api/report/blocked',{domain:d,motivo:sel.value});m.textContent='Listo: gracias por avisar.';sel.disabled=true}
  catch(e){m.textContent=nerr(e);ok.disabled=false}};
 box.append(sel,ok,no,m);li.append(box)}
// Escríbenos: el diagnóstico va en el fragmento (#diag=...), que el navegador nunca manda a ningún servidor.
const b64u=s=>btoa(String.fromCharCode(...new TextEncoder().encode(s))).replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'');
// Se arma antes del toque (al cargar, tras revisar, al pasar o enfocar): un window.open después de
// esperar respuestas lo bloquean Safari y otros navegadores. El toque sigue el enlace normal.
async function prepDiag(){let d={v:'',heap:0,listErr:'',codes:[]};
 try{const s=await (await fetch('/api/stats')).json();d.v=String(s.version||'').slice(0,100);d.heap=s.heap|0;d.listErr=String(s.error||'').slice(0,100)}catch(e){}
 try{const j=await call('/api/diagnose?probe='+(lastProbe===null?'':lastProbe?'ok':'fail'),{headers:{'X-Requested-With':'adblock'}});
  d.codes=(j.findings||[]).map(x=>x.code).filter(c=>/^[a-z_]{1,40}$/.test(c)).slice(0,20)}catch(e){}
 let f=b64u(JSON.stringify(d));if(f.length>600){d.codes=[];f=b64u(JSON.stringify(d))}
 $('esc').href=NUBE+'contacto.html#diag='+f}
$('esc').onfocus=$('esc').onpointerenter=()=>{prepDiag()};
$('esc').onclick=()=>{setTimeout(prepDiag,0)};

showAuth();tick();rules();backup();options();cargaNube();loadHist();loadDevs().then(loadSched);check();setInterval(tick,3000);
</script></body></html>)HTML";

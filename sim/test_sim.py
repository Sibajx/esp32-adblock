#!/usr/bin/env python3
"""Pruebas de punta a punta contra el ESP32 simulado (make run en otra terminal).

  python3 test_sim.py          # todo
  python3 test_sim.py --lento  # además espera 70 s a que venza una pausa de 1 min
  python3 test_sim.py --solo-nuevas  # solo las secciones con placas propias (12 en adelante)
  python3 test_sim.py --solo-nube    # solo la sección 18 (nube)
  python3 test_sim.py --solo-telegram  # solo la sección 18g (avisos por Telegram)
  python3 test_sim.py --solo-fase5   # solo las secciones 19 a 23 (fase 5)
"""
import base64
import concurrent.futures as cf
import http.client as httpclient
import hashlib
import http.server as httpserver
import json
import os
import re
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

import dns.exception
import dns.message
import dns.query
import dns.rdatatype

DNS = ("127.0.0.1", int(os.environ.get("SIM_DNS_PORT", 5353)))
WEB = f"http://127.0.0.1:{os.environ.get('SIM_WEB_PORT', 8080)}"
PASSWORD = "clave-de-prueba"
HERE = os.path.dirname(os.path.abspath(__file__))
SIM_FLASH = os.environ.get("SIM_FLASH_DIR", os.path.join(HERE, "sim-flash"))
FRESH = os.path.join(HERE, "esp32-sim-fresh")
fails = 0


def check(cond, msg):
    global fails
    print(("  ok    " if cond else "  FALLA ") + msg)
    if not cond:
        fails += 1


def query(name, rdtype="A", timeout=3):
    q = dns.message.make_query(name, rdtype, use_edns=0)
    return dns.query.udp(q, DNS[0], port=DNS[1], timeout=timeout)


def addrs(r):
    return sorted(rr.to_text() for rrset in r.answer for rr in rrset
                  if rrset.rdtype in (dns.rdatatype.A, dns.rdatatype.AAAA))


TOKEN = None


def http(method, path=None, data=None, csrf=True, password=None, token=True, headers=None, raw=None, web=None):
    """password=None y token=True: usa la sesión abierta en la sección 4. token="..." usa ese token."""
    if raw is not None:
        body = raw
    else:
        body = urllib.parse.urlencode(data or {}).encode() if method == "POST" else None
    req = urllib.request.Request((web or WEB) + path, data=body, method=method)
    if csrf:
        req.add_header("X-Requested-With", "adblock")
    if password is not None:
        b = base64.b64encode(f"admin:{password}".encode()).decode()
        req.add_header("Authorization", "Basic " + b)
    elif isinstance(token, str):
        req.add_header("X-Token", token)
    elif token and TOKEN and web is None:
        req.add_header("X-Token", TOKEN)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode(), dict(r.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(), dict(e.headers)


def stats():
    return json.loads(http("GET", "/api/stats", csrf=False, token=False)[1])


class Fresh:
    """Otra placa simulada (sim/esp32-sim-fresh, sin secrets.h) en su propia carpeta de flash.

    Se cierra con Popen.terminate() y wait(); nunca con pkill."""

    def __init__(self, nvs=None, env=None, clock_ms=None, stdin=False, list_bin=True, dir=None):
        self.own_dir = dir is None  # con dir=... se relanza sobre la flash de otra placa y no se borra
        self.dir = dir or tempfile.mkdtemp(prefix="siblock-sim-")
        if nvs is not None:
            with open(os.path.join(self.dir, "nvs.json"), "w") as f:
                json.dump(nvs, f)
        if clock_ms is not None:
            self.set_clock(clock_ms)
        src = os.path.join(SIM_FLASH, "list.bin")
        if list_bin and self.own_dir and os.path.exists(src):
            shutil.copy(src, os.path.join(self.dir, "list.bin"))
        self.dns_port, self.web_port = 5363, 8090
        self.web = f"http://127.0.0.1:{self.web_port}"
        e = dict(os.environ, SIM_DNS_PORT=str(self.dns_port), SIM_WEB_PORT=str(self.web_port),
                 SIM_FLASH=self.dir, SIM_HTTP_FAIL="1")
        e.pop("SIM_CLIENT_IP", None)
        e.update(env or {})
        self.log_path = os.path.join(self.dir, f"salida-{time.time_ns()}.log")
        self.log = open(self.log_path, "wb")
        self.proc = subprocess.Popen([FRESH], cwd=HERE, env=e, stdout=self.log, stderr=subprocess.STDOUT,
                                     stdin=subprocess.PIPE if stdin else subprocess.DEVNULL)
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                self.stats()
                return
            except Exception:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.2)
        raise RuntimeError("la placa simulada no arrancó: " + self.output().decode(errors="replace")[-2000:])

    def set_clock(self, ms):
        with open(os.path.join(self.dir, ".clock_ms"), "w") as f:
            f.write(str(int(ms)))
        time.sleep(0.5)  # el simulador relee el reloj cada 200 ms

    def http(self, method, path, data=None, **kw):
        kw.setdefault("token", False)
        return http(method, path, data, web=self.web, **kw)

    def stats(self):
        return json.loads(self.http("GET", "/api/stats", csrf=False)[1])

    def nvs(self):
        with open(os.path.join(self.dir, "nvs.json"), "rb") as f:
            return json.loads(f.read()).get("siblock", {})

    def output(self):
        self.log.flush()
        with open(self.log_path, "rb") as f:
            return f.read()

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.close()
        if self.own_dir:
            shutil.rmtree(self.dir, ignore_errors=True)

    def press(self, on):
        path = os.path.join(self.dir, ".boot")
        if on:
            open(path, "w").close()
        elif os.path.exists(path):
            os.remove(path)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


AP_ALPHABET = set("abcdefghjkmnpqrstuvwxyz23456789")
SEED_WIFI = {"siblock": {"wifi_ssid": "simulador", "wifi_pass": "simulador"}}


def detail():
    code, body, _ = http("GET", "/api/detail")
    return json.loads(body) if code == 200 else {}


def primer_uso():
    print("\n12. Primer uso (placa sin contraseña)")
    with Fresh(nvs=SEED_WIFI) as f:
        st = f.stats()
        check(st.get("passwordSet") is False and st.get("setupOpen") is True,
              f"passwordSet=false y setupOpen=true ({st.get('passwordSet')}, {st.get('setupOpen')})")
        code, body, _ = f.http("GET", "/api/detail")
        check(code == 403 and "primero crea tu contraseña" in body, f"/api/detail sin contraseña -> 403 ({code})")
        code, body, _ = f.http("GET", "/")
        check(code == 200 and "Crea tu contraseña" in body, "la página trae 'Crea tu contraseña'")
        check(f.http("POST", "/api/setup", {"password": "clave:con:dos€€"}, csrf=False)[0] == 403, "/api/setup sin anti-CSRF -> 403")
        code, body, _ = f.http("POST", "/api/setup", {"password": "a" * 11})
        check(code == 400 and "entre 12 y 64" in body, f"contraseña de 11 -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/setup", {"password": "a" * 65})
        check(code == 400, f"contraseña de 65 -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/setup", raw=b"password=abc%00" + b"x" * 12)
        check(code == 400 and f.stats().get("passwordSet") is False,
              f"contraseña con %00 -> 400 y no se crea ({code})")
        code, body, _ = f.http("POST", "/api/setup", raw=b"password=abc\x00" + b"x" * 12)
        check(code == 400 and f.stats().get("passwordSet") is False, f"contraseña con un nulo crudo -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/setup", {"password": "clave:con:dos€€"})
        tok = json.loads(body).get("token") if code == 200 else None
        check(code == 200 and tok and len(tok) == 32, f"crear 'clave:con:dos€€' -> 200 con token ({code})")
        check(f.http("GET", "/api/detail", token=tok)[0] == 200, "el token de /api/setup sirve")
        code, body, _ = f.http("POST", "/api/setup", {"password": "otra-clave-larga"})
        check(code == 409 and "ya hay contraseña" in body, f"segundo /api/setup -> 409 ({code})")
        st = f.stats()
        check(st.get("passwordSet") is True and st.get("setupOpen") is False, "ya con contraseña: passwordSet=true, setupOpen=false")
        nvs = f.nvs()
        check("clave:con:dos" not in json.dumps(nvs, ensure_ascii=False), "la contraseña no queda en texto en NVS")
        code, body, _ = f.http("POST", "/api/login", password="clave:con:dos€€")
        check(code == 200, f"login Basic con ':' y '€' -> 200 ({code})")
        check(f.http("POST", "/api/login", password="clave:con:dos€")[0] == 401, "login con la contraseña incompleta -> 401")
        code, _, _ = f.http("POST", "/api/password", {"current": "mala-mala-mala", "new": "nueva-clave-123"}, token=tok)
        check(code == 401, f"/api/password con la actual mala -> 401 ({code})")
        code, _, _ = f.http("POST", "/api/password", {"current": "clave:con:dos€€", "new": "corta"}, token=tok)
        check(code == 400, f"/api/password con nueva inválida -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/password", {"current": "clave:con:dos€€", "new": "nueva-clave-123"}, token=tok)
        tok2 = json.loads(body).get("token") if code == 200 else None
        check(code == 200 and tok2 and tok2 != tok, f"/api/password con la buena -> 200 y token nuevo ({code})")
        check(f.http("GET", "/api/detail", token=tok)[0] == 401, "el token viejo ya no sirve")
        check(f.http("GET", "/api/detail", token=tok2)[0] == 200, "el token nuevo sirve")
        check(f.http("POST", "/api/login", password="nueva-clave-123")[0] == 200, "entra con la contraseña nueva")
        check(f.http("POST", "/api/login", password="clave:con:dos€€")[0] == 401, "la vieja ya no entra")
        check(f.http("GET", "/api/backup-ap", token=False)[0] == 401, "/api/backup-ap sin sesión -> 401")
        code, body, _ = f.http("GET", "/api/backup-ap", token=tok2)
        ap = json.loads(body) if code == 200 else {}
        check(ap.get("ssid") == "SiBlock-0C3F", f"/api/backup-ap ssid = SiBlock-0C3F ({ap.get('ssid')})")
        pw = ap.get("pass", "")
        check(len(pw) == 10 and set(pw) <= AP_ALPHABET, "/api/backup-ap pass de 10 caracteres del alfabeto")
        check(f.nvs().get("ap_pass") == pw, "es la misma que está guardada")

    print("\n12a. Contraseña de respaldo cuando la NVS no la guarda")
    with Fresh(nvs=SEED_WIFI, env={"SIM_NVS_FAIL_KEY": "ap_pass"}) as f:
        code, body, _ = f.http("POST", "/api/setup", {"password": "clave-de-prueba-9"})
        tok = json.loads(body).get("token") if code == 200 else None
        pws = []
        for _ in range(2):
            code, body, _ = f.http("GET", "/api/backup-ap", token=tok)
            pws.append(json.loads(body).get("pass", "") if code == 200 else "")
        check(pws[0] and pws[0] == pws[1], f"sin poder guardarla, /api/backup-ap da siempre la misma ({pws})")
        out = f.output().decode(errors="replace")
        check(f"/ {pws[0]}" in out, "y es la misma que salió por el cable al arrancar")
        check("ap_pass" not in f.nvs() and "no se pudo guardar la contraseña de respaldo" in out,
              "no quedó guardada y lo avisa por el cable")

    print("\n12b. Ventana de creación (reloj adelantado)")
    with Fresh(nvs=SEED_WIFI) as f:
        f.set_clock(31 * 60000)  # después de arrancar: la ventana se abrió al encender
        st = f.stats()
        check(st.get("setupOpen") is False and st.get("passwordSet") is False, f"a los 31 min setupOpen=false ({st.get('setupOpen')})")
        code, body, _ = f.http("POST", "/api/setup", {"password": "clave-de-prueba-2"})
        check(code == 403 and "Pulsa el botón BOOT de tu SiBlock una vez y vuelve a intentar" in body,
              f"/api/setup fuera de la ventana -> 403 con el texto de BOOT ({code})")

    print("\n12c. Corte de luz a mitad de guardar la contraseña")
    half = {"siblock": {"wifi_ssid": "simulador", "wifi_pass": "simulador",
                        "panel_salt": "hex:" + "00" * 16, "panel_iter": "10000"}}
    with Fresh(nvs=half) as f:
        st = f.stats()
        check(st.get("passwordSet") is False and st.get("setupOpen") is True, "sal sin hash -> sin contraseña (se puede crear)")
        check(f.http("POST", "/api/setup", {"password": "clave-de-prueba-2"})[0] == 200, "y se crea bien")

    print("\n12d. Corte de luz a mitad de cambiar la contraseña")
    def cred(pw, salt_byte, prefix):
        salt = bytes([salt_byte]) * 16
        h = hashlib.pbkdf2_hmac("sha256", pw.encode(), salt, 10000)
        return {f"{prefix}salt": "hex:" + salt.hex(), f"{prefix}iter": "10000", f"{prefix}hash": "hex:" + h.hex()}
    base = {"wifi_ssid": "simulador", "wifi_pass": "simulador"}
    vieja, nueva = cred("clave-vieja-123", 0x11, "panel_"), cred("clave-nueva-456", 0x22, "panel_n")
    with Fresh(nvs={"siblock": {**base, **vieja, **nueva}}) as f:
        check(f.http("POST", "/api/login", password="clave-nueva-456")[0] == 200, "la nueva (completa) ya vale")
        check(f.http("POST", "/api/login", password="clave-vieja-123")[0] == 401, "la vieja ya no")
        nvs = f.nvs()
        check(not [k for k in nvs if k.startswith("panel_n")] and nvs.get("panel_hash", "").lower() == nueva["panel_nhash"],
              f"al arrancar se termina de copiar ({sorted(nvs)})")
    media = {"panel_nsalt": nueva["panel_nsalt"], "panel_niter": "10000"}  # sin hash: no llegó a guardarse
    with Fresh(nvs={"siblock": {**base, **vieja, **media}}) as f:
        st = f.stats()
        check(st.get("passwordSet") is True and st.get("setupOpen") is False,
              "cambio a medias: el panel sigue con contraseña (no se puede crear otra)")
        check(f.http("POST", "/api/setup", {"password": "clave-del-intruso"})[0] == 409, "/api/setup -> 409")
        check(f.http("POST", "/api/login", password="clave-vieja-123")[0] == 200, "la vieja sigue valiendo")
        check(not [k for k in f.nvs() if k.startswith("panel_n")], "y lo que quedó a medias se borra")


def boton_boot():
    print("\n13. Botón BOOT")
    with Fresh(nvs=SEED_WIFI) as f:
        f.set_clock(31 * 60000)
        check(f.stats().get("setupOpen") is False, "ventana cerrada a los 31 min")
        f.press(True)
        time.sleep(0.5)
        f.press(False)
        time.sleep(0.5)
        check(f.stats().get("setupOpen") is True, "pulsación corta de BOOT -> ventana abierta otra vez")
        check(b"[boot] ventana de 30 min para crear la contrase" in f.output(), "lo dice por serie")
        check(f.http("POST", "/api/setup", {"password": "clave-de-prueba-3"})[0] == 200, "y se puede crear la contraseña")

    with Fresh(nvs=SEED_WIFI) as f:
        f.press(True)
        time.sleep(0.02)
        f.press(False)
        time.sleep(0.3)
        f.set_clock(31 * 60000)
        check(f.stats().get("setupOpen") is False, "un toque de 20 ms no cuenta (antirrebote)")

    print("\n13b. Borrar la configuración (BOOT 10 s)")
    f = Fresh(nvs=SEED_WIFI)
    try:
        code, body, _ = f.http("POST", "/api/setup", {"password": "clave-de-prueba-3"})
        tok = json.loads(body).get("token") if code == 200 else None
        check(f.http("POST", "/api/rules", {"action": "block", "domain": "regla-propia.example"}, token=tok)[0] == 200,
              "contraseña creada y una regla propia agregada")
        check("panel_hash" in f.nvs() and "ap_pass" in f.nvs(), "NVS tiene contraseña y contraseña del AP")
        f.press(True)
        time.sleep(0.5)
        f.set_clock(11000)
        try:
            rc = f.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            rc = None
        f.press(False)
        check(rc == 3, f"al llegar a 10 s se reinicia (código {rc})")
        out = f.output()
        check(b"[boot] borrando la configuraci" in out, "lo dice por serie")
        nvs = f.nvs()
        check("panel_hash" not in nvs and "ap_pass" not in nvs and "wifi_ssid" not in nvs,
              f"NVS sin contraseña, sin WiFi y sin contraseña del AP ({sorted(nvs)})")
        custom = os.path.join(f.dir, "custom.txt")
        check(os.path.exists(custom) and "regla-propia.example" in open(custom).read(), "custom.txt sigue con la regla")
        check(os.path.exists(os.path.join(f.dir, "list.bin")), "list.bin sigue ahí")
        with Fresh(dir=f.dir) as g:
            st = g.stats()
            check(st.get("passwordSet") is False and st.get("setupOpen") is True, "al volver a arrancar: sin contraseña")
    finally:
        f.close()


def improv_paquete(tipo, datos):
    p = b"IMPROV" + bytes([1, tipo, len(datos)]) + bytes(datos)
    return p + bytes([sum(p) % 256]) + b"\n"


def improv_comando(cmd, textos):
    d = b"".join(bytes([len(t)]) + t for t in (x.encode() for x in textos))
    return improv_paquete(3, bytes([cmd, len(d)]) + d)


def improv_leer(salida):
    """Parser mínimo: devuelve [(tipo, datos)] de los paquetes con checksum bueno."""
    out, i = [], 0
    while (i := salida.find(b"IMPROV", i)) >= 0:
        if i + 9 <= len(salida) and salida[i + 6] == 1:
            n = salida[i + 8]
            fin = i + 9 + n
            if fin < len(salida) and sum(salida[i:fin]) % 256 == salida[fin]:
                out.append((salida[i + 7], salida[i + 9:fin]))
                i = fin
                continue
        i += 1
    return out


def improv_textos(datos):
    cmd, n, t, pos = datos[0], datos[1], [], 2
    while pos < 2 + n:
        k = datos[pos]
        t.append(datos[pos + 1:pos + 1 + k].decode("utf-8", "replace"))
        pos += 1 + k
    return cmd, t


def improv_usb():
    print("\n14. Improv por USB")
    with Fresh(env={"SIM_SERIAL_STDIN": "1", "SIM_WIFI": "down"}, stdin=True) as f:
        def mandar(b, espera=1.0):
            antes = len(f.output())
            f.proc.stdin.write(b)
            f.proc.stdin.flush()
            time.sleep(espera)
            return improv_leer(f.output()[antes:])

        got = mandar(bytes.fromhex("494d50524f56010302a00083"))
        res = [improv_textos(d) for t, d in got if t == 4]
        check(len(res) == 1 and res[0][0] == 0xA0 and res[0][1][0] == "SiBlock-0C3F", f"0xA0 -> nombre del AP ({res})")
        pw = res[0][1][1] if res and len(res[0][1]) > 1 else ""
        check(len(pw) == 10 and set(pw) <= AP_ALPHABET, "0xA0 -> contraseña de 10 caracteres del alfabeto")
        got = mandar(improv_comando(2, []))
        check((1, bytes([2])) in got and not [d for t, d in got if t == 4], "estado sin WiFi -> listo (0x02), sin URL")
        got = mandar(improv_comando(3, []))
        res = [improv_textos(d) for t, d in got if t == 4]
        check(res and res[0] == (3, ["SiBlock", "dev", "ESP32", "SiBlock-0C3F"]), f"info -> {res}")
        got = mandar(improv_comando(4, []))
        res = [improv_textos(d) for t, d in got if t == 4]
        check(len(res) == 3 and res[0] == (4, ["CasaDemo", "-50", "YES"]) and res[1] == (4, ['Café "Ñandú"', "-70", "NO"])
              and res[2] == (4, []), f"escaneo -> dos redes y fin ({res})")
        got = mandar(improv_comando(1, ["simulador-ok", "clave123"]), espera=2.0)
        estados = [d[0] for t, d in got if t == 1]
        res = [improv_textos(d) for t, d in got if t == 4]
        check(estados[:2] == [3, 4], f"WiFi bueno -> estados conectando y conectado ({estados})")
        check(res == [(1, ["http://127.0.0.1/"])], f"y la URL del panel ({res})")
        check(f.nvs().get("wifi_ssid") == "simulador-ok" and f.nvs().get("wifi_pass") == "clave123", "WiFi guardado en NVS")
        got = mandar(improv_comando(2, []))
        res = [improv_textos(d) for t, d in got if t == 4]
        check((1, bytes([4])) in got and res == [(2, ["http://127.0.0.1/"])], f"estado conectado -> 0x04 + URL ({res})")
        got = mandar(improv_comando(0x77, []))
        check((2, bytes([2])) in got, "comando desconocido -> error 0x02")
        malo = bytearray(improv_comando(2, []))
        malo[-2] ^= 1
        got = mandar(bytes(malo))
        check((2, bytes([1])) in got, "checksum malo -> error 0x01")
        got = mandar(improv_comando(1, ["", "clave123"]))
        check((2, bytes([1])) in got, "SSID vacío -> error 0x01")
        got = mandar(improv_comando(1, ["otra-red", "clave123"]), espera=23)
        check((2, bytes([3])) in got, "WiFi que no conecta -> error 0x03 (a los 20 s)")
        check(f.nvs().get("wifi_ssid") == "simulador-ok", "y no se guarda (queda el de antes)")
        got = mandar(improv_comando(2, []), espera=2)
        check((1, bytes([4])) in got, "vuelve a conectar con el WiFi guardado")
        check(b"STATS," in f.output(), "las líneas STATS siguen saliendo")

    print("\n14b. Improv con el WiFi de la casa conectado")
    with Fresh(env={"SIM_SERIAL_STDIN": "1"}, stdin=True) as f:
        def mandar(b, espera=1.0):
            antes = len(f.output())
            f.proc.stdin.write(b)
            f.proc.stdin.flush()
            time.sleep(espera)
            return improv_leer(f.output()[antes:])

        # La placa sigue conectada a "CasaDemo" justo después de pedir otra red: eso no es éxito.
        got = mandar(improv_comando(1, ["otra-red", "clave123"]), espera=1.5)
        estados = [d[0] for t, d in got if t == 1]
        check(estados == [3] and not [d for t, d in got if t == 4],
              f"otra red con la casa todavía conectada -> sigue conectando, sin URL ({estados})")
        check("wifi_ssid" not in f.nvs(), "y no se guarda")
        f.set_clock(25000)
        got = improv_leer(f.output())
        check((2, bytes([3])) in got, "a los 20 s -> error 0x03")
        check("wifi_ssid" not in f.nvs(), "sigue sin guardarse")
        got = mandar(improv_comando(1, ["simulador-ok", "clave123"]), espera=2.0)
        estados = [d[0] for t, d in got if t == 1]
        check(estados[:2] == [3, 4] and f.nvs().get("wifi_ssid") == "simulador-ok",
              f"la red pedida sí conectada -> se guarda ({estados})")


def portal_wifi():
    print("\n15. Modo de configuración por WiFi")
    with Fresh(env={"SIM_WIFI": "down"}) as f:
        f.set_clock(110000)
        check(f.stats().get("portal") is False, "sin WiFi guardado, a los 110 s: todavía sin portal")
        f.set_clock(125000)
        time.sleep(0.3)
        check(f.stats().get("portal") is True, "a los 125 s: portal abierto")
        out = f.output()
        ap_pass = f.nvs().get("ap_pass", "")
        linea = next((l for l in out.splitlines() if l.startswith(b"[portal] abierto")), b"")
        check(linea == b"[portal] abierto: SiBlock-0C3F", f"la salida dice '[portal] abierto: SiBlock-0C3F' ({linea!r})")
        check(len(ap_pass) == 10 and ap_pass.encode() not in linea and ap_pass.encode() not in
              b"".join(l for l in out.splitlines() if b"portal" in l or b"[sim] AP" in l),
              "la contraseña del AP no sale en los mensajes del portal")
        q = dns.message.make_query("example.com", "A")
        r = dns.query.udp(q, "127.0.0.1", port=f.dns_port, timeout=3)
        check(addrs(r) == ["127.0.0.1"], f"DNS example.com -> IP de la placa ({addrs(r)})")
        try:
            r = dns.query.tcp(dns.message.make_query("example.com", "A"), "127.0.0.1", port=f.dns_port, timeout=3)
            tcp_addrs = addrs(r)
        except Exception as e:
            tcp_addrs = repr(e)
        check(tcp_addrs == ["127.0.0.1"], f"DNS por TCP también -> IP de la placa, sin reenviar ({tcp_addrs})")
        r = dns.query.udp(dns.message.make_query("example.com", "AAAA"), "127.0.0.1", port=f.dns_port, timeout=3)
        check(r.rcode() == 0 and not r.answer, "AAAA -> vacío")
        c = httpclient.HTTPConnection("127.0.0.1", f.web_port, timeout=5)
        c.request("GET", "/generate_204", headers={"Host": "connectivitycheck.gstatic.com"})
        resp = c.getresponse()
        resp.read()
        check(resp.status == 302 and resp.getheader("Location") == "http://192.168.4.1/",
              f"Host ajeno -> 302 a http://192.168.4.1/ ({resp.status} {resp.getheader('Location')})")
        c.close()
        code, body, hdr = f.http("GET", "/", headers={"Host": "192.168.4.1"})
        check(code == 200 and "Configura el WiFi de SiBlock" in body, "GET / -> página del portal")
        check("frame-ancestors 'none'" in hdr.get("Content-Security-Policy", ""), "con la misma CSP")
        code, body, _ = f.http("GET", "/api/wifi/scan", headers={"Host": "192.168.4.1"})
        nets = json.loads(body).get("networks", []) if code == 200 else []
        check(len(nets) == 2 and nets[0] == {"ssid": "CasaDemo", "rssi": -50, "open": False}, f"escaneo: dos redes ({nets})")
        check(len(nets) == 2 and nets[1] == {"ssid": 'Café "Ñandú"', "rssi": -70, "open": True}, "la segunda bien escapada y abierta")
        check(f.http("GET", "/api/wifi/scan", csrf=False)[0] == 403, "escaneo sin anti-CSRF -> 403")
        code2, body2, _ = f.http("GET", "/api/wifi/scan", headers={"Host": "192.168.4.1"})
        escaneos = f.output().count(b"[sim] escaneo de redes")
        check(code2 == 200 and body2 == body and escaneos == 1,
              f"segundo escaneo antes de 10 s -> mismo resultado sin volver a escanear ({escaneos} escaneos)")
        f.set_clock(125000 + 11000)
        time.sleep(0.3)
        f.http("GET", "/api/wifi/scan", headers={"Host": "192.168.4.1"})
        escaneos = f.output().count(b"[sim] escaneo de redes")
        check(escaneos == 2, f"pasados 10 s sí se escanea otra vez ({escaneos} escaneos)")
        code, _, _ = f.http("POST", "/api/wifi", raw=b"ssid=simulador-ok%00xxxxxxxx&pass=clave123")
        check(code == 400 and "wifi_ssid" not in f.nvs(), f"SSID con %00 -> 400 y no se guarda ({code})")
        code, body, _ = f.http("POST", "/api/wifi", {"ssid": "Casa de Mamá ñ & 100%", "pass": "1234567"})
        check(code == 400, f"contraseña de 7 -> 400 ({code})")
        check(f.http("POST", "/api/wifi", {"ssid": "simulador-ok", "pass": "clave123"}, csrf=False)[0] == 403, "sin anti-CSRF -> 403")
        check(f.http("POST", "/api/wifi", {"ssid": "x" * 33, "pass": "clave123"})[0] == 400, "SSID de 33 -> 400")
        code, body, _ = f.http("POST", "/api/wifi", {"ssid": "simulador-ok", "pass": "clave123"})
        check(code == 200, f"WiFi bueno -> 200 ({code})")
        time.sleep(3)
        check(f.stats().get("portal") is False, "a los 3 s el portal se cierra")
        check(f.nvs().get("wifi_ssid") == "simulador-ok", "y el WiFi queda guardado")

    with Fresh(env={"SIM_WIFI": "down", "SIM_CLIENT_IP": "192.168.1.50"}) as f:
        f.set_clock(125000)
        time.sleep(0.3)
        check(f.stats().get("portal") is True, "otra placa con el portal abierto")
        code, _, _ = f.http("POST", "/api/wifi", {"ssid": "Casa de Mamá ñ & 100%", "pass": "12345678"})
        check(code == 403, f"un aparato de la casa (no del AP) -> 403 ({code})")
        check("wifi_ssid" not in f.nvs(), "y no se guarda nada")

    with Fresh(nvs={"siblock": {"wifi_ssid": "malo", "wifi_pass": "clave-mala"}}, env={"SIM_WIFI": "down"}) as f:
        f.set_clock(590000)
        check(f.stats().get("portal") is False, "WiFi guardado que no conecta: a los 590 s sin portal")
        f.set_clock(605000)
        time.sleep(0.3)
        check(f.stats().get("portal") is True, "a los 605 s se abre")
        f.set_clock(605000 + 890000)
        check(f.stats().get("portal") is True, "nadie conectado: sigue abierto a los 890 s")
        f.set_clock(605000 + 910000)
        time.sleep(0.3)
        check(f.stats().get("portal") is False, "a los 910 s se cierra solo")

    with Fresh(nvs=SEED_WIFI) as f:
        check(f.stats().get("portal") is False, "con WiFi: sin portal")
        check(f.http("GET", "/api/wifi/scan")[0] == 404, "fuera del portal /api/wifi/scan -> 404")
        check(f.http("POST", "/api/wifi", {"ssid": "a", "pass": ""})[0] == 404, "fuera del portal /api/wifi -> 404")
        f.press(True)
        time.sleep(0.3)
        f.set_clock(4000)
        time.sleep(0.3)
        f.press(False)
        time.sleep(0.5)
        check(f.stats().get("portal") is True, "BOOT 3-8 s abre el portal aunque haya WiFi")
        # En el simulador la red de la casa (127.0.0.0/8) abarca la del AP (127.0.0.1): es el caso
        # de una casa con el mismo rango que SiBlock. No se puede saber quién es del AP: nadie lo es.
        code, body, _ = f.http("GET", "/")
        check(code == 200 and "Configura el WiFi de SiBlock" not in body, "red de la casa que choca: se ve el panel, no el portal")
        code, _, _ = f.http("POST", "/api/wifi", {"ssid": "Red del vecino", "pass": "12345678"})
        check(code == 403, f"y /api/wifi -> 403 ({code})")
        check(f.nvs().get("wifi_ssid") == "simulador", "el WiFi guardado no cambia")
        r = dns.query.udp(dns.message.make_query("doubleclick.net", "A"), "127.0.0.1", port=f.dns_port, timeout=3)
        check(addrs(r) == ["0.0.0.0"], f"el DNS sigue bloqueando para la casa, sin portal cautivo ({addrs(r)})")
        check(b"[sim] AP en 192.168.4.1" in f.output(), "la red de SiBlock se abre en 192.168.4.1 (no choca con 127.x)")


def relay_placa_nueva():
    print("\n16. Opción de Private Relay en una placa nueva")
    with Fresh(nvs=SEED_WIFI) as f:
        check("relay_block" not in f.nvs(), "placa nueva: relay_block no está en NVS")
        r = dns.query.udp(dns.message.make_query("mask.icloud.com", "A"), "127.0.0.1", port=f.dns_port, timeout=4)
        check(r.rcode() == 0, f"placa nueva: mask.icloud.com no es NXDOMAIN (rcode {r.rcode()})")
    nvs = {"siblock": dict(SEED_WIFI["siblock"], relay_block="1")}
    f = Fresh(nvs=nvs)
    try:
        r = dns.query.udp(dns.message.make_query("mask.icloud.com", "A"), "127.0.0.1", port=f.dns_port, timeout=4)
        check(r.rcode() == 3, f"con relay_block=1 en NVS al arrancar -> NXDOMAIN (rcode {r.rcode()})")
        for name in ("history.bin", "history.tmp"):  # como si la placa ya tuviera historia guardada
            with open(os.path.join(f.dir, name), "wb") as fh:
                fh.write(b"SBH1" + bytes(1348))
        f.press(True)
        time.sleep(0.5)
        f.set_clock(11000)
        try:
            rc = f.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            rc = None
        f.press(False)
        check(rc == 3, f"BOOT 10 s borra la configuración (código {rc})")
        check("relay_block" not in f.nvs(), f"relay_block ya no está en NVS ({sorted(f.nvs())})")
        check(not os.path.exists(os.path.join(f.dir, "history.bin")) and
              not os.path.exists(os.path.join(f.dir, "history.tmp")),
              "borrar la configuración también borra la historia de las gráficas")
    finally:
        f.close()


def comprobacion_placa_nueva():
    print("\n17. Router que hace de DNS e historia que sobrevive al reinicio")
    with Fresh(nvs=SEED_WIFI, env={"SIM_GATEWAY": "127.0.0.1"}) as f:
        def q(n):
            dns.query.udp(dns.message.make_query("doubleclick.net", "A"), "127.0.0.1", port=f.dns_port, timeout=4)
        for i in range(10):
            q(i)
        body = f.http("GET", "/api/diagnose")[1]
        check("router_dns" not in body, f"10 consultas de la puerta de enlace: sin router_dns ({body})")
        for i in range(50):
            q(i)
        body = f.http("GET", "/api/diagnose")[1]
        check("router_dns" in body, f"60 consultas de la puerta de enlace: router_dns ({body})")
    with Fresh(nvs=SEED_WIFI) as f:
        for i in range(60):
            dns.query.udp(dns.message.make_query("doubleclick.net", "A"), "127.0.0.1", port=f.dns_port, timeout=4)
        body = f.http("GET", "/api/diagnose")[1]
        check("router_dns" not in body, "sin SIM_GATEWAY (127.0.0.254) no sale router_dns")

    f = Fresh(nvs=SEED_WIFI)
    try:
        check(f.http("POST", "/api/setup", {"password": "clave-de-prueba-4"})[0] == 200, "contraseña creada")
        for _ in range(5):
            dns.query.udp(dns.message.make_query("doubleclick.net", "A"), "127.0.0.1", port=f.dns_port, timeout=4)
        f.set_clock(61 * 60000)
        time.sleep(1)
        f.proc.terminate()
        f.proc.wait(timeout=10)
        check(os.path.exists(os.path.join(f.dir, "history.bin")) and
              os.path.getsize(os.path.join(f.dir, "history.bin")) == 1352, "history.bin guardado (1352 B)")
        check(not os.path.exists(os.path.join(f.dir, "history.tmp")), "sin history.tmp suelto")
        with Fresh(dir=f.dir) as g:
            code, body, _ = g.http("GET", "/api/history", password="clave-de-prueba-4")
            h = json.loads(body).get("h", []) if code == 200 else []
            check(sum(x[1] for x in h) >= 5, f"tras reiniciar la historia sigue ({code}, {sum(x[1] for x in h)} bloqueadas)")
    finally:
        f.close()


class NubeFalsa:
    """Worker de mentira: anota cada petición (método, ruta, headers, cuerpo) y contesta lo que
    diga respuestas[ruta] = (código, cuerpo); si la ruta no está, 200 {"ok":true}."""

    def __init__(self):
        self.peticiones = []
        self.respuestas = {}
        self.lock = threading.Lock()
        nube = self

        class Manejador(httpserver.BaseHTTPRequestHandler):
            def _atender(self):
                n = int(self.headers.get("Content-Length") or 0)
                cuerpo = self.rfile.read(n) if n else b""
                with nube.lock:
                    nube.peticiones.append({"metodo": self.command, "ruta": self.path,
                                            "headers": {k.lower(): v for k, v in self.headers.items()},
                                            "cuerpo": cuerpo})
                    codigo, resp = nube.respuestas.get(self.path, (200, '{"ok":true}'))
                data = resp.encode() if isinstance(resp, str) else resp
                self.send_response(codigo)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            do_GET = do_POST = do_PUT = do_DELETE = _atender

            def log_message(self, *a):
                pass

        self.server = httpserver.ThreadingHTTPServer(("127.0.0.1", 0), Manejador)
        self.puerto = self.server.server_address[1]
        self.url = f"http://127.0.0.1:{self.puerto}"
        self.hilo = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.hilo.start()

    def lista(self):
        with self.lock:
            return list(self.peticiones)

    def a(self, ruta):
        return [p for p in self.lista() if p["ruta"] == ruta]

    def close(self):
        self.server.shutdown()
        self.server.server_close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def adelantar(f, desde_ms, horas, paso_ms=3600000):
    """Adelanta el reloj de la placa `horas` horas en pasos de 1 h; devuelve el reloj final."""
    t = desde_ms
    for _ in range(int(horas * 3600000 // paso_ms)):
        t += paso_ms
        f.set_clock(t)
    time.sleep(1)
    return t


def detener(f):
    """Apaga el proceso de una placa sin borrar su flash (para volver a arrancarla)."""
    if f.proc.poll() is None:
        f.proc.terminate()
        f.proc.wait(timeout=10)


def nube_apagada():
    print("\n18. Nube apagada de fábrica")
    with NubeFalsa() as nube:
        env = {"SIM_CLOUD_URL": nube.url}
        f = Fresh(nvs=SEED_WIFI, env=env)
        try:
            t = adelantar(f, 0, 25)
            check(len(nube.lista()) == 0, f"sin permiso no sale nada en 25 h ({len(nube.lista())} peticiones)")
            check(b"[sim] nube" not in f.output(), "el registro no anota ninguna petición a la nube")
            detener(f)
            with open(os.path.join(f.dir, "nvs.json")) as fh:
                nvs = json.load(fh)
            nvs.setdefault("siblock", {}).update({"cloud_share": "2", "ver_check": "2"})
            with open(os.path.join(f.dir, "nvs.json"), "w") as fh:
                json.dump(nvs, fh)
            with Fresh(dir=f.dir, env=env) as g:
                adelantar(g, t, 25)
                check(len(nube.lista()) == 0,
                      f"con cloud_share=2 y ver_check=2 en NVS: 25 h más sin peticiones ({len(nube.lista())})")
        finally:
            f.close()


def crear_clave(f, clave):
    code, body, _ = f.http("POST", "/api/setup", {"password": clave})
    return json.loads(body).get("token") if code == 200 else None


def entrar(f, clave):
    """Sesión nueva (la de antes vence a las 12 h de reloj)."""
    code, body, _ = f.http("POST", "/api/login", password=clave)
    return json.loads(body).get("token") if code == 200 else None


def esperar(cond, segundos=8):
    fin = time.time() + segundos
    while time.time() < fin:
        if cond():
            return True
        time.sleep(0.2)
    return cond()


def nube_estado(f, tok):
    code, body, _ = f.http("GET", "/api/cloud", token=tok)
    return json.loads(body) if code == 200 else {}


def nube_opciones():
    print("\n18 (sigue). Opciones de la nube, aviso diario y consulta de versión")
    with NubeFalsa() as nube:
        env = {"SIM_CLOUD_URL": nube.url}
        f = Fresh(nvs=SEED_WIFI, env=env)
        try:
            tok = crear_clave(f, "clave-de-la-nube-1")
            check(tok is not None, "contraseña creada")
            code, _, _ = f.http("GET", "/api/cloud")
            check(code == 401, f"GET /api/cloud sin sesión -> 401 ({code})")
            code, _, _ = f.http("GET", "/api/cloud", csrf=False, token=tok)
            check(code == 403, f"GET /api/cloud sin anti-CSRF -> 403 ({code})")
            code, _, _ = f.http("POST", "/api/cloud", {"share": "1"}, csrf=False, token=tok)
            check(code == 403, f"POST /api/cloud sin anti-CSRF -> 403 ({code})")
            code, _, _ = f.http("POST", "/api/cloud", {"share": "1"})
            check(code == 401, f"POST /api/cloud sin sesión -> 401 ({code})")
            st = nube_estado(f, tok)
            iid = st.get("installId", "")
            check(st.get("share") is False and st.get("versionCheck") is False, f"de fábrica todo apagado ({st})")
            check(len(iid) == 32 and all(c in "0123456789abcdef" for c in iid), f"installId de 32 hex ({iid})")
            check(st.get("current") == "dev" and st.get("latest") == "" and st.get("update") is False and
                  st.get("lastPing") == "never", f"current=dev, latest vacío, update=false, lastPing=never ({st})")
            check(set(st) == {"share", "versionCheck", "installId", "current", "latest", "update", "lastPing",
                              "community", "communityCount", "communityUpdated"},
                  f"solo las claves del contrato ({sorted(st)})")
            check(st.get("community") is False and st.get("communityCount") == 0 and st.get("communityUpdated") == 0,
                  f"permitidos de la comunidad apagados y vacíos ({st})")
            check(nube_estado(f, tok).get("installId") == iid and f.nvs().get("install_id") == iid,
                  "el número se guarda y no cambia")
            for malo in ({"share": "2"}, {"share": ""}, {"versionCheck": "si"}, {"share": "1", "versionCheck": "2"},
                         {}, {"otra": "1"}, {"community": "2"}, {"community": ""}, {"community": "1", "share": "x"}):
                code, _, _ = f.http("POST", "/api/cloud", malo, token=tok)
                check(code == 400, f"POST /api/cloud {malo} -> 400 ({code})")
            st = nube_estado(f, tok)
            check(st.get("share") is False and st.get("versionCheck") is False and st.get("community") is False and
                  "cloud_share" not in f.nvs() and "comunidad" not in f.nvs(),
                  "tras los 400 todo sigue apagado y no se guardó nada")
            check(len(nube.lista()) == 0, "nada salió a la nube todavía")

            for _ in range(3):  # 3 bloqueadas en la historia de esta hora
                dns.query.udp(dns.message.make_query("doubleclick.net", "A"), "127.0.0.1", port=f.dns_port, timeout=4)
            code, body, _ = f.http("POST", "/api/cloud", {"share": "1"}, token=tok)
            check(code == 200 and json.loads(body).get("share") is True, f"share=1 -> 200 share:true ({code} {body})")
            check(f.nvs().get("cloud_share") == "1", "cloud_share=1 en NVS")
            time.sleep(1)
            check(len(nube.lista()) == 0, "al encender no sale nada enseguida (el primero va al minuto)")
            t = 61000
            f.set_clock(t)
            check(esperar(lambda: nube.a("/api/v1/placa")), "a los 61 s la nube recibió POST /api/v1/placa")
            ps = nube.a("/api/v1/placa")
            if ps:
                p = ps[0]
                try:
                    cuerpo = json.loads(p["cuerpo"])
                except ValueError:
                    cuerpo = {}
                check(p["metodo"] == "POST", "es un POST")
                check(set(cuerpo) == {"i", "v", "b"} and cuerpo.get("i") == iid and cuerpo.get("v") == "dev" and
                      isinstance(cuerpo.get("b"), int) and cuerpo.get("b") >= 3,
                      f"cuerpo con solo i, v y b ({p['cuerpo']!r})")
                check(p["headers"].get("content-type") == "application/json", "Content-Type: application/json")
                check(p["headers"].get("user-agent", "").startswith("SiBlock/"),
                      f"User-Agent empieza con SiBlock/ ({p['headers'].get('user-agent')})")
            check(esperar(lambda: nube_estado(f, tok).get("lastPing") == "ok"), "lastPing:ok")
            check(len(nube.a("/version.json")) == 0, "sin 'avisarme de versiones' no se consulta la versión")

            nube.respuestas["/api/v1/placa"] = (500, '{"error":"interno"}')
            t += 25 * 3600000
            f.set_clock(t)
            check(esperar(lambda: len(nube.a("/api/v1/placa")) == 2), "a las 25 h sale el aviso del día siguiente")
            tok = entrar(f, "clave-de-la-nube-1")
            check(esperar(lambda: nube_estado(f, tok).get("lastPing") == "fail"), "la nube contesta 500 -> lastPing:fail")
            t += 30 * 60000
            f.set_clock(t)
            time.sleep(1.5)
            check(len(nube.a("/api/v1/placa")) == 2, "a la media hora no reintenta")
            t += 31 * 60000
            f.set_clock(t)
            check(esperar(lambda: len(nube.a("/api/v1/placa")) == 3), "a la hora reintenta")
            del nube.respuestas["/api/v1/placa"]

            nube.respuestas["/version.json"] = (200, '{"version":"v9.0.0","url":"https://siblock.s-dev.win/#paso-instalar"}')
            code, body, _ = f.http("POST", "/api/cloud", {"versionCheck": "1"}, token=tok)
            check(code == 200 and json.loads(body).get("versionCheck") is True, f"versionCheck=1 -> 200 ({code})")
            t += 61000
            f.set_clock(t)
            check(esperar(lambda: nube.a("/version.json")), "a los 61 s la nube recibió GET /version.json")
            vs = nube.a("/version.json")
            if vs:
                v = vs[0]
                check(v["metodo"] == "GET" and v["cuerpo"] == b"", "GET sin cuerpo")
                check(iid not in json.dumps(v["headers"]) and iid not in v["ruta"], "sin el número de instalación")
            st = nube_estado(f, tok)
            check(esperar(lambda: nube_estado(f, tok).get("latest") == "v9.0.0"), f"latest:v9.0.0 ({st.get('latest')})")
            check(nube_estado(f, tok).get("update") is False, "update:false (la versión es dev)")

            antes = len(nube.lista())
            code, body, _ = f.http("POST", "/api/cloud", {"share": "0", "versionCheck": "0"}, token=tok)
            check(code == 200 and json.loads(body).get("share") is False and json.loads(body).get("versionCheck") is False,
                  f"share=0 y versionCheck=0 -> 200 ({code})")
            check(f.nvs().get("cloud_share") == "0" and f.nvs().get("ver_check") == "0", "y se guardan en NVS")
            t = adelantar(f, t, 25)
            check(len(nube.lista()) == antes, f"apagado: 25 h más sin peticiones nuevas ({len(nube.lista()) - antes})")
            tok = entrar(f, "clave-de-la-nube-1")

            code, _, _ = f.http("POST", "/api/cloud", {"share": "1"}, token=tok)
            detener(f)
            with Fresh(dir=f.dir, env=env) as g:
                antes = len(nube.a("/api/v1/placa"))
                t += 61000
                g.set_clock(t)
                check(esperar(lambda: len(nube.a("/api/v1/placa")) == antes + 1),
                      "al arrancar con 'compartir' encendido, el aviso sale al minuto")
        finally:
            f.close()


def nube_reportes():
    print("\n18b. Reportes desde el panel")
    nube = NubeFalsa()
    env = {"SIM_CLOUD_URL": nube.url}
    f = Fresh(nvs=SEED_WIFI, env=env)
    rutas = ("/api/v1/reporte/modem", "/api/v1/reporte/bloqueo")

    def reportes():
        return [p for p in nube.lista() if p["ruta"] in rutas]

    try:
        tok = crear_clave(f, "clave-de-la-nube-2")
        iid = nube_estado(f, tok).get("installId", "")
        modem = {"fabricante": "huawei", "dnsOk": "1", "menu": "internet_wan"}
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 409 and "apagado" in body, f"con compartir apagado -> 409 apagado ({code} {body})")
        code, body, _ = f.http("POST", "/api/report/blocked", {"domain": "ads.ejemplo.com", "motivo": "otro"}, token=tok)
        check(code == 409, f"bloqueado con compartir apagado -> 409 ({code})")
        check(len(nube.lista()) == 0, "y la nube no recibió nada")
        code, _, _ = f.http("POST", "/api/report/modem", modem)
        check(code == 401, f"sin sesión -> 401 ({code})")
        code, _, _ = f.http("POST", "/api/report/modem", modem, csrf=False, token=tok)
        check(code == 403, f"sin anti-CSRF -> 403 ({code})")

        check(f.http("POST", "/api/cloud", {"share": "1"}, token=tok)[0] == 200, "compartir encendido")
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 200 and json.loads(body).get("ok") is True, f"reporte de módem -> 200 ({code} {body})")
        ms = nube.a("/api/v1/reporte/modem")
        esperado = ('{"i":"%s","origen":"panel","fabricante":"huawei","dns_ok":true,"menu":"internet_wan"}' % iid).encode()
        check(len(ms) == 1 and ms[0]["metodo"] == "POST" and ms[0]["cuerpo"] == esperado,
              f"la nube recibió el cuerpo exacto ({ms[0]['cuerpo'] if ms else None!r})")
        check(len(ms) == 1 and ms[0]["headers"].get("content-type") == "application/json", "Content-Type: application/json")
        code, body, _ = f.http("POST", "/api/report/modem", dict(modem, dnsOk="0", menu="otro", fabricante="no-se"), token=tok)
        ms = nube.a("/api/v1/reporte/modem")
        check(code == 200 and len(ms) == 2 and b'"dns_ok":false' in ms[1]["cuerpo"] and b'"fabricante":"no-se"' in ms[1]["cuerpo"],
              f"dnsOk=0 y no-se -> dns_ok:false ({code})")

        antes = len(reportes())
        malos = [
            ("/api/report/modem", dict(modem, fabricante="linksys")),
            ("/api/report/modem", dict(modem, dnsOk="2")),
            ("/api/report/modem", dict(modem, dnsOk="")),
            ("/api/report/modem", dict(modem, menu="")),
            ("/api/report/modem", {"fabricante": "huawei", "menu": "internet_wan"}),
            ("/api/report/modem", dict(modem, fabricante='huawei","x":"y')),
            ("/api/report/blocked", {"domain": "ejemplo", "motivo": "otro"}),
            ("/api/report/blocked", {"domain": "a.com", "motivo": "otra"}),
            ("/api/report/blocked", {"domain": "a.com"}),
            ("/api/report/blocked", {"domain": "a_b.com", "motivo": "otro"}),
            ("/api/report/blocked", {"domain": "a.com\".x", "motivo": "otro"}),
            ("/api/report/blocked", {"domain": "x" * 300 + ".com", "motivo": "otro"}),
        ]
        for ruta, datos in malos:
            code, body, _ = f.http("POST", ruta, datos, token=tok)
            check(code == 400, f"{ruta} {str(datos)[:60]} -> 400 ({code})")
        code, _, _ = f.http("POST", "/api/report/blocked", raw=b"domain=a.com&motivo=otro&x=" + b"a" * 600, token=tok)
        check(code == 413, f"cuerpo de 600 B -> 413 ({code})")
        check(len(reportes()) == antes, "nada de eso salió a la nube")

        code, body, _ = f.http("POST", "/api/report/blocked", {"domain": " Ads.Ejemplo.com ", "motivo": "pagina_no_abre"}, token=tok)
        bs = nube.a("/api/v1/reporte/bloqueo")
        esperado = ('{"i":"%s","origen":"panel","dominio":"ads.ejemplo.com","motivo":"pagina_no_abre"}' % iid).encode()
        check(code == 200 and len(bs) == 1 and bs[0]["cuerpo"] == esperado,
              f"' Ads.Ejemplo.com ' -> 200 y la nube recibió ads.ejemplo.com ({code} {bs[0]['cuerpo'] if bs else None!r})")

        nube.respuestas["/api/v1/reporte/modem"] = (429, '{"error":"limite"}')
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 502 and '"limite"' in body, f"nube con 429 -> 502 limite ({code} {body})")
        nube.respuestas["/api/v1/reporte/modem"] = (400, '{"error":"datos"}')
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 502 and '"rechazado"' in body, f"nube con 400 -> 502 rechazado ({code} {body})")
        nube.respuestas["/api/v1/reporte/modem"] = (500, '{"error":"interno"}')
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 502 and '"rechazado"' in body, f"nube con 500 -> 502 rechazado ({code} {body})")
        nube.respuestas["/api/v1/reporte/modem"] = (302, '')
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 502 and '"rechazado"' in body, f"nube con 302 -> 502 rechazado, sin seguirla ({code})")
        nube.close()
        code, body, _ = f.http("POST", "/api/report/modem", modem, token=tok)
        check(code == 502 and '"sin_internet"' in body, f"nube caída -> 502 sin_internet ({code} {body})")
        code, body, _ = f.http("POST", "/api/report/blocked", {"domain": "ads.ejemplo.com", "motivo": "otro"}, token=tok)
        check(code == 502 and '"sin_internet"' in body, f"bloqueado con la nube caída -> 502 sin_internet ({code})")
    finally:
        f.close()
        try:
            nube.close()
        except Exception:
            pass


CSP_PANEL = ("default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
             "connect-src 'self'; img-src *.prueba.siblock.internal:*; frame-ancestors 'none'")
AVISO_COMPARTIR = (
    "Si lo activas, tu SiBlock manda a siblock.s-dev.win una vez al día un número al azar de esta instalación "
    "(no dice quién eres), la versión de SiBlock y cuántos anuncios quitó ese día. Y, solo cuando tú lo mandes, "
    "la marca de tu módem, si pudiste cambiar el DNS y en qué menú, o el sitio que no debió bloquearse. Nunca manda "
    "tu IP, la MAC, nombres de aparatos ni qué sitios visitas. El servidor ve tu país y tu compañía de internet por "
    "la conexión, pero no guarda tu IP. Con esta opción apagada, SiBlock no manda nada tuyo a siblock.s-dev.win (solo revisa si hay "
    "versión nueva, si activas «Avisarme de versiones nuevas»).")
AVISO_VERSION = ("Si lo activas, SiBlock revisa una vez al día si hay una versión nueva. Solo pide un archivo público; "
                 "no manda nada tuyo.")


def nube_panel():
    print("\n18c. Panel: Compartir con SiBlock")
    with Fresh(nvs=SEED_WIFI) as f:
        code, page, h = f.http("GET", "/")
        check(code == 200, f"la página carga ({code})")
        for texto in ("Compartir con SiBlock", AVISO_COMPARTIR, AVISO_VERSION, "Compartir datos anónimos",
                      "Avisarme de versiones nuevas", "No debió bloquearse", "reporte.html#i=",
                      "contacto.html#diag=", "Reportar desde la página", "Escríbenos",
                      "Hay una versión nueva de SiBlock (", "Se instala desde la página con el cable USB.",
                      "https://siblock.s-dev.win/#paso-instalar", "¿Pudiste cambiar el DNS?", ">Mandar<",
                      "Una página no abre", "Una app no funciona", "Un juego no funciona",
                      "Internet o WAN", "Red local, LAN o DHCP", "Avanzado", "Otro o no lo encontré",
                      "SiBlock no pudo llegar a internet. Prueba más tarde.",
                      "Mandaste muchos seguidos. Espera un minuto.", "No se pudo guardar. Prueba más tarde.",
                      "Usar los permitidos de la comunidad", 'id="comst"', "permitido por la comunidad",
                      "Tus sitios en «Bloquear» siempre ganan"):
            check(texto in page, f"la página trae «{texto[:60]}»")
        for fab in ("tp-link", "huawei", "zte", "technicolor", "arris", "netgear", "asus", "mikrotik", "proveedor", "no-se"):
            check(f'value="{fab}"' in page, f"el formulario trae el fabricante {fab}")
        for v in ("internet_wan", "red_local", "avanzado", "pagina_no_abre", "app_no_funciona", "juego_no_funciona"):
            check(f'value="{v}"' in page, f"el formulario trae {v}")
        # «¿Qué módem tienes?» no trae respuestas puestas: sin elegir no se manda nada.
        for sel, vacio in (("mfab", "Elige la marca"), ("mdns", "Elige una opción"), ("mmenu", "Elige el menú")):
            trozo = page.split(f'<select id="{sel}">', 1)[-1].split("</select>", 1)[0]
            check(trozo.startswith(f'<option value="" selected disabled>{vacio}</option>'),
                  f"«{sel}» empieza sin respuesta elegida")
            check(trozo.count("selected") == 1, f"«{sel}» no trae otra opción elegida de antemano")
        check("Revisa lo que elegiste." in page.split("$('msend').onclick", 1)[-1][:400],
              "«Mandar» sin elegir todo avisa y no manda")
        check(h.get("Content-Security-Policy") == CSP_PANEL, "la CSP no cambió")
        check(page.count("<script") == 1, f"un solo <script> ({page.count('<script')})")
        check("innerHTML" not in page, "sin innerHTML")
        # «Escríbenos» arma el enlace antes del toque: un window.open tras esperar lo bloquea Safari.
        check("window.open(" not in page and "preventDefault" not in page.split("$('esc').onclick", 1)[-1][:80],
              "«Escríbenos» sigue un enlace normal, sin window.open después de esperar")
        check("$('esc').href=NUBE+'contacto.html#diag='" in page, "el enlace con #diag= se arma antes del toque")


def nube_borrar():
    print("\n18d. Borrar la configuración apaga la nube y cambia el número")
    with NubeFalsa() as nube:
        env = {"SIM_CLOUD_URL": nube.url}
        f = Fresh(nvs=SEED_WIFI, env=env)
        try:
            tok = crear_clave(f, "clave-de-la-nube-3")
            code, body, _ = f.http("POST", "/api/cloud", {"share": "1", "versionCheck": "1", "community": "1"}, token=tok)
            st = json.loads(body) if code == 200 else {}
            check(st.get("share") is True and st.get("versionCheck") is True and st.get("community") is True,
                  f"las tres opciones encendidas ({code})")
            viejo = st.get("installId", "")
            # Una lista de la comunidad ya guardada (como si se hubiera bajado).
            with open(os.path.join(f.dir, "comunidad.bin"), "wb") as fh:
                fh.write(b"SBC1" + bytes(12))
            f.press(True)
            time.sleep(0.5)
            f.set_clock(11000)
            try:
                rc = f.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                rc = None
            f.press(False)
            check(rc == 3, f"BOOT 10 s borra la configuración y reinicia (código {rc})")
            nvs = f.nvs()
            check(not {"install_id", "cloud_share", "ver_check", "comunidad"} & set(nvs), f"NVS sin opciones de nube ({sorted(nvs)})")
            check(not os.path.exists(os.path.join(f.dir, "comunidad.bin")), "y sin /comunidad.bin")
            antes = len(nube.lista())
            with Fresh(dir=f.dir, env=env) as g:
                tok = crear_clave(g, "clave-de-la-nube-4")
                st = nube_estado(g, tok)
                nuevo = st.get("installId", "")
                check(st.get("share") is False and st.get("versionCheck") is False and st.get("community") is False and
                      st.get("communityCount") == 0, f"tras borrar: share, versionCheck y community apagados ({st})")
                check(len(nuevo) == 32 and nuevo != viejo, f"installId distinto ({viejo[:8]}… -> {nuevo[:8]}…)")
                check(st.get("latest") == "" and st.get("lastPing") == "never", "latest y lastPing vuelven a cero")
                adelantar(g, 11000, 25)
                check(len(nube.lista()) == antes, f"25 h sin peticiones ({len(nube.lista()) - antes})")
        finally:
            f.close()


def nube_guardar_falla():
    print("\n18e. Si no se puede guardar \"compartir\", no cambia nada")
    with NubeFalsa() as nube:
        env = {"SIM_CLOUD_URL": nube.url, "SIM_NVS_FAIL_KEY": "cloud_share"}
        with Fresh(nvs=SEED_WIFI, env=env) as f:
            tok = crear_clave(f, "clave-de-la-nube-5")
            code, body, _ = f.http("POST", "/api/cloud", {"share": "1", "versionCheck": "1"}, token=tok)
            check(code == 500 and json.loads(body).get("error") == "guardar", f"share falla al guardar -> 500 guardar ({code})")
            st = nube_estado(f, tok)
            check(st.get("share") is False and st.get("versionCheck") is False,
                  f"tras el 500 las dos siguen apagadas ({st})")
            nvs = f.nvs()
            check(nvs.get("ver_check") in (None, "0") and "cloud_share" not in nvs,
                  f"y en NVS no quedó nada encendido ({sorted(nvs)})")
            code, body, _ = f.http("POST", "/api/cloud", {"versionCheck": "1"}, token=tok)
            check(code == 200 and json.loads(body).get("versionCheck") is True, f"solo versionCheck sí se guarda ({code})")


def nube_comunidad():
    print("\n18f. Permitidos de la comunidad")
    lista = {"version": 3, "dominios": ["doubleclick.net", "otro.ejemplo.com"]}
    with NubeFalsa() as nube:
        env = {"SIM_CLOUD_URL": nube.url}
        nube.respuestas["/api/v1/permitidos"] = (200, json.dumps(lista))
        f = Fresh(nvs=SEED_WIFI, env=env)

        def respuesta(placa, name):
            try:
                r = dns.query.udp(dns.message.make_query(name, "A"), "127.0.0.1", port=placa.dns_port, timeout=4)
            except dns.exception.Timeout:
                return None
            return addrs(r)

        def bloqueado(placa, name="doubleclick.net"):
            return respuesta(placa, name) == ["0.0.0.0"]

        def pasa(placa, name="doubleclick.net"):
            a = respuesta(placa, name)
            return a is not None and a != ["0.0.0.0"]

        def veredictos(placa, tok, name):
            code, body, _ = placa.http("GET", "/api/detail", token=tok)
            return [r.get("v") for r in json.loads(body).get("recent", []) if r.get("d") == name] if code == 200 else []

        try:
            tok = crear_clave(f, "clave-comunidad-1")
            check(bloqueado(f), "de fábrica doubleclick.net está bloqueado (está en la lista)")
            code, body, _ = f.http("POST", "/api/cloud", {"community": "1"}, token=tok)
            st = json.loads(body) if code == 200 else {}
            check(code == 200 and st.get("community") is True and st.get("share") is False,
                  f"community=1 -> 200 sin encender «compartir» ({code} {body})")
            check(f.nvs().get("comunidad") == "1", "comunidad=1 en NVS")
            time.sleep(1)
            check(not nube.a("/api/v1/permitidos"), "al encender no sale nada enseguida (va al minuto)")
            check(bloqueado(f), "sin la lista bajada, sigue bloqueado")
            t = 61000
            f.set_clock(t)
            check(esperar(lambda: nube.a("/api/v1/permitidos")), "a los 61 s pide GET /api/v1/permitidos")
            ps = nube.a("/api/v1/permitidos")
            if ps:
                p = ps[0]
                iid = nube_estado(f, tok).get("installId", "")
                check(p["metodo"] == "GET" and p["cuerpo"] == b"", "GET sin cuerpo")
                check(iid and iid not in json.dumps(p["headers"]) and iid not in p["ruta"], "sin el número de instalación")
                check(p["headers"].get("user-agent", "").startswith("SiBlock/"), "User-Agent SiBlock/…")
            check(not nube.a("/api/v1/placa") and not nube.a("/version.json"), "y nada más sale")
            check(esperar(lambda: pasa(f)), "con la opción, doubleclick.net ya no se bloquea")
            check(pasa(f, "ad.doubleclick.net"), "y sus subdominios tampoco")
            check("comunidad" in veredictos(f, tok, "doubleclick.net"),
                  f"el registro dice «comunidad», no «permitido» ({veredictos(f, tok, 'doubleclick.net')})")
            check("permitido" not in veredictos(f, tok, "doubleclick.net"), "y no dice que lo permitiste tú")
            st = nube_estado(f, tok)
            check(st.get("communityCount") == 2 and st.get("communityUpdated", 0) > 1700000000,
                  f"communityCount=2 y communityUpdated con la hora ({st.get('communityCount')}, {st.get('communityUpdated')})")
            check(os.path.getsize(os.path.join(f.dir, "comunidad.bin")) == 16 + 2 * 8, "/comunidad.bin con 2 huellas")
            reglas = json.loads(f.http("GET", "/api/rules", token=tok)[1])
            check(reglas == {"blocked": [], "allowed": []}, f"no aparecen en «Mis reglas» ({reglas})")
            code, texto, _ = f.http("GET", "/api/backup", token=tok)
            check(code == 200 and "doubleclick" not in texto and "otro.ejemplo.com" not in texto, "ni en el respaldo")

            # Tu regla «Bloquear» gana (también sobre un padre).
            check(f.http("POST", "/api/rules", {"action": "block", "domain": "doubleclick.net"}, token=tok)[0] == 200,
                  "regla propia: bloquear doubleclick.net")
            check(bloqueado(f) and bloqueado(f, "ad.doubleclick.net"), "tu «Bloquear» gana sobre la comunidad")
            check("tuyo" in veredictos(f, tok, "doubleclick.net"), "y el registro dice «tuyo»")
            check(f.http("POST", "/api/rules", {"action": "remove", "domain": "doubleclick.net"}, token=tok)[0] == 200,
                  "quitar la regla")
            check(pasa(f), "sin tu regla vuelve a pasar")

            # Una lista mala no se usa: queda la de antes.
            nube.respuestas["/api/v1/permitidos"] = (200, '{"version":4,"dominios":["NO VALE"]}')
            t += 25 * 3600000
            f.set_clock(t)
            check(esperar(lambda: len(nube.a("/api/v1/permitidos")) == 2), "al día siguiente la pide otra vez")
            time.sleep(0.5)
            tok = entrar(f, "clave-comunidad-1")
            check(nube_estado(f, tok).get("communityCount") == 2 and pasa(f),
                  "una lista mal formada se rechaza entera y queda la anterior")

            # Sobrevive a un reinicio (aunque la nube no conteste).
            nube.respuestas["/api/v1/permitidos"] = (500, '{"error":"interno"}')
            detener(f)
            with Fresh(dir=f.dir, env=env) as g:
                check(pasa(g), "tras reiniciar, la lista guardada se usa enseguida")
                tok = entrar(g, "clave-comunidad-1")
                check(nube_estado(g, tok).get("communityCount") == 2, "communityCount=2 tras reiniciar")
                code, body, _ = g.http("POST", "/api/cloud", {"community": "0"}, token=tok)
                st = json.loads(body) if code == 200 else {}
                check(code == 200 and st.get("community") is False and st.get("communityCount") == 0,
                      f"community=0 -> 200 y lista vacía ({body})")
                check(bloqueado(g), "apagada: doubleclick.net vuelve a bloquearse")
                check(not os.path.exists(os.path.join(g.dir, "comunidad.bin")), "y /comunidad.bin se borra")
                antes = len(nube.a("/api/v1/permitidos"))
                adelantar(g, t, 25)
                check(len(nube.a("/api/v1/permitidos")) == antes, "apagada: 25 h sin pedir la lista")
        finally:
            f.close()


TG_TOKEN = "123456789:AAH-prueba_" + "x" * 24 + "Zq9"
TG_CHAT = "987654321"


def tg_estado(f, tok):
    code, body, _ = f.http("GET", "/api/telegram", token=tok)
    return (json.loads(body) if code == 200 else {}), body


def tg_envios(tg):
    return [p for p in tg.lista() if p["ruta"].endswith("/sendMessage")]


def tg_campos(p):
    import urllib.parse
    return dict(urllib.parse.parse_qsl(p["cuerpo"].decode()))


def tg_probar(f, tok, t):
    """Adelanta 31 s, pide el mensaje de prueba y espera a que se mande. Devuelve (código, reloj, estado)."""
    t += 31000
    f.set_clock(t)
    code = f.http("POST", "/api/telegram/test", token=tok)[0]
    esperar(lambda: not tg_estado(f, tok)[0].get("pending", True), 10)
    return code, t, tg_estado(f, tok)[0]


def nube_telegram():
    print("\n18g. Avisos por Telegram (servidor de Telegram de mentira)")
    ruta = f"/bot{TG_TOKEN}/sendMessage"
    salidas = []
    with NubeFalsa() as tg:
        env = {"SIM_TELEGRAM_URL": tg.url}
        f = Fresh(nvs=SEED_WIFI, env=env)
        try:
            tok = crear_clave(f, "clave-telegram-1")
            check(f.http("GET", "/api/telegram")[0] == 401, "GET /api/telegram sin sesión -> 401")
            check(f.http("GET", "/api/telegram", csrf=False, token=tok)[0] == 403, "sin anti-CSRF -> 403")
            check(f.http("POST", "/api/telegram", {"token": TG_TOKEN})[0] == 401, "POST sin sesión -> 401")
            check(f.http("POST", "/api/telegram/test", {}, token=False)[0] == 401, "prueba sin sesión -> 401")
            st, _ = tg_estado(f, tok)
            check(st == {"on": False, "tokenSaved": False, "chat": "", "reboot": True, "listFail": True, "weekly": True,
                         "last": "never", "lastCode": 0, "lastAt": 0, "pending": False},
                  f"de fábrica: apagado, sin token ni chat ({st})")

            _, page, _ = f.http("GET", "/")
            for texto in ("Avisos por Telegram", 'id="tgtok" type="password"', "Borrar el token",
                          "Mandar mensaje de prueba", "La placa se reinició", "La lista no se pudo actualizar",
                          "Resumen semanal", "Mandarme avisos por Telegram", "@BotFather"):
                check(texto in page, f"el panel trae «{texto}»")

            # Apagado no sale nada (ni con la lista fallando: SIM_HTTP_FAIL).
            check(f.http("POST", "/api/update", token=tok)[0] == 202, "pedir bajar la lista (va a fallar)")
            t = adelantar(f, 0, 25)
            tok = entrar(f, "clave-telegram-1")
            check(not tg.lista(), f"apagado: 25 h sin mandar nada ({len(tg.lista())})")
            check(b"[sim] telegram" not in f.output(), "el registro no anota ningún envío")

            # Revisión de lo que llega: si algo no vale, no se guarda nada.
            malos = ({"token": "123:abc"}, {"token": TG_TOKEN + "/x"}, {"token": "12345:" + "a" * 29},
                     {"chat": "abc"}, {"chat": "1" * 21}, {"chat": "12&text=x"}, {"on": "2"}, {"weekly": "si"},
                     {}, {"otra": "1"}, {"on": "1"}, {"on": "1", "token": TG_TOKEN},
                     {"token": TG_TOKEN, "clearToken": "1"}, {"token": TG_TOKEN, "chat": TG_CHAT, "reboot": "x"})
            for malo in malos:
                code, body, _ = f.http("POST", "/api/telegram", malo, token=tok)
                check(code == 400, f"POST /api/telegram {str(malo)[:60]} -> 400 ({code} {body[:40]})")
            code, body, _ = f.http("POST", "/api/telegram", {"on": "1"}, token=tok)
            check("falta_token" in body, f"encender sin token dice falta_token ({body})")
            check(not [k for k in f.nvs() if k.startswith("tg_")], f"tras los 400 no se guardó nada ({sorted(f.nvs())})")
            check(f.http("POST", "/api/telegram/test", token=tok)[0] == 409, "prueba sin token ni chat -> 409")

            # Guardar token y chat (con espacios alrededor, como al copiar y pegar).
            code, body, _ = f.http("POST", "/api/telegram", {"token": " " + TG_TOKEN + "\n", "chat": TG_CHAT,
                                                             "weekly": "0"}, token=tok)
            st = json.loads(body) if code == 200 else {}
            check(code == 200 and st.get("tokenSaved") is True and st.get("chat") == TG_CHAT and st.get("on") is False
                  and st.get("weekly") is False and st.get("reboot") is True, f"token y chat guardados, sigue apagado ({body})")
            check(TG_TOKEN not in body and TG_TOKEN.split(":")[1] not in body, "la respuesta no trae el token")
            check(f.nvs().get("tg_token") == TG_TOKEN and f.nvs().get("tg_chat") == TG_CHAT, "token y chat en la NVS")

            # Mensaje de prueba.
            code, t, st = tg_probar(f, tok, t)
            check(code == 202, f"POST /api/telegram/test -> 202 ({code})")
            envs = tg_envios(tg)
            check(len(envs) == 1 and envs[0]["ruta"] == ruta and envs[0]["metodo"] == "POST",
                  f"un POST a /bot<token>/sendMessage ({[e['ruta'][:12] for e in envs]})")
            if envs:
                c = tg_campos(envs[0])
                check(set(c) == {"chat_id", "text"} and c.get("chat_id") == TG_CHAT and "prueba" in c.get("text", ""),
                      f"chat_id y text de prueba ({c})")
                texto = envs[0]["cuerpo"].decode()
                check("127.0.0." not in texto and "simulador" not in texto and "CasaDemo" not in texto
                      and not re.search(r"([0-9a-f]{2}[:-]){5}[0-9a-f]{2}", texto, re.I), "sin IPs, MACs ni el WiFi")
                check("application/x-www-form-urlencoded" in envs[0]["headers"].get("content-type", ""), "como formulario")
                check(envs[0]["headers"].get("user-agent", "").startswith("SiBlock/"), "User-Agent SiBlock/…")
            check(st.get("last") == "ok" and st.get("lastCode") == 200 and st.get("lastAt", 0) > 1700000000,
                  f"el panel dice ok, 200 y cuándo ({st})")
            check(f.http("POST", "/api/telegram/test", token=tok)[0] == 429, "otra prueba enseguida -> 429 (una cada 30 s)")
            time.sleep(1)
            check(len(tg_envios(tg)) == 1, "y no sale")

            # Lo que contesta Telegram no llega al panel: solo el código.
            for codigo, nombre in ((401, "token"), (400, "chat"), (403, "bloqueado"), (500, "rechazado")):
                tg.respuestas[ruta] = (codigo, '{"ok":false,"description":"DETALLE-SECRETO de Telegram"}')
                _, t, st = tg_probar(f, tok, t)
                _, body = tg_estado(f, tok)
                check(st.get("last") == nombre and st.get("lastCode") == codigo and "DETALLE" not in body,
                      f"Telegram {codigo} -> last={nombre} sin repetir lo que contestó ({st.get('last')}, {st.get('lastCode')})")
            del tg.respuestas[ruta]

            # Tope de 20 por hora entre todos los envíos.
            antes = len(tg_envios(tg))
            vistos = None
            for _ in range(22):
                _, t, st = tg_probar(f, tok, t)
                if st.get("last") == "limite":
                    vistos = len(tg_envios(tg))
                    break
            check(vistos == 20, f"a los 20 envíos en una hora se corta ({vistos}, {antes} antes)")
            check(st.get("last") == "limite" and st.get("lastCode") == 429, f"el panel dice limite ({st})")
            t += 3600000
            f.set_clock(t)
            tok = entrar(f, "clave-telegram-1")
            _, t, st = tg_probar(f, tok, t)
            check(st.get("last") == "ok" and len(tg_envios(tg)) == 21, f"una hora después vuelve a salir ({st.get('last')})")

            # Encender y la lista que no se puede bajar (SIM_HTTP_FAIL): aviso a la tercera falla seguida.
            code, body, _ = f.http("POST", "/api/telegram", {"on": "1"}, token=tok)
            check(code == 200 and json.loads(body).get("on") is True and f.nvs().get("tg_on") == "1",
                  f"on=1 -> 200 ({code})")
            antes = len(tg_envios(tg))
            check(f.http("POST", "/api/update", token=tok)[0] == 202, "pedir bajar la lista")
            esperar(lambda: f.stats().get("error"), 10)
            for paso in (61000, 5 * 60000 + 1000):
                time.sleep(1)
                t += paso
                f.set_clock(t)
            avisos = lambda: [tg_campos(p).get("text", "") for p in tg_envios(tg)[antes:]]
            check(esperar(lambda: any("no pudo actualizar la lista" in x for x in avisos()), 15),
                  f"a la tercera falla avisa que la lista no se pudo actualizar ({avisos()})")
            n = len(tg_envios(tg))
            t += 30 * 60000 + 1000
            f.set_clock(t)
            time.sleep(3)
            check(len(tg_envios(tg)) == n, "la cuarta falla (30 min después) no repite el aviso")

            # El respaldo no lleva token ni chat, y no se aceptan al subirlo.
            tok = entrar(f, "clave-telegram-1")
            code, texto, _ = f.http("GET", "/api/backup", token=tok)
            check(code == 200 and TG_TOKEN not in texto and TG_TOKEN.split(":")[1] not in texto and TG_CHAT not in texto
                  and "telegram" not in texto.lower(), "el respaldo no trae el token ni el chat")
            malo = texto.replace("fin\n", "telegram " + TG_TOKEN + "\nfin\n")
            check(subir_pedazo(f, tok, 0, malo.encode())[0] == 200, "subir un respaldo con una línea de Telegram")
            code, body, _ = f.http("POST", "/api/restore/check", {}, token=tok)
            check(code == 400 and "no se entiende" in body, f"se rechaza entero ({code} {body[:60]})")

            # Ninguna respuesta del panel trae el token.
            for ruta_panel in ("/api/telegram", "/api/cloud", "/api/options", "/api/detail", "/api/stats",
                               "/api/history", "/api/devices", "/api/backup", "/"):
                _, body, _ = f.http("GET", ruta_panel, token=tok)
                check(TG_TOKEN.split(":")[1] not in body, f"GET {ruta_panel} no trae el token")

            # Reinicios: el aviso dice por qué, una vez por arranque.
            detener(f)
            salidas.append(f.output())
            antes = len(tg_envios(tg))
            with Fresh(dir=f.dir, env=dict(env, SIM_RESET_REASON="panic")) as g:
                check(esperar(lambda: len(tg_envios(tg)) > antes, 15), "tras reiniciar manda un aviso")
                txt = tg_campos(tg_envios(tg)[antes]).get("text", "") if len(tg_envios(tg)) > antes else ""
                check("se reinició" in txt and "falla y se recuperó sola" in txt and "Lista:" in txt,
                      f"«se reinició (tuvo una falla…)» con la lista ({txt})")
                time.sleep(2)
                check(len(tg_envios(tg)) == antes + 1, "uno solo por arranque")
                check(g.nvs().get("tg_fw") == "dev", "guarda la versión con la que arrancó")
                salidas.append(g.output())
            with open(os.path.join(f.dir, "nvs.json")) as fh:
                nvs = json.load(fh)
            nvs["siblock"]["tg_fw"] = "v0.0.1"
            with open(os.path.join(f.dir, "nvs.json"), "w") as fh:
                json.dump(nvs, fh)
            antes = len(tg_envios(tg))
            with Fresh(dir=f.dir, env=env) as g:
                check(esperar(lambda: len(tg_envios(tg)) > antes, 15), "otro arranque, otro aviso")
                txt = tg_campos(tg_envios(tg)[antes]).get("text", "") if len(tg_envios(tg)) > antes else ""
                check("(actualización)" in txt, f"con otra versión guardada dice «actualización» ({txt})")
                tok = entrar(g, "clave-telegram-1")
                # Sin el aviso de reinicio encendido, no sale.
                check(g.http("POST", "/api/telegram", {"reboot": "0"}, token=tok)[0] == 200, "reboot=0")
                salidas.append(g.output())
            antes = len(tg_envios(tg))
            with Fresh(dir=f.dir, env=env) as g:
                time.sleep(3)
                check(len(tg_envios(tg)) == antes, "con «La placa se reinició» apagado, no avisa")
                tok = entrar(g, "clave-telegram-1")

                # Borrar el token: apaga todo y la prueba ya no se puede.
                code, body, _ = g.http("POST", "/api/telegram", {"clearToken": "1"}, token=tok)
                st = json.loads(body) if code == 200 else {}
                check(code == 200 and st.get("tokenSaved") is False and st.get("on") is False,
                      f"clearToken=1 -> sin token y apagado ({body})")
                check("tg_token" not in g.nvs() and g.nvs().get("tg_on") == "0", "la NVS ya no tiene el token")
                check(g.http("POST", "/api/telegram/test", token=tok)[0] == 409, "prueba sin token -> 409")

                # Borrar la configuración (BOOT 10 s) borra todo lo de Telegram.
                check(g.http("POST", "/api/telegram", {"token": TG_TOKEN, "on": "1"}, token=tok)[0] == 200,
                      "token otra vez y encendido")
                g.press(True)
                time.sleep(0.5)
                g.set_clock(11000)
                try:
                    rc = g.proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    rc = None
                g.press(False)
                check(rc == 3, f"BOOT 10 s borra la configuración ({rc})")
                quedan = [k for k in g.nvs() if k.startswith("tg_")]
                check(not quedan, f"NVS sin nada de Telegram ({quedan})")
                salidas.append(g.output())
            with open(os.path.join(f.dir, ".clock_ms"), "w") as fh:
                fh.write("0")
            antes = len(tg_envios(tg))
            with Fresh(dir=f.dir, env=env) as g:
                time.sleep(3)
                check(len(tg_envios(tg)) == antes, "tras borrar, el arranque no manda nada")
                salidas.append(g.output())
        finally:
            f.close()

    # Sin SIM_TELEGRAM_URL el simulador no sale a api.telegram.org.
    with Fresh(nvs=SEED_WIFI) as h:
        tok = crear_clave(h, "clave-telegram-2")
        check(h.http("POST", "/api/telegram", {"token": TG_TOKEN, "chat": TG_CHAT}, token=tok)[0] == 200,
              "token y chat guardados")
        _, _, st = tg_probar(h, tok, 0)
        salida = h.output()
        check(b"[sim] telegram POST /bot<oculto>/sendMessage (sin SIM_TELEGRAM_URL: no sale)" in salida,
              "sin SIM_TELEGRAM_URL: se anota y no sale")
        check(st.get("last") == "sin_internet", f"el panel dice sin_internet ({st.get('last')})")
        salidas.append(salida)

    # El token no aparece en ningún registro (las placas de esta sección y sim/sim.log).
    log = os.path.join(HERE, "sim.log")
    if os.path.exists(log):
        with open(log, "rb") as fh:
            salidas.append(fh.read())
    secreto = TG_TOKEN.split(":")[1].encode()
    check(salidas and all(secreto not in x for x in salidas), f"el token no sale en los registros ({len(salidas)} revisados)")
    check(any(b"[sim] telegram POST /bot<oculto>/sendMessage" in x for x in salidas), "el registro tapa la ruta")


def nube():
    """Sección 18: la nube (Worker de mentira)."""
    nube_apagada()
    nube_opciones()
    nube_reportes()
    nube_panel()
    nube_borrar()
    nube_guardar_falla()
    nube_comunidad()
    nube_telegram()


# ---------- Fase 5: aparatos de la familia ----------

SAL_PRUEBA = bytes(range(16))


def huella(ultimo, sal=SAL_PRUEBA, mac=None):
    """Huella de un aparato: primeros 8 bytes de sha256(sal ‖ MAC), en hex."""
    mac = mac or bytes([2, 0, 0, 0, 0, ultimo])
    return hashlib.sha256(sal + mac).hexdigest()[:16]


def query_desde(port, name, source, rdtype="A", timeout=3):
    q = dns.message.make_query(name, rdtype, use_edns=0)
    return dns.query.udp(q, "127.0.0.1", port=port, timeout=timeout, source=source)


def minuto_utc():
    t = time.gmtime()
    return t.tm_hour * 60 + t.tm_min


def horario_ahora(dev_hex="00000001", grupos=4):
    """Horario de todos los días que abarca la hora UTC actual (±5 min)."""
    m = minuto_utc()
    return f"horario {dev_hex} {grupos} 127 {(m - 5) % 1440} {(m + 5) % 1440}"


def carpeta_con_familia(texto):
    d = tempfile.mkdtemp(prefix="siblock-sim-")
    with open(os.path.join(d, "nvs.json"), "w") as f:
        json.dump(SEED_WIFI, f)
    with open(os.path.join(d, "familia.txt"), "w") as f:
        f.write(texto)
    src = os.path.join(SIM_FLASH, "list.bin")
    if os.path.exists(src):
        shutil.copy(src, os.path.join(d, "list.bin"))
    return d


def bloqueado_desde(f, name, source):
    try:
        return addrs(query_desde(f.dns_port, name, source)) in (["0.0.0.0"], ["::"])
    except dns.exception.Timeout:
        return None


def familia_dns():
    print("\n19. Aparatos y horarios en el DNS (placa propia)")
    texto = ("SIBLOCK-FAMILIA 1\nzona 0\nsal " + SAL_PRUEBA.hex() + "\n"
             f"aparato 0 {huella(2)} Tele\n" + horario_ahora() + "\nfin\n")
    d = carpeta_con_familia(texto)
    try:
        with Fresh(dir=d) as f:
            tok = crear_clave(f, "clave-de-prueba-19")
            check(bloqueado_desde(f, "example.com", "127.0.0.2") is True,
                  "horario 'Todo internet' activo: example.com desde .2 -> 0.0.0.0")
            ultimas = lambda hh: [hh["h"][-1][0] + hh["h"][-2][0], hh["h"][-1][1] + hh["h"][-2][1]]
            code, body, _ = f.http("GET", "/api/history", token=tok)
            antes = ultimas(json.loads(body)) if code == 200 else [0, 0]
            for _ in range(3):
                bloqueado_desde(f, "example.com", "127.0.0.2")
            code, body, _ = f.http("GET", "/api/history", token=tok)
            h = json.loads(body) if code == 200 else {}
            despues = ultimas(h) if h else [0, 0]
            check(h.get("clock") is True and despues[0] - antes[0] >= 3 and despues[1] == antes[1],
                  f"lo que bloquea un horario suma consultas pero no bloqueadas ni ahorro ({antes} -> {despues})")
            check(bloqueado_desde(f, "example.com", "127.0.0.3") is False, "desde .3 (sin horario) -> IP real")
            with open(os.path.join(d, ".arp"), "w") as fh:
                fh.write("127.0.0.2 -\n")
            f.set_clock(6 * 60000)  # vence lo que la placa recuerda de la tabla ARP
            check(bloqueado_desde(f, "example.com", "127.0.0.2") is False,
                  "si la tabla ARP no tiene la IP -> no se reconoce y pasa (falla abierto)")
            out = f.output().decode(errors="replace")
            check("familia.txt" in out and "Tele" not in out, "lee /familia.txt y no imprime nombres por el cable")
        os.remove(os.path.join(d, ".arp"))
        with open(os.path.join(d, "familia.txt"), "w") as fh:
            fh.write(texto.replace("fin\n", ""))
        with Fresh(dir=d) as f:
            check(bloqueado_desde(f, "example.com", "127.0.0.2") is False,
                  "con /familia.txt cortado (sin 'fin') arranca y no bloquea a nadie")
            check(bloqueado_desde(f, "doubleclick.net", "127.0.0.2") is True, "la lista sigue bloqueando")
        with open(os.path.join(d, "familia.txt"), "w") as fh:
            fh.write(texto.replace("fin\n", "zona 0\n" * 4000 + "fin\n"))
        with Fresh(dir=d) as f:
            check(bloqueado_desde(f, "example.com", "127.0.0.2") is False, "con /familia.txt de más de 24 KB no bloquea a nadie")
            out = f.output().decode(errors="replace")
            check("demasiado grande" in out and "línea vacía" not in out,
                  "y la serie dice 'el archivo es demasiado grande' (no 'línea vacía')")
    finally:
        shutil.rmtree(d, ignore_errors=True)




def aparatos(f, tok):
    code, body, _ = f.http("GET", "/api/devices", token=tok)
    return json.loads(body) if code == 200 else {}


def fila_ip(lista, ip):
    return next((x for x in lista.get("devices", []) if x.get("ip") == ip), None)


def horarios(f, tok):
    code, body, _ = f.http("GET", "/api/schedules", token=tok)
    return json.loads(body) if code == 200 else {}


def familia_panel():
    print("\n20. Aparatos y horarios desde el panel (placa propia)")
    f = Fresh(nvs=SEED_WIFI)
    try:
        tok = crear_clave(f, "clave-de-prueba-20")
        for metodo, ruta in (("GET", "/api/devices"), ("POST", "/api/devices"), ("GET", "/api/schedules"),
                             ("POST", "/api/schedules")):
            check(f.http(metodo, ruta, {"action": "x"})[0] == 401, f"{metodo} {ruta} sin sesión -> 401")
            check(f.http(metodo, ruta, {"action": "x"}, csrf=False, token=tok)[0] == 403,
                  f"{metodo} {ruta} sin X-Requested-With -> 403")
        bloqueado_desde(f, "example.com", "127.0.0.2")
        bloqueado_desde(f, "example.com", "127.0.0.3")
        ap = aparatos(f, tok)
        fila = fila_ip(ap, "127.0.0.2")
        h = fila.get("h", "") if fila else ""
        check(fila is not None and len(h) == 16 and fila.get("s") == -1 and fila.get("n") == "",
              f"después de una consulta desde .2 aparece sin nombre y con su huella ({fila})")
        check("hora" in ap and "tz" in ap, f"/api/devices trae hora y tz ({sorted(ap)})")
        nombre = "<b>Tele</b> 📺"
        code, body, _ = f.http("POST", "/api/devices", {"action": "name", "h": h, "name": nombre}, token=tok)
        check(code == 200, f"ponerle nombre '<b>Tele</b> 📺' -> 200 ({code} {body})")
        code, body, _ = f.http("GET", "/api/devices", token=tok)
        fila = fila_ip(json.loads(body), "127.0.0.2") if code == 200 else None
        check(fila is not None and fila.get("n") == nombre and fila.get("s") == 0, f"se lee igual ({fila})")
        check(code == 200 and '"n":"<b>Tele</b> 📺"' in body, "el JSON lleva el nombre tal cual, como texto")
        code, body, _ = f.http("POST", "/api/devices", {"action": "name", "h": h, "name": "x" * 33}, token=tok)
        check(code == 400 and "muy largo" in body, f"nombre de 33 bytes -> 400 y dice que es muy largo ({code} {body})")
        code, body, _ = f.http("POST", "/api/devices", {"action": "name", "h": h, "name": "Cel de la abuela 📱📺🎮🎧"},
                               token=tok)
        check(code == 400 and "letras" not in body, f"21 letras pero 33 bytes -> 400 sin hablar de letras ({code} {body})")
        code, body, _ = f.http("POST", "/api/devices", {"action": "name", "h": h, "name": " a"}, token=tok)
        check(code == 400, f"nombre con espacio al inicio -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/devices", {"action": "name", "h": "0123456789abcdef", "name": "Falso"},
                               token=tok)
        check(code == 400, f"huella inventada -> 400 ({code} {body})")
        code, body, _ = f.http("POST", "/api/devices", {"action": "name", "h": "zz", "name": "Falso"}, token=tok)
        check(code == 400, f"huella mal escrita -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/devices", {"action": "otra", "h": h}, token=tok)
        check(code == 400, f"acción desconocida -> 400 ({code})")

        # Pausa de un aparato: solo ese deja pasar lo que la lista bloquea.
        check(bloqueado_desde(f, "doubleclick.net", "127.0.0.2") is True, "doubleclick.net bloqueado desde .2")
        for m in ("1441", "-1", "x", ""):
            code, _, _ = f.http("POST", "/api/devices", {"action": "pause", "h": h, "minutes": m}, token=tok)
            check(code == 400, f"pausa de '{m}' minutos -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/devices", {"action": "pause", "h": h, "minutes": "60"}, token=tok)
        check(code == 200, f"pausar .2 una hora -> 200 ({code} {body})")
        check(bloqueado_desde(f, "doubleclick.net", "127.0.0.2") is False, "en pausa: doubleclick.net pasa desde .2")
        check(bloqueado_desde(f, "doubleclick.net", "127.0.0.3") is True, "y sigue bloqueado desde .3")
        diag2 = f.http("GET", "/api/diagnose?probe=ok", token=False, headers={"X-Sim-Client": "127.0.0.2"})[1]
        diag3 = f.http("GET", "/api/diagnose?probe=ok", token=False, headers={"X-Sim-Client": "127.0.0.3"})[1]
        check("aparato_en_pausa" in diag2 and "aparato_en_pausa" not in diag3,
              f"«Algo no funciona» avisa de la pausa solo en ese aparato ({diag2} / {diag3})")
        pausa = (fila_ip(aparatos(f, tok), "127.0.0.2") or {}).get("pausa", 0)
        check(3500 < pausa <= 3600, f"/api/devices dice cuánto le queda ({pausa} s)")
        code, _, _ = f.http("POST", "/api/devices", {"action": "pause", "h": h, "minutes": "0"}, token=tok)
        check(code == 200 and bloqueado_desde(f, "doubleclick.net", "127.0.0.2") is True,
              "minutes=0 lo reanuda: vuelve a bloquear desde .2")

        # Horarios.
        m = minuto_utc()
        sched = {"action": "save", "i": "-1", "dev": "00000001", "g": "4", "d": "127",
                 "ini": str((m - 5) % 1440), "fin": str((m + 5) % 1440), "tz": "0"}
        for k, v in (("dev", "00000002"), ("dev", "1"), ("g", "0"), ("g", "8"), ("d", "0"), ("d", "128"),
                     ("ini", "1440"), ("fin", sched["ini"]), ("tz", "841"), ("tz", "-721"), ("i", "8"),
                     ("i", "0")):
            code, _, _ = f.http("POST", "/api/schedules", dict(sched, **{k: v}), token=tok)
            check(code == 400, f"horario con {k}={v} -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/schedules", sched, token=tok)
        check(code == 200, f"horario 'Todo internet' para .2 ahora -> 200 ({code} {body})")
        check(bloqueado_desde(f, "example.com", "127.0.0.2") is True, "example.com bloqueado desde .2")
        check(bloqueado_desde(f, "example.com", "127.0.0.3") is False, "y no desde .3")
        diag2 = f.http("GET", "/api/diagnose?probe=ok", token=False, headers={"X-Sim-Client": "127.0.0.2"})[1]
        diag3 = f.http("GET", "/api/diagnose?probe=ok", token=False, headers={"X-Sim-Client": "127.0.0.3"})[1]
        check("horario_todo" in diag2 and "todo_bien" not in diag2 and "horario" not in diag3,
              f"«Algo no funciona» dice que un horario le corta todo a .2 ({diag2} / {diag3})")
        st = f.stats()
        antes_b, antes_h = st.get("blocked", 0), st.get("schedule", 0)
        bloqueado_desde(f, "example.com", "127.0.0.2")
        st = f.stats()
        check(st.get("schedule", 0) == antes_h + 1 and st.get("blocked", 0) == antes_b,
              f"lo que corta un horario suma en 'schedule', no en 'blocked' ({st.get('schedule')} / {st.get('blocked')})")
        hs = horarios(f, tok)
        it = (hs.get("items") or [{}])[0]
        check(len(hs.get("items", [])) == 1 and it.get("dev") == "00000001" and it.get("g") == 4 and it.get("d") == 127
              and hs.get("tz") == 0, f"GET /api/schedules lo devuelve ({hs})")
        code, _, _ = f.http("POST", "/api/schedules", {"action": "delete", "i": "0"}, token=tok)
        check(code == 200 and bloqueado_desde(f, "example.com", "127.0.0.2") is False, "borrarlo -> ya no bloquea")
        check(f.http("POST", "/api/schedules", {"action": "delete", "i": "0"}, token=tok)[0] == 400,
              "borrar uno que no existe -> 400")
        f.http("POST", "/api/schedules", sched, token=tok)
        code, _, _ = f.http("POST", "/api/devices", {"action": "forget", "h": h}, token=tok)
        check(code == 200, f"olvidar el aparato -> 200 ({code})")
        check(horarios(f, tok).get("items") == [] and bloqueado_desde(f, "example.com", "127.0.0.2") is False,
              "olvidarlo borra también su horario")
        check(f.http("POST", "/api/devices", {"action": "forget", "h": h}, token=tok)[0] == 400,
              "olvidar uno sin nombre -> 400")

        # Reiniciar conserva nombre y horario y pierde la pausa.
        f.http("POST", "/api/devices", {"action": "name", "h": h, "name": "Tele"}, token=tok)
        redes = dict(sched, g="1")
        check(f.http("POST", "/api/schedules", redes, token=tok)[0] == 200, "horario de redes sociales para Tele")
        f.http("POST", "/api/devices", {"action": "pause", "h": h, "minutes": "60"}, token=tok)
        check(bloqueado_desde(f, "doubleclick.net", "127.0.0.2") is False, "Tele en pausa antes de reiniciar")
        fam = open(os.path.join(f.dir, "familia.txt"), encoding="utf-8").read()
        check("127.0.0." not in fam and "02:00" not in fam and "020000000002" not in fam,
              "/familia.txt no guarda IPs ni MACs")
        detener(f)
        with Fresh(dir=f.dir) as g:
            tok2 = entrar(g, "clave-de-prueba-20")
            ap = aparatos(g, tok2)
            tele = next((x for x in ap.get("devices", []) if x.get("n") == "Tele"), None)
            check(tele is not None and tele.get("h") == h, f"después de reiniciar sigue 'Tele' ({ap})")
            check(len(horarios(g, tok2).get("items", [])) == 1, "y su horario")
            check(bloqueado_desde(g, "doubleclick.net", "127.0.0.2") is True, "la pausa se perdió (bloquea otra vez)")
            check(bloqueado_desde(g, "www.instagram.com", "127.0.0.2") is True, "el horario de redes bloquea instagram")
            check(bloqueado_desde(g, "www.instagram.com", "127.0.0.3") is False, "solo para Tele")
            # El teléfono cambia de MAC (dirección privada que rota).
            code, _, _ = g.http("POST", "/api/schedules", dict(redes, i="0", g="4"), token=tok2)
            check(code == 200 and bloqueado_desde(g, "example.com", "127.0.0.2") is True,
                  "cambiar el horario a 'Todo internet' -> example.com bloqueado desde .2")
            with open(os.path.join(g.dir, ".arp"), "w") as fh:
                fh.write("127.0.0.2 02:aa:bb:cc:dd:ee\n")
            g.set_clock(6 * 60000)
            check(bloqueado_desde(g, "example.com", "127.0.0.2") is False,
                  "con otra MAC el horario de Tele ya no bloquea a esa IP")
            tok2 = entrar(g, "clave-de-prueba-20")
            ap = aparatos(g, tok2)
            nueva = fila_ip(ap, "127.0.0.2")
            check(nueva is not None and nueva.get("n") == "" and nueva.get("s") == -1 and nueva.get("h") not in ("", h),
                  f"aparece como aparato nuevo sin nombre ({nueva})")
            check(any(x.get("n") == "Tele" and x.get("ip") == "" for x in ap.get("devices", [])),
                  "y 'Tele' sigue en la lista, sin IP de ahora")
    finally:
        f.close()



def lineas_lista(f):
    return [l for l in f.output().decode(errors="replace").splitlines() if l.startswith("[lista] ") and "/" in l.split()[1]]


def listas_elegibles():
    print("\n21. Listas que se pueden elegir (placa propia)")
    nvs = {"siblock": dict(SEED_WIFI["siblock"], lists="7")}
    with Fresh(nvs=nvs) as f:
        tok = crear_clave(f, "clave-de-prueba-21")
        code, body, _ = f.http("GET", "/api/options", token=tok)
        o = json.loads(body) if code == 200 else {}
        check(o.get("lists") == 7 and o.get("listsOffer") == 7 and "skipped" in o, f"GET /api/options trae lists=7 ({o})")
        f.http("POST", "/api/update", token=tok)
        check(esperar(lambda: len(lineas_lista(f)) > 0), "pedir 'Actualizar lista' empieza a bajar")
        ls = lineas_lista(f)
        primera = ls[0] if ls else ""
        check(primera.startswith("[lista] 1/4 ") and "hagezi" in primera and "light" not in primera,
              f"con las 3 elegidas son 4 descargas y la 1/4 es la de estafas ({primera[:90]})")
        check(not any(l.startswith("[lista] 2/4") for l in ls), "la descarga falla y no sigue con las demás")
        for v in ("2", "0", "8", "9", "x", ""):
            code, _, _ = f.http("POST", "/api/options", {"lists": v}, token=tok)
            check(code == 400, f"lists={v!r} -> 400 ({code})")
        code, body, _ = f.http("POST", "/api/options", {"lists": "3"}, token=tok)
        check(code == 200 and f.nvs().get("lists") == "3", f"lists=3 -> 200 y queda en NVS ({code} {f.nvs().get('lists')})")
        o = json.loads(f.http("GET", "/api/options", token=tok)[1])
        check(o.get("lists") == 3, f"GET /api/options la devuelve ({o.get('lists')})")
        check(esperar(lambda: any(l.startswith("[lista] 1/3 ") for l in lineas_lista(f))),
              "al cambiar las listas pide bajar otra vez (ahora 3 descargas)")
        code, _, _ = f.http("POST", "/api/options", {"lists": "3", "relayBlock": "1"}, token=tok)
        check(code == 200 and f.nvs().get("relay_block") == "1", "lists y relayBlock juntos -> 200")
        code, _, _ = f.http("POST", "/api/options", {"lists": "2", "relayBlock": "0"}, token=tok)
        check(code == 400 and f.nvs().get("relay_block") == "1", "si uno no vale, no se cambia ninguno")
        check(f.http("POST", "/api/options", {}, token=tok)[0] == 400, "sin campos -> 400")
    nvs = {"siblock": dict(SEED_WIFI["siblock"], lists="9")}
    with Fresh(nvs=nvs) as f:
        tok = crear_clave(f, "clave-de-prueba-21")
        o = json.loads(f.http("GET", "/api/options", token=tok)[1])
        check(o.get("lists") == 1, f"con lists=9 en NVS se comporta como 1 ({o.get('lists')})")
        f.http("POST", "/api/update", token=tok)
        check(esperar(lambda: len(lineas_lista(f)) > 0), "pide bajar")
        ls = lineas_lista(f)
        check(bool(ls) and ls[0].startswith("[lista] 1/2 ") and "StevenBlack" in ls[0], f"solo la general ({ls[:1]})")



def lineas_led(f, desde=0):
    """(segundos, valor) de cada '[sim] led' de la salida, desde el byte `desde`."""
    out = []
    for l in f.output()[desde:].decode(errors="replace").splitlines():
        if l.startswith("[sim] led "):
            out.append(l.split()[-1])
    return out


def luz_y_ahorro():
    print("\n22. LED de estado y 'cuánto te ahorra' (placas propias)")
    with Fresh(env={"SIM_WIFI": "down"}) as f:
        desde = len(f.output())
        time.sleep(1.2)
        leds = lineas_led(f, desde)
        check("1" in leds and "0" in leds and len(leds) >= 4,
              f"sin WiFi: parpadeo rápido (en 1,2 s: {len(leds)} cambios {leds[:8]})")
        check(len(leds) <= 24, f"y solo escribe el pin al cambiar ({len(leds)} líneas en 1,2 s)")
    with Fresh(nvs=SEED_WIFI) as f:
        check(esperar(lambda: "1" in lineas_led(f)), "con WiFi y lista: el LED se prende")
        time.sleep(0.5)
        desde = len(f.output())
        time.sleep(3)
        leds = lineas_led(f, desde)
        check("0" not in leds, f"y queda fijo 3 s, sin apagarse ({leds})")
        f.press(True)
        time.sleep(0.8)
        f.press(False)
        time.sleep(0.5)
        check("0" in lineas_led(f, desde), "con BOOT apretado manda el parpadeo de siempre")
        desde = len(f.output())
        time.sleep(1.5)
        check("0" not in lineas_led(f, desde), "al soltar BOOT vuelve a quedar fijo")
        tok = crear_clave(f, "clave-de-prueba-22")
        code, body, _ = f.http("GET", "/api/history", token=tok)
        h = json.loads(body) if code == 200 else {}
        check(isinstance(h.get("ahorroMb"), int) and h["ahorroMb"] >= 0, f"/api/history trae ahorroMb entero ({h.get('ahorroMb')})")
        code, page, _ = f.http("GET", "/")
        check("Aproximado: en 7 días tu casa se ahorró unos " in page and "https://siblock.s-dev.win/#luz" in page
              and "¿Qué dice la luz?" in page, "la página trae el ahorro y '¿Qué dice la luz?'")
        check("Es una ayuda, no un candado" in page and "VPN se lo salta" in page,
              "la tarjeta de Horarios avisa que un horario se puede saltar")



def subir_pedazo(f, tok, n, data):
    # El número de pedazo va en el header X-Parte: en modo raw el servidor no lee la URL.
    return f.http("POST", "/api/restore/part", raw=data, token=tok,
                  headers={"Content-Type": "text/plain; charset=utf-8", "X-Parte": str(n)})


def partir(texto, piezas=2):
    """Parte el texto en `piezas` pedazos en finales de línea."""
    lineas = texto.encode().splitlines(keepends=True)
    k = max(1, len(lineas) // piezas)
    return [b"".join(lineas[:k]), b"".join(lineas[k:])]


def respaldo():
    print("\n23. Respaldo y restauración (placas propias)")
    with Fresh(nvs=SEED_WIFI) as a:
        tok = crear_clave(a, "clave-de-prueba-23")
        for d in ("regla-uno.example", "regla-dos.example"):
            a.http("POST", "/api/rules", {"action": "block" if "uno" in d else "allow", "domain": d}, token=tok)
        bloqueado_desde(a, "example.com", "127.0.0.2")
        h = (fila_ip(aparatos(a, tok), "127.0.0.2") or {}).get("h", "")
        a.http("POST", "/api/devices", {"action": "name", "h": h, "name": "Cel de mamá 📱"}, token=tok)
        m = minuto_utc()
        a.http("POST", "/api/schedules", {"action": "save", "i": "-1", "dev": "00000001", "g": "1", "d": "127",
                                          "ini": str((m + 60) % 1440), "fin": str((m + 120) % 1440), "tz": "-360"},
               token=tok)
        check(a.http("GET", "/api/backup")[0] == 401, "GET /api/backup sin sesión -> 401")
        check(a.http("GET", "/api/backup", csrf=False, token=tok)[0] == 403, "sin X-Requested-With -> 403")
        code, texto, hdr = a.http("GET", "/api/backup", token=tok)
        disp = hdr.get("Content-Disposition", "")
        check(code == 200 and texto.startswith("SIBLOCK-RESPALDO 1\n") and texto.endswith("fin\n"),
              f"con sesión: empieza con SIBLOCK-RESPALDO 1 y termina en fin ({code})")
        check("text/plain" in hdr.get("Content-Type", "") and "no-store" in hdr.get("Cache-Control", ""),
              "texto plano y sin caché")
        check('attachment; filename="siblock-respaldo-' in disp and disp.endswith('.txt"'),
              f"se baja como siblock-respaldo-AAAA-MM-DD.txt ({disp})")
        nvs = a.nvs()
        check("simulador" not in texto and "127.0.0." not in texto and "clave-de-prueba" not in texto
              and (not nvs.get("install_id") or nvs["install_id"] not in texto),
              "no trae el WiFi, la contraseña, el número de instalación ni IPs")
        check("bloquear regla-uno.example" in texto and "permitir regla-dos.example" in texto
              and "Cel de mamá 📱" in texto and "horario 00000001 1 127" in texto and "zona -360" in texto,
              "trae reglas, aparato, horario y zona")
        reglas_a = json.loads(a.http("GET", "/api/rules", token=tok)[1])
        horarios_a = horarios(a, tok)
        largo = "a" * 60 + "." + "b" * 60 + "." + "c" * 60
        codes = {a.http("POST", "/api/rules", {"action": "block", "domain": f"{largo}.n{i}.example"}, token=tok)[0]
                 for i in range(198)}
        code, body, _ = a.http("GET", "/api/backup", token=tok)
        check(codes == {200} and code == 500 and "demasiado grande" in body,
              f"200 reglas largas: /api/backup da 500 en vez de un archivo que no se podría subir ({codes} {code})")

    f = Fresh(nvs=SEED_WIFI)
    try:
        tok = crear_clave(f, "clave-de-prueba-23")
        p1, p2 = partir(texto)
        code, _, _ = subir_pedazo(f, None, 0, p1)
        check(code == 401 and not os.path.exists(os.path.join(f.dir, "restore.tmp")),
              f"pedazo sin sesión -> 401 y no escribe /restore.tmp ({code})")
        code, _, _ = f.http("POST", "/api/restore/part", raw=p1, csrf=False, token=tok, headers={"X-Parte": "0"})
        check(code == 403 and not os.path.exists(os.path.join(f.dir, "restore.tmp")), "pedazo sin anti-CSRF -> 403 y no escribe")
        check(subir_pedazo(f, tok, 0, p1)[0] == 200, "pedazo 0 -> 200")
        check(subir_pedazo(f, tok, 2, p2)[0] == 400, "pedazo 2 sin el 1 -> 400")
        check(subir_pedazo(f, tok, 0, p1)[0] == 200 and subir_pedazo(f, tok, 1, p2)[0] == 200, "otra vez 0 y 1 -> 200")
        code, body, _ = f.http("POST", "/api/restore/check", token=tok)
        r = json.loads(body) if body.startswith("{") else {}
        check(code == 200 and r.get("ok") and r.get("reglas") == 2 and r.get("aparatos") == 1 and r.get("horarios") == 1
              and r.get("listas") == 1, f"check da los números ({code} {r})")
        check(f.http("POST", "/api/restore/apply", {}, token=tok)[0] == 400, "apply sin confirm=1 -> 400")
        code, body, _ = f.http("POST", "/api/restore/apply", {"confirm": "1"}, token=tok)
        check(code == 200, f"apply -> 200 ({code} {body})")
        check(json.loads(f.http("GET", "/api/rules", token=tok)[1]) == reglas_a, "las reglas quedan iguales")
        check(horarios(f, tok) == horarios_a, "el horario y la zona quedan iguales")
        bloqueado_desde(f, "example.com", "127.0.0.2")
        tele = fila_ip(aparatos(f, tok), "127.0.0.2") or {}
        check(tele.get("n") == "Cel de mamá 📱" and tele.get("h") == h, f"el aparato se reconoce con su nombre ({tele})")
        check(not os.path.exists(os.path.join(f.dir, "restore.tmp")), "borra el archivo temporal")

        malo = texto.replace("fin\n", "")
        f.http("POST", "/api/rules", {"action": "block", "domain": "otra.example"}, token=tok)
        antes = json.loads(f.http("GET", "/api/rules", token=tok)[1])
        subir_pedazo(f, tok, 0, malo.encode())
        code, body, _ = f.http("POST", "/api/restore/check", token=tok)
        r = json.loads(body) if body.startswith("{") else {}
        check(code == 400 and r.get("ok") is False and "cortado" in r.get("error", ""), f"sin 'fin': check da error ({r})")
        code, _, _ = f.http("POST", "/api/restore/apply", {"confirm": "1"}, token=tok)
        check(code == 400 and json.loads(f.http("GET", "/api/rules", token=tok)[1]) == antes, "y apply no cambia nada")
        subir_pedazo(f, tok, 0, p1)
        with open(os.path.join(f.dir, "restore.tmp"), "wb") as fh:
            fh.write(b"SIBLOCK-RESPALDO 1\n" + b"bloquear x.example\n" * 1400)
        code, body, _ = f.http("POST", "/api/restore/check", token=tok)
        r = json.loads(body) if body.startswith("{") else {}
        check(code == 400 and r.get("error") == "el archivo es demasiado grande" and r.get("linea") == 0,
              f"/restore.tmp de más de 24 KB: 'el archivo es demasiado grande', sin número de línea ({r})")
        code, _, _ = subir_pedazo(f, tok, 0, b"x" * 3073)
        check(code == 413 and not os.path.exists(os.path.join(f.dir, "restore.tmp")), f"pedazo de 3073 B -> 413 ({code})")
        codes = [subir_pedazo(f, tok, i, b"a" * 2999 + b"\n")[0] for i in range(9)]
        check(codes[-1] == 413 and not os.path.exists(os.path.join(f.dir, "restore.tmp")),
              f"más de 24 KB en total -> 413 y borra el temporal ({codes})")
        check(f.http("POST", "/api/restore/check", token=tok)[0] == 400, "check sin archivo -> 400")
        check(os.path.exists(os.path.join(f.dir, "familia.txt")), "B tiene /familia.txt")
        f.press(True)
        time.sleep(0.5)
        f.set_clock(11000)
        try:
            rc = f.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            rc = None
        f.press(False)
        check(rc == 3 and not os.path.exists(os.path.join(f.dir, "familia.txt")),
              f"BOOT 10 s borra /familia.txt ({rc})")
    finally:
        f.close()


def familia_flash_falla():
    print("\n20b. Si la flash no guarda, el cambio de aparatos u horarios no queda puesto")
    f = Fresh(nvs=SEED_WIFI)
    try:
        tok = crear_clave(f, "clave-de-prueba-20b")
        bloqueado_desde(f, "example.com", "127.0.0.2")
        h = (fila_ip(aparatos(f, tok), "127.0.0.2") or {}).get("h", "")
        code, _, _ = f.http("POST", "/api/devices", {"action": "name", "h": h, "name": "Tele"}, token=tok)
        check(code == 200, f"nombre 'Tele' con la flash bien -> 200 ({code})")
        detener(f)
        with Fresh(dir=f.dir, env={"SIM_FS_FAIL_PATH": "/familia.new"}) as g:
            tok2 = entrar(g, "clave-de-prueba-20b")
            bloqueado_desde(g, "example.com", "127.0.0.2")
            code, _, _ = g.http("POST", "/api/devices", {"action": "name", "h": h, "name": "Otro"}, token=tok2)
            tele = fila_ip(aparatos(g, tok2), "127.0.0.2") or {}
            check(code == 500 and tele.get("n") == "Tele", f"renombrar sin poder guardar -> 500 y sigue 'Tele' ({code} {tele})")
            m = minuto_utc()
            sched = {"action": "save", "i": "-1", "dev": "00000001", "g": "4", "d": "127",
                     "ini": str((m - 5) % 1440), "fin": str((m + 5) % 1440), "tz": "60"}
            code, _, _ = g.http("POST", "/api/schedules", sched, token=tok2)
            hs = horarios(g, tok2)
            check(code == 500 and hs.get("items") == [] and hs.get("tz") == 0,
                  f"horario sin poder guardar -> 500, sin horario y sin cambiar la hora ({code} {hs})")
            check(bloqueado_desde(g, "example.com", "127.0.0.2") is False, "y no bloquea nada")
            code, _, _ = g.http("POST", "/api/devices", {"action": "forget", "h": h}, token=tok2)
            tele = fila_ip(aparatos(g, tok2), "127.0.0.2") or {}
            check(code == 500 and tele.get("n") == "Tele", f"olvidar sin poder guardar -> 500 y sigue 'Tele' ({code} {tele})")
    finally:
        f.close()


def fase5():
    """Secciones 19 a 23 (fase 5): aparatos, horarios, listas, LED y respaldo."""
    familia_dns()
    familia_panel()
    familia_flash_falla()
    listas_elegibles()
    luz_y_ahorro()
    respaldo()


def nuevas():
    """Secciones con placas simuladas propias (no necesitan el simulador principal)."""
    primer_uso()
    boton_boot()
    improv_usb()
    portal_wifi()
    relay_placa_nueva()
    comprobacion_placa_nueva()
    nube()
    fase5()


def main():
    import urllib.parse  # noqa: F401  (usado por http)

    if "--solo-telegram" in sys.argv:
        nube_telegram()
        print(f"\n{'TODO BIEN' if not fails else f'{fails} FALLAS'}")
        return 1 if fails else 0

    if "--solo-nube" in sys.argv:
        nube()
        print(f"\n{'TODO BIEN' if not fails else f'{fails} FALLAS'}")
        return 1 if fails else 0

    if "--solo-fase5" in sys.argv:
        fase5()
        print(f"\n{'TODO BIEN' if not fails else f'{fails} FALLAS'}")
        return 1 if fails else 0

    if "--solo-nuevas" in sys.argv:  # para iterar rápido: solo las secciones 12 en adelante
        nuevas()
        print(f"\n{'TODO BIEN' if not fails else f'{fails} FALLAS'}")
        return 1 if fails else 0

    print("\n0. Simulador")
    check(os.path.isfile(os.path.join(SIM_FLASH, "nvs.json")), "nvs.json existe tras arrancar")
    check(os.path.isfile(FRESH) and os.access(FRESH, os.X_OK), "sim/esp32-sim-fresh existe y es ejecutable")

    print("\n1. Bloqueo por lista")
    r = query("doubleclick.net")
    check(addrs(r) == ["0.0.0.0"], f"doubleclick.net A -> 0.0.0.0 ({addrs(r)})")
    r = query("ad.doubleclick.net", "AAAA")
    check(addrs(r) == ["::"], f"subdominio AAAA -> :: ({addrs(r)})")
    r = query("doubleclick.net", "TXT")
    check(r.rcode() == 0 and not r.answer, "TXT bloqueado -> NOERROR vacío")
    check(r.id is not None and r.question[0].name.to_text() == "doubleclick.net.", "respuesta trae la pregunta")
    r = query("DoubleClick.NET")
    check(addrs(r) == ["0.0.0.0"], "mayúsculas también se bloquean")

    print("\n2. Reenvío al DNS real")
    r = query("example.com")
    check(len(addrs(r)) > 0 and "0.0.0.0" not in addrs(r), f"example.com -> IP real {addrs(r)}")
    r = query("github.com", "AAAA")
    check(r.rcode() == 0, "AAAA permitido contesta")
    r = query("no-existe-xyz-123.example")
    check(r.rcode() == 3, "dominio inexistente -> NXDOMAIN del DNS real")

    print("\n2b. Caché")
    before = stats()["cached"]
    r1 = query("wikipedia.org")
    time.sleep(1.2)
    t = time.perf_counter()
    r2 = query("wikipedia.org")
    dt = (time.perf_counter() - t) * 1000
    check(stats()["cached"] == before + 1, f"la segunda vez sale de la caché ({dt:.1f} ms)")
    check(addrs(r1) == addrs(r2), "misma respuesta")
    ttl1, ttl2 = r1.answer[0].ttl, r2.answer[0].ttl
    check(ttl2 < ttl1 or ttl1 <= 1, f"el TTL baja con el tiempo ({ttl1} -> {ttl2})")
    r3 = query("wikipedia.org", "AAAA")
    check(r3.rcode() == 0 and stats()["cached"] == before + 1, "otro tipo (AAAA) no se mezcla en la caché")

    print("\n2c. DNS por TCP")
    q = dns.message.make_query("doubleclick.net", "A")
    r = dns.query.tcp(q, DNS[0], port=DNS[1], timeout=5)
    check(addrs(r) == ["0.0.0.0"], "TCP: bloqueado -> 0.0.0.0")
    q = dns.message.make_query("example.com", "A")
    r = dns.query.tcp(q, DNS[0], port=DNS[1], timeout=8)
    check(len(addrs(r)) > 0 and "0.0.0.0" not in addrs(r), f"TCP: reenviado -> {addrs(r)}")
    q = dns.message.make_query("github.com", "TXT")
    r = dns.query.tcp(q, DNS[0], port=DNS[1], timeout=8)
    check(r.rcode() == 0 and len(r.answer) > 0, f"TCP: TXT grande ({len(r.to_wire())} bytes)")
    check(stats()["tcp"] >= 3, "contador TCP")

    print("\n3. Paquetes raros (no deben tumbarlo)")
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.5)
    for junk in [b"", b"\x00", b"\xff" * 12, os.urandom(300), b"\x12\x34\x01\x00\x00\x02" + b"\x00" * 6]:
        s.sendto(junk, DNS)
        try:
            s.recvfrom(2048)
        except socket.timeout:
            pass
    r = query("example.com")
    check(len(addrs(r)) > 0, "sigue vivo después de basura")

    print("\n4. Panel: permisos")
    global TOKEN
    code, body, hdr = http("GET", "/", csrf=False, token=False)
    check(code == 200 and "SiBlock" in body, "página carga (dice SiBlock)")
    check("frame-ancestors 'none'" in hdr.get("Content-Security-Policy", ""), "trae CSP")
    for i in ("comprobar", "algo", "hallazgos", "grafica", "ver24", "ver7", "top", "saltan", "relay"):
        check(f'id="{i}"' in body, f"la página trae #{i}")
    check("prueba.siblock.internal" in body, "la página carga la imagen de prueba")
    check("innerHTML" not in body, "la página no usa innerHTML")
    check("Puedes activar la opción de abajo" not in body,
          "el aviso de Private Relay no manda a una opción que sin contraseña no se ve")
    for txt in (
        "Este aparato usa SiBlock.",
        "Este aparato pregunta a SiBlock, pero esta revisión no pasó por SiBlock. Puede ser que este navegador use su propio DNS (\"DNS seguro\"): apágalo en sus ajustes. O que tu módem o router reparta otro DNS por IPv6 (mira el aviso de IPv6).",
        "Este aparato no pregunta a SiBlock. Revisa que tu módem o router dé la dirección de SiBlock como DNS y desconecta y vuelve a conectar el WiFi del aparato.",
        "Si tu módem o router tiene IPv6, apaga su DNS por IPv6: los aparatos lo usan para saltarse SiBlock.",
        "Este aparato no ha preguntado nada a SiBlock en los últimos 10 minutos.",
        "Este aparato usa un DNS cifrado y se salta SiBlock. Apaga \"DNS privado\" o \"DNS seguro\" en sus ajustes.",
        "Este aparato usa iCloud Private Relay y se salta SiBlock. Apágalo en el iPhone o Mac, o quien tenga la contraseña del panel puede activar «Apagar iCloud Private Relay en esta casa».",
        "SiBlock está en pausa: no bloquea nada hasta que termine.",
        "SiBlock todavía no tiene su lista de anuncios. Revisa que tenga internet y espera unos minutos.",
        "SiBlock no logra hablar con internet. Revisa el módem.",
        "Todo llega desde tu módem o router: así SiBlock no ve cada aparato. Lo mejor es que reparta la dirección de SiBlock directo.",
        "No encontré nada raro.",
        "✅ Este aparato está usando SiBlock.",
        "❌ Este aparato todavía no usa SiBlock.",
        "Comprobar otra vez",
        "Algo no funciona",
        "Todavía no hay datos. Aparecen cuando SiBlock tenga la hora de internet.",
        "Todavía nada bloqueado.",
        "Apagar iCloud Private Relay en esta casa",
        "Safari en iPhone y Mac dejará de usar Private Relay en tu WiFi: SiBlock podrá bloquear sus anuncios, pero pierdes la privacidad extra de Private Relay.",
        "https://siblock.s-dev.win/guia-router.html",
        "https://siblock.s-dev.win/obstaculos.html",
        "'Ver qué hacer':'Ver la guía'",  # el enlace a problemas comunes no se llama "guía"
        "navegador_dns_propio:OBST",  # el aviso de DNS del navegador lleva a cómo apagarlo
        "El nombre es muy largo: usa uno más corto (los acentos y los emoji ocupan más).",
        "las listas elegidas y la opción de Private Relay. No guarda el WiFi",
        "reemplaza tus reglas, aparatos, horarios, listas y la opción de Private Relay.",
    ):
        check(txt in body, f"la página dice: {txt[:50]}")
    check(http("POST", "/api/pause", {"minutes": 5}, csrf=False, password=PASSWORD)[0] == 403, "sin header anti-CSRF -> 403")
    check(http("POST", "/api/pause", {"minutes": 5}, token=False)[0] == 401, "sin contraseña ni sesión -> 401")
    check(http("POST", "/api/pause", {"minutes": 5}, password="mala")[0] == 401, "contraseña mala -> 401")
    check(http("POST", "/api/pause", {"minutes": 5}, headers={"X-Token": "0" * 32})[0] == 401, "token inventado -> 401")
    code, body, _ = http("POST", "/api/login", password=PASSWORD)
    TOKEN = json.loads(body).get("token") if code == 200 else None
    check(code == 200 and TOKEN and len(TOKEN) == 32, "contraseña buena -> token de sesión")
    check(http("GET", "/api/pause", csrf=False)[0] == 404, "GET a una acción no hace nada")
    st = stats()
    check("recent" not in st and "clients" not in st, "estadísticas públicas: solo contadores")
    check(http("GET", "/api/detail", token=False)[0] == 401, "detalle (consultas, aparatos) sin sesión -> 401")
    check(http("GET", "/api/rules", token=False)[0] == 401, "reglas sin sesión -> 401")
    check("recent" in detail(), "detalle con sesión -> 200")

    print("\n4b. Panel: Host, tamaño y bloqueo por IP")
    for path in ("/", "/api/stats", "/api/detail"):
        check(http("GET", path, headers={"Host": "evil.example"})[0] == 403, f"Host ajeno en {path} -> 403 (anti-rebinding)")
    check(http("GET", "/api/stats", headers={"Host": "adblock.local"})[0] == 200, "Host adblock.local -> 200 (nombre de antes)")
    check(http("GET", "/api/stats", headers={"Host": "siblock.local"})[0] == 200, "Host siblock.local -> 200")
    check(http("GET", "/", headers={"Host": "siblock"})[0] == 200, "Host siblock -> 200")
    code = http("POST", "/api/rules", raw=b"action=block&domain=" + b"a" * 2000,
                headers={"Content-Type": "application/x-www-form-urlencoded"})[0]
    check(code == 413, f"cuerpo de 2 KB -> 413 sin guardarlo entero ({code})")
    check(http("GET", "/api/stats", token=False)[0] == 200, "después de un 413 lo demás sigue andando")

    def closed(**kw):
        try:
            return http(**kw)[0] is None
        except Exception:
            return True
    check(closed(method="POST", path="/api/rules", raw=b"a" * 200000,
                 headers={"Content-Type": "application/x-www-form-urlencoded"}),
          "cuerpo de 200 KB -> conexión cerrada sin leerlo")
    check(closed(method="GET", path="/api/stats", headers={"X-Basura": "a" * 5000}),
          "header de 5 KB -> conexión cerrada")
    check(closed(method="POST", path="/api/rules", raw=b"--x\r\nContent-Disposition: form-data; name=\"action\"\r\n\r\nblock\r\n--x--\r\n",
                 headers={"Content-Type": "multipart/form-data; boundary=x"}),
          "multipart -> conexión cerrada")
    check(stats()["queries"] > 0, "sigue vivo después de todo eso")
    ip = f"192.168.77.{int(time.time()) % 200 + 20}"
    codes = [http("POST", "/api/login", password="mala", headers={"X-Sim-Client": ip})[0] for _ in range(5)]
    check(codes == [401] * 5, "5 contraseñas malas -> 401")
    check(http("POST", "/api/login", password=PASSWORD, headers={"X-Sim-Client": ip})[0] == 429,
          "esa IP queda bloqueada aunque ahora mande la buena -> 429")
    check(http("POST", "/api/login", password=PASSWORD, headers={"X-Sim-Client": "192.168.77.5"})[0] == 200,
          "otra IP de la casa sigue entrando")
    check(http("GET", "/api/rules")[0] == 200, "la sesión abierta sigue sirviendo")
    # R8: llenar las 16 casillas con IPs bloqueadas; la 17.ª no puede soltar a nadie.
    for i in range(15):
        for _ in range(5):
            http("POST", "/api/login", password="mala", headers={"X-Sim-Client": f"10.9.9.{i + 1}"})
    nueva = {"X-Sim-Client": "10.9.8.1"}
    check(http("POST", "/api/login", password="mala", headers=nueva)[0] == 401, "tabla llena: IP nueva prueba una vez")
    check(http("POST", "/api/login", password="mala", headers={"X-Sim-Client": "10.9.8.2"})[0] == 429,
          "tabla llena: otra IP enseguida -> 429 (turno compartido)")
    check(http("POST", "/api/login", password=PASSWORD, headers={"X-Sim-Client": "10.9.9.1"})[0] == 429,
          "las IPs bloqueadas siguen bloqueadas")
    check(http("POST", "/api/login", password=PASSWORD, headers={"X-Sim-Client": "192.168.77.5"})[0] == 200,
          "una IP que ya entró antes no espera el turno")
    check(http("POST", "/api/login", password="mala", headers={"X-Sim-Client": "192.168.77.5"})[0] == 401,
          "tabla llena: la IP que ya entró manda una mala -> 401")
    check(http("POST", "/api/login", password=PASSWORD, headers={"X-Sim-Client": "192.168.77.5"})[0] == 429,
          "tras una mala pierde el pase y espera el turno compartido -> 429")
    time.sleep(5.5)
    check(http("POST", "/api/login", password=PASSWORD, headers=nueva)[0] == 200,
          "a los 5 s el dueño entra desde una IP nueva")

    print("\n5. Reglas propias")
    check(http("POST", "/api/rules", {"action": "block", "domain": "Example.ORG"})[0] == 200, "bloquear example.org")
    check(addrs(query("www.example.org")) == ["0.0.0.0"], "www.example.org bloqueado")
    check(http("POST", "/api/rules", {"action": "allow", "domain": "doubleclick.net"})[0] == 200, "permitir doubleclick.net")
    r = query("doubleclick.net")
    check("0.0.0.0" not in addrs(r), f"doubleclick.net ya pasa {addrs(r)}")
    rules = json.loads(http("GET", "/api/rules")[1])
    check(rules == {"blocked": ["example.org"], "allowed": ["doubleclick.net"]}, f"lista de reglas {rules}")
    check(http("POST", "/api/rules", {"action": "block", "domain": "<script>x"})[0] == 400, "dominio con basura -> 400")
    check(http("POST", "/api/rules", {"action": "nada", "domain": "a.com"})[0] == 400, "acción rara -> 400")
    check(http("POST", "/api/rules", {"action": "remove", "domain": "doubleclick.net"})[0] == 200, "quitar regla")
    check(addrs(query("doubleclick.net")) == ["0.0.0.0"], "doubleclick.net vuelve a bloquearse")

    print("\n5b. Nombres especiales y Private Relay")
    before = stats().get("canary", 0)
    for t in ("A", "AAAA"):
        r = query("use-application-dns.net", t)
        check(r.rcode() == 3 and not r.answer, f"canario de Firefox {t} -> NXDOMAIN sin respuestas (rcode {r.rcode()})")
    check(stats().get("canary", 0) == before + 2, f"contador canary sube 2 ({stats().get('canary')})")
    r = query("ab12cd34ef56.prueba.siblock.internal")
    check(addrs(r) == ["127.0.0.1"] and r.answer and r.answer[0].ttl == 0,
          f"nombre de prueba A -> IP de la placa con TTL 0 ({addrs(r)})")
    r = query("ab12cd34ef56.prueba.siblock.internal", "AAAA")
    check(r.rcode() == 0 and not r.answer, "nombre de prueba AAAA -> NOERROR vacío")
    r = query("mask.icloud.com")
    check(r.rcode() == 0, f"Private Relay apagado de fábrica: mask.icloud.com no es NXDOMAIN (rcode {r.rcode()})")
    code, body, _ = http("GET", "/api/options")
    check(code == 200 and json.loads(body).get("relayBlock") is False, f"/api/options -> relayBlock false ({code} {body})")
    check(http("GET", "/api/options", token=False)[0] == 401, "/api/options sin sesión -> 401")
    check(http("POST", "/api/options", {"relayBlock": "1"}, token=False)[0] == 401, "POST /api/options sin sesión -> 401")
    check(http("POST", "/api/options", {"relayBlock": "1"}, csrf=False)[0] == 403, "POST /api/options sin anti-CSRF -> 403")
    for bad in ({"relayBlock": "2"}, {}, {"relayBlock": ""}, {"relayBlock": "true"}):
        code, body, _ = http("POST", "/api/options", bad)
        check(code == 400 and "valor inválido" in body, f"POST /api/options {bad} -> 400 ({code})")
    check(json.loads(http("GET", "/api/options")[1]).get("relayBlock") is False, "y sigue apagado")
    before = stats().get("relay", 0)
    code, body, _ = http("POST", "/api/options", {"relayBlock": "1"})
    check(code == 200 and json.loads(body) == {"ok": True, "relayBlock": True},
          f"activar Private Relay -> 200 con lo que dice api.md ({code} {body})")
    check(json.loads(http("GET", "/api/options")[1]).get("relayBlock") is True, "/api/options dice true")
    for n in ("mask.icloud.com", "mask-h2.icloud.com"):
        r = query(n)
        check(r.rcode() == 3 and not r.answer, f"activado: {n} -> NXDOMAIN (rcode {r.rcode()})")
    check(stats().get("relay", 0) >= before + 2, f"contador relay sube ({stats().get('relay')})")
    check(http("POST", "/api/pause", {"minutes": 5})[0] == 200, "pausar 5 min")
    check(query("mask.icloud.com").rcode() == 3, "en pausa sigue NXDOMAIN (la persona lo activó)")
    check(query("use-application-dns.net").rcode() == 3, "en pausa el canario sigue")
    check(http("POST", "/api/pause", {"minutes": 0})[0] == 200, "reanudar")
    code, body, _ = http("POST", "/api/options", {"relayBlock": "0"})
    check(code == 200 and json.loads(body) == {"ok": True, "relayBlock": False},
          f"apagar Private Relay -> 200 con lo que dice api.md ({code} {body})")
    check(query("mask.icloud.com").rcode() == 0, "apagado: mask.icloud.com contesta otra vez")

    # DDR (RFC 9462): SiBlock no anuncia DNS cifrado; resolver.arpa nunca va al DNS de arriba.
    s0 = stats()
    by_ip = lambda: sorted(k for b in detail().get("bypass", []) if b.get("ip") == "127.0.0.1" for k in b.get("k", []))
    by0 = by_ip()
    for n, t in (("_dns.resolver.arpa", "SVCB"), ("_dns.resolver.arpa", "A"), ("resolver.arpa", "AAAA"),
                 ("_853._dns.resolver.arpa", "SVCB")):
        r = query(n, t)
        check(r.rcode() == 0 and not r.answer and not r.authority,
              f"DDR {n} {t} -> NOERROR sin respuestas (rcode {r.rcode()}, {len(r.answer)} respuestas)")
    s1 = stats()
    check(s1.get("forwarded") == s0.get("forwarded"), f"DDR: nada se reenvió ({s0.get('forwarded')} -> {s1.get('forwarded')})")
    check(s1.get("blocked") == s0.get("blocked"), "DDR: no cuenta como bloqueada")
    check(by_ip() == by0, f"DDR: no marca el aparato como que se salta SiBlock ({by0} -> {by_ip()})")
    check(not any("resolver.arpa" in x.get("d", "") for x in detail().get("recent", [])),
          "DDR: no sale en las últimas consultas")

    print("\n5c. Comprobación, diagnóstico e historia")
    def gif_get(host=None):
        h = {"Host": host} if host else {}
        try:
            with urllib.request.urlopen(urllib.request.Request(WEB + "/siblock-prueba.gif", headers=h),
                                        timeout=10) as raw:
                return raw.status, raw.read(), dict(raw.headers)
        except urllib.error.HTTPError as e:
            return e.code, b"", dict(e.headers)

    code, gif, gh = gif_get("ab12cd34ef56.prueba.siblock.internal:8080")
    check(code == 200, f"GIF de prueba con Host de prueba -> 200 ({code})")
    check(gif.startswith(b"GIF89a") and len(gif) == 43, f"es un GIF de 43 B ({len(gif)} B)")
    check(gh.get("Content-Type", "").startswith("image/gif"), "Content-Type image/gif")
    check("no-store" in gh.get("Cache-Control", ""), "Cache-Control no-store")
    check(gif_get()[0] == 200, "GIF por la IP de la placa -> 200")
    check(gif_get("AB12.Prueba.Siblock.Internal")[0] == 200, "GIF con Host de prueba en mayúsculas -> 200")
    for bad in ("x.prueba.siblock.internal.evil.com", "xprueba.siblock.internal", "evil.example",
                "a.prueba.siblock.internal.", "a_b.prueba.siblock.internal", "prueba.siblock.internal"):
        check(gif_get(bad)[0] == 403,
              f"GIF con Host {bad} -> 403")
    for path in ("/api/stats", "/", "/api/diagnose?probe=ok", "/api/history"):
        check(http("GET", path, headers={"Host": "ab12.prueba.siblock.internal"})[0] == 403,
              f"{path} con Host de prueba -> 403")
    csp = http("GET", "/", csrf=False, token=False)[2].get("Content-Security-Policy", "")
    check("img-src *.prueba.siblock.internal:*" in csp, f"CSP con img-src de prueba ({csp})")

    query("example.com")
    code, body, _ = http("GET", "/api/diagnose?probe=ok", token=False)
    d = json.loads(body) if code == 200 else {}
    codes = [f["code"] for f in d.get("findings", [])]
    check(code == 200 and "usa_siblock" in codes, f"/api/diagnose?probe=ok sin sesión -> usa_siblock ({code} {codes})")
    check(not any(f["level"] == "bad" for f in d.get("findings", [])), f"sin hallazgos bad ({d})")
    check(http("GET", "/api/diagnose?probe=ok", csrf=False, token=False)[0] == 403, "/api/diagnose sin anti-CSRF -> 403")
    code, body, _ = http("GET", "/api/diagnose?probe=fail", token=False)
    check("navegador_dns_propio" in body, f"probe=fail desde un aparato que sí pregunta -> navegador_dns_propio")
    check('"ipv6"' in body, f"probe=fail desde un aparato que sí pregunta -> también el aviso de IPv6 ({body})")
    code, body, _ = http("GET", "/api/diagnose?probe=fail", token=False, headers={"X-Sim-Client": "192.168.1.77"})
    d = json.loads(body) if code == 200 else {}
    f2 = d.get("findings", [])
    lv = [f["level"] for f in f2]
    check({"code": "no_usa_siblock", "level": "bad"} in f2 and {"code": "ipv6", "level": "info"} in f2,
          f"otro aparato que no pregunta -> no_usa_siblock (bad) e ipv6 (info) ({f2})")
    order = {"bad": 0, "warn": 1, "info": 2, "ok": 3}
    check(lv == sorted(lv, key=order.get), f"bad antes que info ({lv})")

    # Pasos según el sistema (por el User-Agent de esa petición; no se guarda ni se devuelve).
    UA = {
        "windows": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
        "mac": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Safari/605.1.15",
        "iphone": "Mozilla/5.0 (iPhone; CPU iPhone OS 18_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Mobile/15E148 Safari/604.1",
        "android": "Mozilla/5.0 (Linux; Android 14; SAMSUNG SM-S918B) AppleWebKit/537.36 (KHTML, like Gecko) SamsungBrowser/26.0 Chrome/122.0.0.0 Mobile Safari/537.36",
        "otro": "Python-urllib/3",
    }
    esperado = {
        "windows": ["windows_dns_cifrado", "antivirus_dns", "vpn"],
        "mac": ["apple_perfil_dns", "vpn"],
        "iphone": ["apple_perfil_dns", "vpn"],
        "android": ["android_dns_privado", "vpn"],
        "otro": ["revisar_aparato"],
    }
    pasos = set(c for v in esperado.values() for c in v)
    for so, ua in UA.items():
        code, body, _ = http("GET", "/api/diagnose?probe=fail", token=False,
                             headers={"X-Sim-Client": "192.168.1.77", "User-Agent": ua})
        fs = json.loads(body).get("findings", []) if code == 200 else []
        cs = [f["code"] for f in fs]
        extra = [c for c in cs if c in pasos]
        check(extra == esperado[so] and all(f["level"] == "info" for f in fs if f["code"] in pasos)
              and cs[0] == "no_usa_siblock",
              f"{so}: no usa SiBlock -> pasos {esperado[so]} ({cs})")
        check(ua not in body and "Mozilla" not in body, f"{so}: la respuesta no trae el User-Agent")
        code, body, _ = http("GET", "/api/diagnose?probe=ok", token=False, headers={"User-Agent": ua})
        cs = [f["code"] for f in json.loads(body).get("findings", [])] if code == 200 else []
        check(not any(c in pasos for c in cs), f"{so}: si usa SiBlock no hay pasos ({cs})")

    def strings(o):
        if isinstance(o, dict):
            for k, v in o.items():
                yield k
                yield from strings(v)
        elif isinstance(o, list):
            for v in o:
                yield from strings(v)
        elif isinstance(o, str):
            yield o
    check(not any("." in x for x in strings(d)), "el diagnóstico no trae dominios ni IPs")
    code, body, _ = http("GET", "/api/diagnose?probe=" + "x" * 300, token=False)
    check(code == 200 and "findings" in body, "probe raro -> se ignora")

    query("dns.google")
    dt = detail()
    by = dt.get("bypass", [])
    check(any(b.get("ip") == "127.0.0.1" and "doh" in b.get("k", []) and isinstance(b.get("ago"), int) for b in by),
          f"/api/detail trae el aparato que usa DNS cifrado ({by})")
    code, body, _ = http("GET", "/api/diagnose?probe=ok", token=False)
    check("dns_cifrado" in body, "/api/diagnose avisa del DNS cifrado")

    check(http("GET", "/api/history", token=False)[0] == 401, "/api/history sin sesión -> 401")
    check(http("GET", "/api/history", csrf=False)[0] == 403, "/api/history sin anti-CSRF -> 403")
    code, body, _ = http("GET", "/api/history")
    hist = json.loads(body) if code == 200 else {}
    check(code == 200 and len(hist.get("h", [])) == 168 and hist.get("clock") is True and hist.get("hour", 0) > 472222,
          f"/api/history: 168 horas con reloj ({code}, {len(hist.get('h', []))}, {hist.get('clock')})")
    for _ in range(3):
        query("doubleclick.net")
    hist2 = json.loads(http("GET", "/api/history")[1])
    last = lambda hh: [hh["h"][-1][0] + hh["h"][-2][0], hh["h"][-1][1] + hh["h"][-2][1]]
    a, b = last(hist), last(hist2)
    check(b[0] - a[0] >= 3 and b[1] - a[1] >= 3, f"la última hora sube con 3 bloqueadas ({a} -> {b})")
    top = hist2.get("top", [])
    check(len(top) <= 10 and top and top[0].get("d") == "doubleclick.net" and top[0].get("n", 0) >= 3,
          f"top 1 es doubleclick.net ({top[:2]})")
    largo = "a" * 58 + ".largo.example"  # 72 caracteres
    check(http("POST", "/api/rules", {"action": "block", "domain": largo})[0] == 200, "bloquear un dominio de 72 caracteres")
    for _ in range(2):
        query(largo)
    top = json.loads(http("GET", "/api/history")[1]).get("top", [])
    check(any(t["d"] == largo[:63] for t in top), f"aparece en el top cortado a 63 ({[t['d'] for t in top]})")
    http("POST", "/api/rules", {"action": "remove", "domain": largo})

    print("\n6. Pausa")
    check(http("POST", "/api/pause", {"minutes": 9999})[0] == 400, "minutos fuera de rango -> 400")
    check(http("POST", "/api/pause", {"minutes": 5})[0] == 200, "pausar 5 min")
    check(stats()["paused"] > 290, "panel dice que está en pausa")
    check("0.0.0.0" not in addrs(query("doubleclick.net")), "en pausa: doubleclick.net pasa")
    check(addrs(query("www.example.org")) != ["0.0.0.0"], "en pausa: regla propia tampoco bloquea")
    check(http("POST", "/api/pause", {"minutes": 0})[0] == 200, "reanudar")
    check(addrs(query("doubleclick.net")) == ["0.0.0.0"], "reanudado: bloquea otra vez")

    print("\n7. Nombres maliciosos en el registro")
    evil = dns.message.make_query("x", "A")
    evil.question[0].name = dns.name.Name([b'"><img/src=x/onerror=alert(1)>', b"com", b""])
    dns.query.udp(evil, DNS[0], port=DNS[1], timeout=3)
    spaced = dns.message.make_query("x", "A")
    spaced.question[0].name = dns.name.Name([b"a b", b"com", b""])
    try:
        dns.query.udp(spaced, DNS[0], port=DNS[1], timeout=1)
        check(False, "nombre con espacio se descarta")
    except dns.exception.Timeout:
        check(True, "nombre con espacio se descarta")
    st = detail()  # si el JSON se rompe, esto lanza excepción
    check(any("onerror" in q["d"] for q in st["recent"]), "el nombre aparece escapado y el JSON sigue válido")

    print("\n7b. Anti DNS-rebinding y nombres tramposos")
    r = query("192-168-1-10.nip.io")
    check(r.rcode() == 5 and not r.answer, f"dominio de internet que apunta a 192.168.x -> REFUSED (rcode {r.rcode()})")
    check(stats()["rebind"] >= 1, "contador rebind")
    check(http("POST", "/api/rules", {"action": "allow", "domain": "nip.io"})[0] == 200, "permitir nip.io a mano")
    r = query("192-168-1-10.nip.io")
    check(addrs(r) == ["192.168.1.10"], f"permitido a mano: sí puede apuntar a la casa {addrs(r)}")
    http("POST", "/api/rules", {"action": "remove", "domain": "nip.io"})
    r = query("192-168-1-10.nip.io")
    check(r.rcode() == 5, "al quitar la regla, la respuesta guardada en caché no sobrevive (REFUSED)")
    sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sk.settimeout(2)
    sk.sendto(bytes([0, 9, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 11]) + b"example.com" + bytes([0, 0, 1, 0, 1]), DNS)
    try:
        sk.recvfrom(1500)
        got = True
    except socket.timeout:
        got = False
    r = query("example.com")
    check(r.rcode() == 0 and addrs(r), f"etiqueta 'example.com' (con punto) no envenena la caché de example.com (contestó: {got})")

    print("\n8. Carga: 3000 consultas, 50 a la vez")
    names = ["doubleclick.net", "googleads.g.doubleclick.net", "example.com", "github.com",
             "wikipedia.org", "facebook.com", "connect.facebook.net", "ads.yahoo.com"]
    lat, errors = [], 0

    def one(i):
        t = time.perf_counter()
        try:
            query(names[i % len(names)], timeout=4)
            return time.perf_counter() - t
        except Exception:
            return None

    t0 = time.perf_counter()
    with cf.ThreadPoolExecutor(50) as ex:
        for v in ex.map(one, range(3000)):
            if v is None:
                errors += 1
            else:
                lat.append(v)
    dur = time.perf_counter() - t0
    lat.sort()
    print(f"     {len(lat)} ok, {errors} sin respuesta, {len(lat)/dur:.0f} consultas/s, "
          f"mediana {statistics.median(lat)*1000:.1f} ms, p99 {lat[int(len(lat)*0.99)]*1000:.1f} ms")
    check(errors <= 30, "menos de 1% sin respuesta")
    st = stats()
    check(st["queries"] > 3000 and st["blocked"] > 1000, f"contadores {st['queries']} / {st['blocked']}")

    dt = detail()
    cl = dt["clients"]
    check(len(cl) >= 1 and cl[0]["ip"] == "127.0.0.1" and cl[0]["q"] > 3000, f"tabla de aparatos {cl[:1]}")
    check(all("c" in r for r in dt["recent"]), "las últimas consultas dicen de qué aparato vienen")
    check(st["cacheEntries"] <= 64, f"caché acotada ({st['cacheEntries']} entradas)")

    print("\n9. Actualizar la lista desde el panel")
    before = stats()["domains"]
    check(http("POST", "/api/update")[0] == 202, "pedir actualización")
    deadline = time.time() + 120
    while time.time() < deadline and stats()["updating"]:
        r = query("doubleclick.net")  # el DNS sigue contestando mientras se actualiza
        time.sleep(0.3)
    st = stats()
    check(not st["updating"] and abs(st["domains"] - before) < before * 0.05 and not st["error"], f"lista nueva: {st['domains']} dominios")
    check(addrs(query("doubleclick.net")) == ["0.0.0.0"], "bloquea con la lista nueva")

    print("\n11. Configuración guardada")
    with open(os.path.join(SIM_FLASH, "nvs.json"), "rb") as f:
        raw_nvs = f.read()
    nvs = json.loads(raw_nvs).get("siblock", {})
    check(nvs.get("panel_hash", "").startswith("hex:") and len(nvs["panel_hash"]) == 4 + 64, "NVS tiene panel_hash (32 B)")
    check(nvs.get("panel_salt", "").startswith("hex:") and len(nvs["panel_salt"]) == 4 + 32, "NVS tiene panel_salt (16 B)")
    check(nvs.get("panel_iter") == "10000", f"panel_iter = 10000 ({nvs.get('panel_iter')})")
    check(nvs.get("wifi_ssid") == "simulador", "wifi_ssid sembrado desde secrets.h")
    check(PASSWORD.encode() not in raw_nvs, "la contraseña del panel no está en texto en NVS")
    check(stats().get("version") == "dev", f"/api/stats trae version = dev ({stats().get('version')})")

    nuevas()

    if "--lento" in sys.argv:
        print("\n10. La pausa vence sola")
        check(http("POST", "/api/pause", {"minutes": 1})[0] == 200, "pausar 1 min")
        check("0.0.0.0" not in addrs(query("doubleclick.net")), "en pausa: pasa")
        time.sleep(62)
        check(stats()["paused"] == 0, "a los 62 s el panel dice activo")
        check(addrs(query("doubleclick.net")) == ["0.0.0.0"], "y vuelve a bloquear")

    print(f"\n{'TODO BIEN' if not fails else f'{fails} FALLAS'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

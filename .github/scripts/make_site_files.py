#!/usr/bin/env python3
"""Escribe version.json y js/config.js de la página de SiBlock al publicar una versión.

Uso: make_site_files.py --version vX.Y.Z --out <carpeta> [--wa-token T] [--turnstile-sitekey K]
                          [--apoyo-url URL]

- <out>/version.json: {"version":"vX.Y.Z","url":"https://siblock.s-dev.win/#paso-instalar"}
  (la placa lo lee para avisar que hay versión nueva).
- <out>/js/config.js: la misma línea `export` que site/js/config.js (contrato entre carriles),
  con el token de Cloudflare Web Analytics, la clave pública de Turnstile y el enlace de apoyo
  (variable del repo SIBLOCK_APOYO_URL). Vacío = la página no carga ese servicio ni muestra
  el botón de apoyo.

Los valores son públicos (van en la página), pero entran en un archivo JavaScript: el token y
la clave solo aceptan [A-Za-z0-9_-] (hasta 200); el enlace de apoyo solo acepta
https://<host>[/<ruta>] con host [A-Za-z0-9.-] (hasta 100) y ruta [A-Za-z0-9._~/-] (hasta
150), sin usuario, puerto, '?' ni '#'. Así ninguno puede cerrar la comilla ni meter código.
Se escriben con json.dumps y luego entre comillas simples, igual que el archivo del repo.
La versión sigue la misma regla que la placa (parseSemver): cada parte 0..9999, sin ceros
a la izquierda. Sale con 2 (sin escribir nada ni mostrar el valor) si algo no cumple.
"""
import argparse
import json
import os
import re
import sys

URL_INSTALAR = "https://siblock.s-dev.win/#paso-instalar"
PARTE = r"(?:0|[1-9][0-9]{0,3})"
VERSION_RE = re.compile(rf"v{PARTE}\.{PARTE}\.{PARTE}")
VALOR_RE = re.compile(r"[A-Za-z0-9_-]{0,200}")
APOYO_RE = re.compile(r"(?:https://[A-Za-z0-9.-]{1,100}(?:/[A-Za-z0-9._~/-]{0,150})?)?")

CABECERA = (
    "// Configuración pública de la página. La escribió CI al publicar (make_site_files.py):\n"
    "// token de Cloudflare Web Analytics, clave pública de Turnstile y enlace de apoyo.\n"
    "// Vacío = no se carga nada (sin enlace de apoyo no se muestra el botón).\n"
)


def js_string(valor):
    # json.dumps escapa todo; como el valor ya pasó VALOR_RE o APOYO_RE (sin comillas, '\'
    # ni caracteres de control; '/' no se escapa), el resultado es "valor" y
    # se pasa a comillas simples para que el archivo quede igual al del repo.
    return "'" + json.dumps(valor)[1:-1] + "'"


def version_json(version):
    return json.dumps({"version": version, "url": URL_INSTALAR}, separators=(",", ":")) + "\n"


def config_js(token, clave, apoyo=""):
    return (
        CABECERA
        + "export const CONFIG = Object.freeze({ api: '/api/v1', "
        + f"analiticaToken: {js_string(token)}, turnstileClave: {js_string(clave)}, "
        + f"apoyoUrl: {js_string(apoyo)} }});\n"
    )


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--wa-token", default="")
    ap.add_argument("--turnstile-sitekey", default="")
    ap.add_argument("--apoyo-url", default="")
    args = ap.parse_args(argv)  # argumentos que faltan: argparse sale con 2

    if not VERSION_RE.fullmatch(args.version):
        print(f"versión no válida: {args.version!r} (se espera vX.Y.Z)", file=sys.stderr)
        return 2
    for nombre, valor in (("--wa-token", args.wa_token),
                          ("--turnstile-sitekey", args.turnstile_sitekey)):
        if not VALOR_RE.fullmatch(valor):
            # No se imprime el valor: solo el nombre de la opción.
            print(f"{nombre} no válido: solo letras, números, '_' y '-' (hasta 200)",
                  file=sys.stderr)
            return 2
    if not APOYO_RE.fullmatch(args.apoyo_url):
        # Tampoco se imprime el enlace.
        print("--apoyo-url no válido: vacío o un enlace https con solo letras, números, "
              "'.', '-', '_', '~' y '/' (sin '?', '#', usuario ni puerto)", file=sys.stderr)
        return 2

    os.makedirs(os.path.join(args.out, "js"), exist_ok=True)
    rutas = (
        (os.path.join(args.out, "version.json"), version_json(args.version)),
        (os.path.join(args.out, "js", "config.js"), config_js(args.wa_token, args.turnstile_sitekey, args.apoyo_url)),
    )
    for ruta, texto in rutas:
        with open(ruta, "w", encoding="utf-8", newline="\n") as f:
            f.write(texto)
        print(ruta)
    return 0


if __name__ == "__main__":
    sys.exit(main())

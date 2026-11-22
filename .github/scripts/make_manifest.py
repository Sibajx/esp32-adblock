#!/usr/bin/env python3
"""Escribe el manifest.json de ESP Web Tools para una versión de SiBlock.

Uso: make_manifest.py --version vX.Y.Z --out <carpeta>

Carga tres partes por separado (arranque en 0x1000, particiones en 0x8000, programa en 0x10000)
para no pisar la NVS (0x9000), donde viven el WiFi y la contraseña del panel.
Campos según ESP Web Tools 10.4.0: ahí `improv` ya no va en `builds` (se detecta solo).
Sale con código 2 si la versión no es vX.Y.Z.
"""
import argparse
import json
import os
import re
import sys

VERSION_RE = re.compile(r"v[0-9]+\.[0-9]+\.[0-9]+")

PARTS = [
    ("bootloader.bin", 0x1000),
    ("partitions.bin", 0x8000),
    ("firmware.bin", 0x10000),
]


def manifest(version):
    return {
        "name": "SiBlock",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32",
                "parts": [{"path": p, "offset": o} for p, o in PARTS],
            }
        ],
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)  # argumentos que faltan: argparse sale con 2

    if not VERSION_RE.fullmatch(args.version):
        print(f"versión no válida: {args.version!r} (se espera vX.Y.Z)", file=sys.stderr)
        return 2

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "manifest.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest(args.version), f, indent=2)
        f.write("\n")
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Genera lib/siblock_core/oui_table.h: qué marca tiene cada prefijo de MAC (OUI).

Sirve para que el panel sugiera un nombre ("Samsung", "Consola PlayStation") a los aparatos sin
nombre. Es solo una sugerencia: nunca se aplica sola.

Fuente: la lista pública MA-L del IEEE (Registration Authority):
    https://standards-oui.ieee.org/oui/oui.csv
Columnas: Registry, Assignment, Organization Name, Organization Address. Solo se usan las dos
primeras y el nombre; de cada prefijo se guarda la marca con una etiqueta nuestra en español.

Uso (solo Python 3 de serie, sin paquetes):
    wget -U "Wget/1.21" -O /tmp/oui/oui.csv https://standards-oui.ieee.org/oui/oui.csv
    python3 -I tools/gen_oui.py /tmp/oui/oui.csv [--fecha AAAA-MM-DD]

El CSV no va en el repo. La fecha es la de la descarga (por defecto, la de hoy).
"""
import argparse
import csv
import datetime
import hashlib
import io
import os
import re
import sys

SOURCE_URL = "https://standards-oui.ieee.org/oui/oui.csv"
OUT_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib", "siblock_core", "oui_table.h")

# Primero se descartan los fabricantes de chips y los que arman para otras marcas: su prefijo no
# dice qué aparato es. Es una red de seguridad: ninguna regla de abajo debería tocarlos.
EXCLUDE = re.compile(
    r"(?i)realtek|mediatek|qualcomm|broadcom|hon ?hai|foxconn|azurewave|murata|lite-?on|"
    r"cloud network technology|universal global scientific|wistron|quanta|pegatron|compal|"
    r"chicony|private$")

# Reglas en orden: gana la primera que coincide con el nombre de la organización.
RULES = [
    (r"^Sony (Interactive|Computer Entertainment)", "Consola PlayStation"),
    (r"^Apple,? Inc", "Apple"),
    (r"^Samsung Electronics|^SAMSUNG ELECTRO", "Samsung"),
    (r"Xiaomi", "Xiaomi"),
    (r"^Honor Device", "Honor"),
    (r"^Huawei (Technologies|Device)|^HUAWEI TECHNOLOGIES", "Huawei"),
    (r"^Motorola ?(Mobility|\(Wuhan\))", "Motorola"),
    (r"^Lenovo|^LCFC", "Lenovo"),
    (r"^(?i:LG Electronics)|^LG Innotek", "LG"),
    (r"^Sony (Corporation|Home|Imaging|Video)", "Sony"),
    (r"^Google|^Nest Labs", "Google/Chromecast"),
    (r"^Amazon Technologies|^Amazon\.com|^Blink by Amazon", "Amazon (Echo/Fire TV)"),
    (r"^Microsoft", "Microsoft/Xbox"),
    (r"^Nintendo", "Nintendo"),
    (r"^Roku", "Roku"),
    (r"^TCL ", "TCL"),
    (r"Hisense", "Hisense"),
    (r"\bOPPO\b", "OPPO"),
    (r"^vivo Mobile", "vivo"),
    (r"^Realme", "realme"),
    (r"^OnePlus", "OnePlus"),
    (r"^(?i:zte corporation)", "ZTE"),
    (r"^HMD Global", "Nokia"),
    (r"^HP Inc|^Hewlett Packard$", "HP"),
    (r"^Dell Inc", "Dell"),
    (r"^ASUSTek", "Asus"),
    (r"^Acer", "Acer"),
    (r"TP-LINK|TP-Link", "TP-Link"),
    (r"^Espressif|^Tuya Smart", "Aparato inteligente"),
    (r"^Signify|^Philips Lighting", "Philips Hue"),
    (r"^Ring LLC", "Ring"),
    (r"^Sonos", "Sonos"),
    (r"^Seiko Epson", "Impresora Epson"),
    (r"^(?i:canon inc)", "Impresora Canon"),
    (r"^(?i:brother industries)", "Impresora Brother"),
    (r"^Intel Corporat", "Computadora"),
]
COMPILED = [(re.compile(p), label) for p, label in RULES]
LABELS = list(dict.fromkeys(label for _, label in RULES))  # en el orden de las reglas, sin repetir


def label_for(org):
    """Etiqueta para una organización, o None."""
    org = org.strip()
    if EXCLUDE.search(org):
        return None
    for rx, label in COMPILED:
        if rx.search(org):
            return label
    return None


def build(rows):
    """rows: filas del CSV sin el encabezado. Devuelve ({etiqueta: [prefijo int]}, filas MA-L, dudosos)."""
    seen = {}
    conflicts = set()
    total = 0
    for row in rows:
        if len(row) < 3 or row[0].strip() != "MA-L":
            continue
        a = row[1].strip().upper()
        if not re.fullmatch(r"[0-9A-F]{6}", a):
            continue
        total += 1
        label = label_for(row[2])
        p = int(a, 16)
        if p in seen and seen[p] != label:
            conflicts.add(p)
        seen.setdefault(p, label)
    blocks = {label: [] for label in LABELS}
    for p, label in seen.items():
        if label is not None and p not in conflicts:
            blocks[label].append(p)
    for v in blocks.values():
        v.sort()
    return blocks, total, len(conflicts)


def c_string(s):
    assert all(0x20 <= ord(c) < 0x7F and c not in '"\\' for c in s), s
    return '"' + s + '"'


def render(blocks, total, conflicts, fecha, sha256):
    n = sum(len(v) for v in blocks.values())
    out = io.StringIO()
    w = out.write
    w("// GENERADO por tools/gen_oui.py: NO EDITAR A MANO. Para regenerar, ver docs/operacion.md.\n")
    w("//\n")
    w(f"// Fuente: {SOURCE_URL} (IEEE Registration Authority, lista MA-L)\n")
    w(f"// Bajado: {fecha}. Filas MA-L: {total}. sha256 del CSV: {sha256}\n")
    w(f"// Prefijos guardados: {n} ({3 * n} bytes). Prefijos con dos marcas distintas (no se usan): {conflicts}\n")
    w("// Solo prefijo -> marca, con etiquetas propias en español. Es una sugerencia: puede fallar\n")
    w("// (los módems del proveedor son Huawei, ZTE o TP-Link; algunos aparatos usan piezas de otros).\n")
    w("//\n")
    w("// Prefijos por marca:\n")
    for label in LABELS:
        w(f"//   {label}: {len(blocks[label])}\n")
    w("#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace siblock {\nnamespace oui_data {\n\n")
    w(f"constexpr size_t kBrandCount = {len(LABELS)};\n")
    w(f"constexpr size_t kPrefixCount = {n};\n\n")
    w("// Etiqueta de cada marca (código = posición + 1).\n")
    w("static const char* const kLabels[kBrandCount] = {\n")
    for label in LABELS:
        w(f"    {c_string(label)},\n")
    w("};\n\n")
    w("// La marca i tiene los prefijos [kStart[i], kStart[i+1]) de kPrefixes, ordenados.\n")
    w("static const uint16_t kStart[kBrandCount + 1] = {")
    acc = 0
    starts = []
    for label in LABELS:
        starts.append(acc)
        acc += len(blocks[label])
    starts.append(acc)
    assert acc < 65536
    w(", ".join(str(s) for s in starts))
    w("};\n\n")
    w("// 3 bytes por prefijo (los 3 primeros bytes de la MAC), por bloques de marca.\n")
    w("static const uint8_t kPrefixes[kPrefixCount * 3] = {\n")
    for label in LABELS:
        w(f"    // {label}\n")
        v = blocks[label]
        for i in range(0, len(v), 8):
            chunk = v[i:i + 8]
            w("    " + " ".join(f"0x{p >> 16:02X},0x{(p >> 8) & 0xFF:02X},0x{p & 0xFF:02X}," for p in chunk) + "\n")
    w("};\n\n}  // namespace oui_data\n}  // namespace siblock\n")
    return out.getvalue()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("csv", help="oui.csv bajado del IEEE")
    ap.add_argument("--fecha", default=datetime.date.today().isoformat(), help="día de la descarga (AAAA-MM-DD)")
    ap.add_argument("--salida", default=OUT_DEFAULT, help="archivo .h que se escribe")
    args = ap.parse_args(argv)
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", args.fecha):
        ap.error("la fecha va como AAAA-MM-DD")
    with open(args.csv, "rb") as f:
        raw = f.read()
    sha = hashlib.sha256(raw).hexdigest()
    reader = csv.reader(io.StringIO(raw.decode("utf-8-sig"), newline=""))
    head = next(reader, None)
    if not head or [h.strip() for h in head[:3]] != ["Registry", "Assignment", "Organization Name"]:
        sys.exit("el CSV no tiene las columnas Registry,Assignment,Organization Name")
    blocks, total, conflicts = build(reader)
    text = render(blocks, total, conflicts, args.fecha, sha)
    with open(args.salida, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    n = sum(len(v) for v in blocks.values())
    print(f"{args.salida}: {n} prefijos ({3 * n} bytes) de {total} filas MA-L, {len(LABELS)} marcas")
    return 0


if __name__ == "__main__":
    sys.exit(main())

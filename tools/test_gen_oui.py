#!/usr/bin/env python3
"""Pruebas de gen_oui.py con un CSV chico inventado. Correr: python3 tools/test_gen_oui.py"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "gen_oui.py")
sys.path.insert(0, HERE)
import gen_oui  # noqa: E402

CSV = """Registry,Assignment,Organization Name,Organization Address
MA-L,FCF136,Samsung Electronics Co.,Ltd,"Suwon KR"
MA-L,0000F0,Samsung Electronics Co.,Ltd,"Suwon KR"
MA-L,00041F,Sony Interactive Entertainment Inc.,"Tokyo JP"
MA-L,000393,"Apple, Inc.",Cupertino US
MA-L,00E04C,REALTEK SEMICONDUCTOR CORP.,Hsinchu TW
MA-L,0014A4,Hon Hai Precision Ind. Co.,Ltd.,Taipei TW
MA-L,000B86,Hewlett Packard Enterprise ,Houston US
MA-L,0001E6,Hewlett Packard,Palo Alto US
MA-L,001787,"Brother, Brother & Sons ApS",DK
MA-L,001BA9,"Brother industries, LTD.",Nagoya JP
MA-L,123456,zte corporation,Shenzhen CN
MA-L,ABCDEF,Private,
MA-M,FCF1361,Samsung Electronics Co.,Ltd,"Suwon KR"
MA-L,zzzzzz,Samsung Electronics Co.,Ltd,"Suwon KR"
"""


class Reglas(unittest.TestCase):
    def test_etiquetas(self):
        cases = {
            "Sony Interactive Entertainment Inc.": "Consola PlayStation",
            "Sony Corporation": "Sony",
            "Apple, Inc.": "Apple",
            "SAMSUNG ELECTRO-MECHANICS(THAILAND)": "Samsung",
            "Hewlett Packard": "HP",
            "Hewlett Packard Enterprise": None,
            "Hewlett Packard Enterprise ": None,
            "HP Inc.": "HP",
            "Motorola(Wuhan) Mobility Technologies Communication Co.,Ltd": "Motorola",
            "Motorola Solutions Inc.": None,
            "LG ELECTRONICS INC": "LG",
            "Nest Labs Inc.": "Google/Chromecast",
            "Nestle Purina PetCare": None,
            "Espressif Inc.": "Aparato inteligente",
            "CANON INC.": "Impresora Canon",
            "Intel Corporate": "Computadora",
            "Intelbras": None,
            "REALTEK SEMICONDUCTOR CORP.": None,
            "Cloud Network Technology Singapore Pte. Ltd.": None,
            "Private": None,
        }
        for org, want in cases.items():
            self.assertEqual(gen_oui.label_for(org), want, org)

    def test_etiquetas_ascii(self):
        for label in gen_oui.LABELS:
            gen_oui.c_string(label)  # falla si trae comillas, barra o algo fuera de ASCII
        self.assertLess(len(gen_oui.LABELS), 255)


class Archivo(unittest.TestCase):
    def test_genera(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "oui.csv")
            out = os.path.join(d, "oui_table.h")
            with open(src, "w", encoding="utf-8") as f:
                f.write(CSV)
            subprocess.run([sys.executable, "-I", SCRIPT, src, "--fecha", "2026-11-22", "--salida", out],
                           check=True, capture_output=True)
            with open(out, encoding="utf-8") as f:
                text = f.read()
        self.assertIn("NO EDITAR A MANO", text)
        self.assertIn("https://standards-oui.ieee.org/oui/oui.csv", text)
        self.assertIn("Bajado: 2026-11-22. Filas MA-L: 12.", text)
        self.assertRegex(text, r"sha256 del CSV: [0-9a-f]{64}")
        self.assertIn("constexpr size_t kPrefixCount = 7;", text)
        self.assertIn("//   Samsung: 2\n", text)
        self.assertIn("//   Impresora Brother: 1\n", text)
        # Samsung ordenado dentro de su bloque; Realtek, Hon Hai y HPE no están.
        sam = text.index("    // Samsung\n")
        self.assertTrue(text.startswith("    0x00,0x00,0xF0, 0xFC,0xF1,0x36,", sam + len("    // Samsung\n")))
        for no in ("0x00,0xE0,0x4C", "0x00,0x14,0xA4", "0x00,0x0B,0x86", "0x00,0x17,0x87", "0xAB,0xCD,0xEF"):
            self.assertNotIn(no, text)
        starts = re.search(r"kStart\[kBrandCount \+ 1\] = \{([^}]*)\}", text).group(1)
        nums = [int(x) for x in starts.split(",")]
        self.assertEqual(nums[0], 0)
        self.assertEqual(nums[-1], 7)
        self.assertEqual(nums, sorted(nums))

    def test_rechaza_otro_csv(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "x.csv")
            with open(src, "w") as f:
                f.write("a,b,c\n1,2,3\n")
            r = subprocess.run([sys.executable, "-I", SCRIPT, src, "--salida", os.path.join(d, "o.h")],
                               capture_output=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertFalse(os.path.exists(os.path.join(d, "o.h")))


if __name__ == "__main__":
    unittest.main()

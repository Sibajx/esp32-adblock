#!/usr/bin/env python3
"""Pruebas de make_manifest.py. Correr: python3 .github/scripts/test_make_manifest.py"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_manifest.py")

# Campos según ESP Web Tools 10.4.0 (la versión fijada en site/index.html). En esa versión
# `improv` ya no existe en `builds`: la página detecta Improv sola, por eso no va.
EXPECTED = {
    "name": "SiBlock",
    "version": "v1.2.3",
    "new_install_prompt_erase": True,
    "builds": [
        {
            "chipFamily": "ESP32",
            "parts": [
                {"path": "bootloader.bin", "offset": 4096},
                {"path": "partitions.bin", "offset": 32768},
                {"path": "firmware.bin", "offset": 65536},
            ],
        }
    ],
}


def run(*args):
    return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True)


class MakeManifestTest(unittest.TestCase):
    def test_valid_version_writes_exact_manifest(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.2.3", "--out", d)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
                self.assertEqual(json.load(f), EXPECTED)

    def test_creates_out_dir(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "public", "firmware")
            r = run("--version", "v10.0.42", "--out", out)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(os.path.join(out, "manifest.json"), encoding="utf-8") as f:
                self.assertEqual(json.load(f)["version"], "v10.0.42")

    def test_bad_versions_exit_2_and_write_nothing(self):
        for bad in ["1.2.3", "v1.2", "v1.2.3; rm", "v1.2.3\n", "v1.2.3-rc1", "", "v01.2.x"]:
            with self.subTest(version=bad), tempfile.TemporaryDirectory() as d:
                r = run("--version", bad, "--out", d)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertFalse(os.path.exists(os.path.join(d, "manifest.json")))

    def test_missing_args_exit_2(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run("--out", d).returncode, 2)
        self.assertEqual(run("--version", "v1.2.3").returncode, 2)


if __name__ == "__main__":
    unittest.main()

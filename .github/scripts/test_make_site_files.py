#!/usr/bin/env python3
"""Pruebas de make_site_files.py. Correr: python3 .github/scripts/test_make_site_files.py"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_site_files.py")

HEADER = (
    "// Configuración pública de la página. La escribió CI al publicar (make_site_files.py):\n"
    "// token de Cloudflare Web Analytics, clave pública de Turnstile y enlace de apoyo.\n"
    "// Vacío = no se carga nada (sin enlace de apoyo no se muestra el botón).\n"
)


def config_js(token, clave, apoyo=""):
    # Línea del contrato entre carriles (site/js/config.js), byte a byte salvo los valores.
    return (
        HEADER
        + "export const CONFIG = Object.freeze({ api: '/api/v1', "
        + f"analiticaToken: '{token}', turnstileClave: '{clave}', apoyoUrl: '{apoyo}' }});\n"
    )


def run(*args):
    return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True)


def read(d, rel):
    with open(os.path.join(d, rel), encoding="utf-8") as f:
        return f.read()


class MakeSiteFilesTest(unittest.TestCase):
    def test_valid_version_writes_both_files_exact(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.2.3", "--out", d,
                    "--wa-token", "abc123_-XYZ", "--turnstile-sitekey", "0x4AAAAAAA-b_c")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(
                read(d, "version.json"),
                '{"version":"v1.2.3","url":"https://siblock.s-dev.win/#paso-instalar"}\n',
            )
            self.assertEqual(json.loads(read(d, "version.json")),
                             {"version": "v1.2.3",
                              "url": "https://siblock.s-dev.win/#paso-instalar"})
            self.assertEqual(read(d, "js/config.js"), config_js("abc123_-XYZ", "0x4AAAAAAA-b_c"))

    def test_without_token_or_sitekey_writes_empty_values(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v0.10.0", "--out", d)
            self.assertEqual(r.returncode, 0, r.stderr)
            texto = read(d, "js/config.js")
            self.assertEqual(texto, config_js("", ""))
            self.assertIn("analiticaToken: ''", texto)
            self.assertIn("turnstileClave: ''", texto)

    def test_empty_values_from_unset_repo_vars(self):
        # En GitHub, ${{ vars.X }} sin definir llega como texto vacío.
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.0.0", "--out", d, "--wa-token", "", "--turnstile-sitekey", "")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(read(d, "js/config.js"), config_js("", ""))

    def test_overwrites_existing_site_files(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "js"))
            with open(os.path.join(d, "js", "config.js"), "w", encoding="utf-8") as f:
                f.write("viejo")
            with open(os.path.join(d, "version.json"), "w", encoding="utf-8") as f:
                f.write('{"version":"v0.0.0"}')
            r = run("--version", "v2.0.1", "--out", d, "--wa-token", "t")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(read(d, "js/config.js"), config_js("t", ""))
            self.assertEqual(json.loads(read(d, "version.json"))["version"], "v2.0.1")

    def test_bad_values_exit_2_and_write_nothing(self):
        malos = ["a'b", "a<b", "</script>", "a\"b", "a b", "a\nb", "a\\b", "ñ", "a;b", "x" * 201]
        for opcion in ("--wa-token", "--turnstile-sitekey"):
            for malo in malos:
                with self.subTest(opcion=opcion, valor=malo), tempfile.TemporaryDirectory() as d:
                    r = run("--version", "v1.2.3", "--out", d, opcion, malo)
                    self.assertEqual(r.returncode, 2, r.stderr)
                    self.assertFalse(os.path.exists(os.path.join(d, "version.json")))
                    self.assertFalse(os.path.exists(os.path.join(d, "js", "config.js")))

    def test_apoyo_url_is_written(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.2.3", "--out", d, "--apoyo-url", "https://ko-fi.com/siblock")
            self.assertEqual(r.returncode, 0, r.stderr)
            texto = read(d, "js/config.js")
            self.assertEqual(texto, config_js("", "", "https://ko-fi.com/siblock"))
            self.assertIn("apoyoUrl: 'https://ko-fi.com/siblock'", texto)

    def test_without_apoyo_url_writes_empty(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.2.3", "--out", d)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("apoyoUrl: '' });", read(d, "js/config.js"))

    def test_empty_apoyo_url_from_unset_repo_var(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.2.3", "--out", d, "--apoyo-url", "")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(read(d, "js/config.js"), config_js("", ""))

    def test_apoyo_url_largest_accepted(self):
        url = "https://" + "a" * 100 + "/" + "b" * 150
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v1.2.3", "--out", d, "--apoyo-url", url)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(read(d, "js/config.js"), config_js("", "", url))

    def test_bad_apoyo_url_exit_2_and_write_nothing(self):
        malos = ["http://x.com", "https://x.com/a'b", "https://x.com/<s>", "javascript:x",
                 "https://x.com/a b", "https://" + "a" * 101, "https://", "https://x.com/a\nb",
                 "https://x.com/a\\b", "https://x.com/a\"b", "https://x.com/?q=1",
                 "https://x.com/#a", "https://user@x.com", "https://x.com:8080",
                 "https://x.com/" + "b" * 151, " https://x.com", "https://x.com/ñ"]
        for malo in malos:
            with self.subTest(valor=malo), tempfile.TemporaryDirectory() as d:
                r = run("--version", "v1.2.3", "--out", d, "--apoyo-url", malo)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertNotIn(malo, r.stderr)
                self.assertIn("--apoyo-url", r.stderr)
                self.assertFalse(os.path.exists(os.path.join(d, "version.json")))
                self.assertFalse(os.path.exists(os.path.join(d, "js", "config.js")))

    def test_bad_versions_exit_2_and_write_nothing(self):
        for bad in ["v1.2", "1.2.3", "v1.2.3; rm", "v1.2.3\n", "v1.2.3-rc1", "", "v01.2.3",
                    "v1.02.3", "v1.2.03", "v10000.0.0", "dev", "v1.2.3 "]:
            with self.subTest(version=bad), tempfile.TemporaryDirectory() as d:
                r = run("--version", bad, "--out", d)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertFalse(os.path.exists(os.path.join(d, "version.json")))
                self.assertFalse(os.path.exists(os.path.join(d, "js", "config.js")))

    def test_accepts_largest_parts(self):
        with tempfile.TemporaryDirectory() as d:
            r = run("--version", "v9999.0.9999", "--out", d)
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_missing_args_exit_2(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run("--out", d).returncode, 2)
        self.assertEqual(run("--version", "v1.2.3").returncode, 2)


if __name__ == "__main__":
    unittest.main()

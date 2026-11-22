import { cloudflareTest, readD1Migrations } from "@cloudflare/vitest-pool-workers";
import { defineConfig } from "vitest/config";

export default defineConfig(async () => {
  const migraciones = await readD1Migrations(new URL("./migrations/", import.meta.url).pathname);
  return {
    plugins: [
      cloudflareTest({
        wrangler: { configPath: "./wrangler.toml" },
        miniflare: {
          // El paquete de pruebas fijado (0.23.0) trae un runtime que llega hasta 2026-08-22;
          // en producción vale la fecha de wrangler.toml.
          compatibilityDate: "2026-08-22",
          bindings: {
            ID_PEPPER: "pimienta-de-prueba",
            TURNSTILE_SECRET: "secreto-de-prueba",
            TEST_MIGRATIONS: migraciones,
          },
        },
      }),
    ],
    test: { setupFiles: ["./test/setup.ts"] },
  };
});

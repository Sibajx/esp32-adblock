# Documentación técnica de SiBlock

Para instalarlo y usarlo, empieza por el [README del proyecto](../README.md) o la página
https://siblock.s-dev.win. Esta carpeta explica cómo está hecho por dentro, para quien vaya a
cambiar el código.

| Documento | Qué tiene |
|---|---|
| [arquitectura.md](arquitectura.md) | Piezas, tareas de FreeRTOS, memoria y flash, decisiones de diseño; fase 1: arranque, NVS, modos, Improv; fase 2: historia, diagnóstico; fase 3: avisos a la nube; fase 5: aparatos, horarios, respaldo, LED y tarea del botón |
| [dns.md](dns.md) | Camino de una consulta: parser, veredicto, caché, reenvío, TCP, anti-rebinding |
| [lista.md](lista.md) | Formato de `/list.bin`, cómo se baja y arma la lista, búsqueda y cambio atómico |
| [api.md](api.md) | API HTTP del panel (fases 1, 2, 3 y 5: contraseña, portal `/api/wifi*`, diagnóstico, gráficas, nube, aparatos, horarios, listas y respaldo) y los comandos Improv por USB |
| [seguridad.md](seguridad.md) | Modelo de amenazas y cada defensa de las fases 1, 2, 3 y 5 (ventana de creación, AP con WPA2, USB, PBKDF2, diagnóstico público, Worker, aparatos y respaldo) |
| [pruebas.md](pruebas.md) | `test_core` y `test_siblock`, simulador (`esp32-sim` y `esp32-sim-fresh`, reloj, botón, Improv, portal), pruebas de la página y del Worker, y lista para la placa |
| [operacion.md](operacion.md) | Instalar y actualizar, botón BOOT y LED, cambiar el WiFi, borrar la configuración, salida por serie y qué hacer si falla |
| [ideas-pendientes.md](ideas-pendientes.md) | Ideas y decisiones de SiBlock (§0–§5) |
| [superpowers/specs/2026-11-21-siblock-design.md](superpowers/specs/2026-11-21-siblock-design.md) | Diseño de SiBlock por fases |
| [superpowers/plans/2026-11-21-siblock-fase-1.md](superpowers/plans/2026-11-21-siblock-fase-1.md) | Plan de la fase 1 ("Instalar sin saber") |
| [superpowers/plans/2026-11-21-siblock-fase-2.md](superpowers/plans/2026-11-21-siblock-fase-2.md) | Plan de la fase 2 (acompañamiento: diagnóstico, obstáculos, gráficas, guías del router) |
| [superpowers/plans/2026-11-21-siblock-fase-3.md](superpowers/plans/2026-11-21-siblock-fase-3.md) | Plan de la fase 3 (Worker en Cloudflare, reportes, estadísticas, Contáctanos) |
| [superpowers/plans/2026-11-21-siblock-fase-5.md](superpowers/plans/2026-11-21-siblock-fase-5.md) | Plan de la fase 5 (aparatos, horarios, listas, LED, respaldo, página en inglés) |
| [../cloud/DEPLOY.md](../cloud/DEPLOY.md) | Pasos a mano para publicar el Worker y la página en Cloudflare |

Lo que describen estas páginas de las fases 1, 2, 3 y 5 está en `src/`, `lib/`, `site/` y `cloud/`, y se revisó contra el código (la fase 4 todavía no existe).

Convenciones del código:

- Comentarios y mensajes en español.
- `lib/adblock_core` y `lib/siblock_core` no dependen de Arduino: todo lo que se pueda probar en
  la PC va ahí.
- Los tiempos se comparan con restas sin signo (`now - inicio >= duración`), nunca contra un
  vencimiento: `millis()` da la vuelta a los 49 días.
- Texto que ve la persona: español, palabras simples, marca **SiBlock**.
- Nada pide memoria proporcional a algo que manda la red. Cada búfer tiene un tope fijo y se
  documenta junto a la constante.
- La tarea DNS nunca espera más de 50 ms por un candado. Si no lo consigue, deja pasar la
  consulta: es mejor que trabar el DNS de la casa.

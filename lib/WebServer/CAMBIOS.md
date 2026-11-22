# WebServer (copia parcheada)

Copia de la librería `WebServer` de arduino-esp32 2.0.17 (LGPL 2.1, © Ivan Grokhotkov y
colaboradores). PlatformIO usa esta copia en vez de la del framework porque está en `lib/`.

Cambios, todos en `src/Parsing.cpp` (`_parseRequest`), para que un cliente de la red no pueda
agotar la RAM ni trabar el loop:

- Cada línea de la petición (la primera y los headers) tiene un máximo de 1024 bytes.
- La petición entera tiene 8 s de plazo, incluido el cuerpo en modo raw.
- Los cuerpos de más de 4096 bytes (o con Content-Length negativo) se rechazan sin leerlos.
- Se rechaza multipart (el panel no lo usa; `_parseForm` arma líneas sin tope).
- El modo raw no lee más allá de Content-Length.
- `_hostHeader` se borra en cada petición (antes una petición sin Host heredaba el anterior).

Una petición rechazada cierra la conexión sin respuesta, como ya hacía la librería con las
peticiones mal formadas.

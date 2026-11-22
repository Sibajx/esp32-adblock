// OPCIONAL. Solo para quien compila su propio firmware: el WiFi se da desde la página de
// instalación (por USB) o desde la red SiBlock-XXXX; la contraseña del panel se crea en el panel.
// Todo se guarda en la placa.
//
// Si copias este archivo como secrets.h (está en .gitignore), estos valores se usan solo la
// primera vez, cuando la placa todavía no tiene nada guardado. El firmware publicado se compila
// sin secrets.h.
#pragma once

// WiFi de la casa (2.4 GHz). Contraseña vacía ("") si la red es abierta; si no, de 8 a 63.
#define WIFI_SSID "TuRed"
#define WIFI_PASSWORD "TuContraseña"

// Contraseña del panel web (usuario: admin), de 12 a 64 caracteres. Se guarda solo su hash.
// No reutilices una importante (el panel es HTTP). "CAMBIA_ESTA_CLAVE" se ignora.
#define WEB_PASSWORD "CAMBIA_ESTA_CLAVE"

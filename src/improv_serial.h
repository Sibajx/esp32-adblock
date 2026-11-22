// Improv Serial por USB: la página de instalación (ESP Web Tools) le da el WiFi a la placa,
// pregunta el estado y lee la contraseña de respaldo del AP (comando propio 0xA0).
// Quien tiene el cable USB ya podría borrar todo con BOOT, así que esto no abre nada nuevo.
#pragma once
#include "modes.h"

namespace improv_serial {
void begin(siblock::PortalPolicy* policy);
void loop();          // lee lo que haya en Serial y contesta; nunca bloquea
bool provisioning();  // probando un WiFi nuevo (main.cpp no reintenta el guardado mientras tanto)
}  // namespace improv_serial

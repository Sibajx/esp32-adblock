// Botón BOOT (GPIO0) y LED azul (GPIO2):
//   < 2 s   -> abre 30 min la ventana para crear la contraseña del panel
//   3 a 8 s -> modo de configuración por WiFi
//   >= 10 s -> borra la configuración (WiFi y contraseñas) y reinicia
// Mientras se mantiene, el LED parpadea rápido hasta los 3 s, queda fijo de 3 a 8 s, parpadea de
// 8 a 10 s y se apaga al borrar. Si nadie lo aprieta, el LED dice el estado (siblock::ledOn):
// fijo = todo bien; parpadeo lento = bajando la lista; parpadeo rápido = problema.
#pragma once
#include <Arduino.h>

#include "modes.h"

namespace button {
void begin();  // después del arranque: GPIO0 solo como entrada (nunca como salida)
// Lee el botón y pone el LED. El pin se escribe solo cuando cambia.
siblock::ButtonAction poll(uint32_t nowMs, siblock::LedState st);
}  // namespace button

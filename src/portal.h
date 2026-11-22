// Modo de configuración por WiFi: SiBlock abre su propia red "SiBlock-XXXX" (WPA2, con la
// contraseña de respaldo) y un portal para elegir el WiFi de la casa. Se usa cuando no hay WiFi
// guardado, cuando el guardado dejó de funcionar o al mantener BOOT de 3 a 8 s.
#pragma once
#include <Arduino.h>

#include "dns_server.h"

namespace portal {
void begin(DnsServer* dns);
void start();
void stop();
bool active();
// IP de la placa en su propia red: 192.168.4.1, u otra si la red de la casa también usa esa.
String apIpText();
// La petición viene de un aparato conectado a la red de SiBlock: llegó a la IP del AP (`local`)
// desde su red. Si la red de la casa choca con la del AP no se confía en nadie (no hay forma
// segura de distinguirlos).
bool isApClient(IPAddress remote, IPAddress local);
bool homeClashesWithAp();  // la red de la casa comparte direcciones con la de SiBlock
// El portal guardó un WiFi nuevo: a los 2 s (para que la respuesta llegue) se cierra y conecta.
void noteWifiSaved(uint32_t nowMs);
bool reconnectDue(uint32_t nowMs);  // true una sola vez, cuando toca
}  // namespace portal

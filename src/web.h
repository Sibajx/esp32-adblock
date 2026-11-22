// Panel de SiBlock: http://<ip-de-la-placa>/
#pragma once
#include "blocklist.h"
#include "dns_server.h"
#include "modes.h"

namespace web {
// window: ventana para crear la contraseña del panel (la abre main.cpp al arrancar y con BOOT).
void begin(BlockList* list, DnsServer* dns, volatile bool* updateRequested, siblock::SetupWindow* window);
void loop();
}  // namespace web

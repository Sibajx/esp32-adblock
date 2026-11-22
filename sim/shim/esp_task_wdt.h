// Simulador: el vigilante no hace nada en la PC.
#pragma once
#include <cstdint>
inline int esp_task_wdt_init(uint32_t, bool) { return 0; }
inline int esp_task_wdt_add(void*) { return 0; }
inline int esp_task_wdt_reset() { return 0; }

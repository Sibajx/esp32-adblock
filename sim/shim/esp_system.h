// Simulador: por qué arrancó la placa. SIM_RESET_REASON=power (por defecto), sw, panic, wdt o
// brownout.
#pragma once
#include <cstdlib>
#include <cstring>

typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
} esp_reset_reason_t;

inline esp_reset_reason_t esp_reset_reason() {
  const char* r = getenv("SIM_RESET_REASON");
  if (!r || !*r || !strcmp(r, "power")) return ESP_RST_POWERON;
  if (!strcmp(r, "sw")) return ESP_RST_SW;
  if (!strcmp(r, "panic")) return ESP_RST_PANIC;
  if (!strcmp(r, "wdt")) return ESP_RST_TASK_WDT;
  if (!strcmp(r, "brownout")) return ESP_RST_BROWNOUT;
  return ESP_RST_UNKNOWN;
}

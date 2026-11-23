// Avisos por Telegram al dueño, con su propio bot (apagados de fábrica). Aquí solo está el envío
// HTTPS y el estado en RAM; cuándo se manda cada aviso lo decide main.cpp (watchTelegram), siempre
// desde loop(), nunca desde la tarea DNS ni dentro de una petición del panel.
#pragma once
#include <Arduino.h>

#include <string>

#include "notify.h"

namespace telegram {
// Avisos que esperan su turno (bits). El de prueba lo pide el panel; los otros, main.cpp.
constexpr uint8_t kTest = 1, kBoot = 2, kListFail = 4, kListOk = 8, kWeekly = 16;

struct Status {
  uint8_t pending = 0;
  siblock::TelegramResult last = siblock::TelegramResult::kNever;
  int lastCode = 0;     // código HTTP de Telegram (0 = no se llegó)
  uint32_t lastAt = 0;  // segundos desde 1970 (0 = sin hora)
};
Status& status();  // solo RAM: se pierde al reiniciar

// POST a SIBLOCK_TELEGRAM_BASE/bot<token>/sendMessage con chat_id y text. Bloquea hasta ~8 s:
// solo desde loop(). Valida el certificado (certs.h), no sigue redirecciones y no lee lo que
// contesta Telegram. Nunca imprime el token. Devuelve el código HTTP, o si no se llegó:
// siblock::kTgNoNet (sin WiFi), siblock::kTgLowMem (poca RAM para TLS), siblock::kTgNoConfig (token o
// chat guardados que faltan o no valen) u otro <= 0 de HTTPClient (sin conexión, tiempo agotado).
int send(const std::string& text);

// Por qué arrancó la placa (esp_reset_reason) en palabras simples; updated: cambió la versión.
siblock::BootCause bootCause(bool updated);
}  // namespace telegram

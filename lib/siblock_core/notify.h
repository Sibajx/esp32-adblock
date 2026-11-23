// SiBlock: avisos por Telegram al dueño, sin Arduino. Validar el token y el chat, armar los
// mensajes (cortos, en español, solo números y estados: nunca IPs, MACs, nombres de aparatos ni
// sitios visitados), el cuerpo del POST, cuándo toca el resumen semanal, cuándo avisar que la
// lista no se pudo actualizar y los topes de envío. Quién decide si sale algo es main.cpp según la
// opción guardada (apagada de fábrica); el envío está en src/telegram.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace siblock {

// Token de un bot: ^[0-9]{5,12}:[A-Za-z0-9_-]{30,64}$ (sin std::regex: ocupa flash).
bool validTelegramToken(const std::string& s);
// Número de chat: ^-?[0-9]{1,20}$ (los grupos son negativos).
bool validTelegramChatId(const std::string& s);

// 152340 -> "152 340", con espacio que no se corta (U+00A0, 2 bytes en UTF-8).
std::string groupThousands(uint32_t n);

// Por qué arrancó la placa, en palabras simples (src/telegram.cpp lo saca de esp_reset_reason()).
enum class BootCause : uint8_t { kPower, kNormal, kCrash, kBrownout, kUpdate };
std::string bootMessage(BootCause c, uint32_t listCount);
// La lista no se pudo actualizar; hoursFailing: horas desde la primera falla de la racha (con menos
// de 24 no se dice desde cuándo: nunca «desde hace 0 días»).
std::string listFailMessage(uint32_t hoursFailing, uint32_t listCount);
std::string listOkMessage(uint32_t listCount);
std::string weeklyMessage(uint32_t blocked);
std::string testMessage();

// Para application/x-www-form-urlencoded: deja [A-Za-z0-9-_.~] y lo demás va como %XX.
std::string formEncode(const std::string& s);
// "chat_id=<chat>&text=<texto codificado>". "" si el chat no es válido o el texto está vacío.
std::string telegramForm(const std::string& chatId, const std::string& text);

// Resumen semanal: el domingo de 20:00 a 23:59 en la hora de la casa. Si toca, devuelve el día
// local (días desde 1970) de ese domingo, para no mandarlo dos veces; si no toca o la hora no es
// creíble, -1.
int32_t weeklySummaryDay(int64_t epochSeconds, int16_t tzMinutes);

// Resumen semanal una sola vez por domingo. El día se guarda en la NVS (sobrevive a un reinicio);
// si la NVS no deja guardarlo, la RAM se acuerda igual y no se repite cada minuto.
class WeeklyOnce {
 public:
  // day: lo que dio weeklySummaryDay (-1 = no toca); stored: el día guardado en la NVS (0 = nunca).
  // true una sola vez por día; quien llama guarda `day` en la NVS.
  bool take(int32_t day, uint32_t stored);

 private:
  uint32_t last_ = 0;
};

// Códigos de src/telegram.cpp cuando no se llegó a Telegram (los demás son el código HTTP, o lo
// que devuelve HTTPClient, <= 0, si no hubo conexión).
constexpr int kTgNoNet = -1;     // sin WiFi o sin conexión
constexpr int kTgLowMem = -2;    // poca RAM seguida para TLS
constexpr int kTgNoConfig = -3;  // el token o el chat guardados faltan o no valen

// Cómo salió el último envío. El panel solo ve el nombre y el código HTTP, nunca lo que contestó
// Telegram.
enum class TelegramResult : uint8_t { kNever, kOk, kToken, kChat, kBlocked, kLimit, kNoNet, kRejected, kLowMem, kNoConfig };
// kTgLowMem: poca RAM; kTgNoConfig: sin datos; otro <= 0: sin internet; 2xx: ok; 401 o 404: token;
// 400: chat; 403: el bot no puede escribir; 429: límite; lo demás: rechazado.
TelegramResult telegramResultFor(int httpCode);
// "never" | "ok" | "token" | "chat" | "bloqueado" | "limite" | "sin_internet" | "rechazado" |
// "poca_ram" | "sin_datos"
const char* telegramResultName(TelegramResult r);
// ¿Falla pasajera (sin internet o poca RAM)? Esas se reintentan (TelegramRetry); las demás no.
bool telegramTransient(TelegramResult r);

// Reintentos de un aviso que no salió por una falla pasajera: uno por minuto y como mucho kMax por
// aviso (cada intento cuenta además en el tope de 20 por hora). Así un aviso no se pierde por un
// rato sin RAM o sin internet, pero tampoco se queda intentando para siempre.
class TelegramRetry {
 public:
  static constexpr uint8_t kMax = 5;
  static constexpr uint32_t kGapMs = 60000;
  // ¿Hay que esperar antes del próximo envío? (un minuto desde la última falla pasajera)
  bool waiting(uint32_t nowMs) const;
  // El aviso `bit` falló por algo pasajero: true si vuelve a la cola, false si ya se reintentó kMax
  // veces (se deja).
  bool failed(uint8_t bit, uint32_t nowMs);
  // Salió, o falló por algo que no se arregla reintentando: la cuenta vuelve a cero.
  void done();

 private:
  uint32_t at_ = 0;
  uint8_t bit_ = 0, tries_ = 0;
  bool wait_ = false;
};

// Tope de envíos: como mucho kMax en kWindowMs (una ventana que se mueve). Restas sin signo:
// aguanta que millis() dé la vuelta.
class SendLimiter {
 public:
  static constexpr uint8_t kMax = 20;
  static constexpr uint32_t kWindowMs = 3600000;
  bool allow(uint32_t nowMs);  // true y lo cuenta, o false sin contarlo

 private:
  uint32_t at_[kMax] = {};
  uint8_t used_ = 0, next_ = 0;
};

// Un intento cada gapMs (el botón «Mandar mensaje de prueba»: uno cada 30 s).
class Cooldown {
 public:
  bool take(uint32_t nowMs, uint32_t gapMs);  // true y empieza a contar, o false

 private:
  uint32_t last_ = 0;
  bool used_ = false;
};

// Cuándo avisar que la lista no se pudo actualizar: a la kMinFails-ésima falla seguida (la primera
// y dos reintentos, unos 6 min) y luego como mucho una vez cada 24 h mientras siga fallando. Si se
// avisó y vuelve a funcionar, «ya se pudo». Solo cuentan las fallas de los intentos automáticos:
// pulsar «Actualizar ahora» varias veces no adelanta el aviso (un intento a mano que sale bien sí
// cuenta: la lista ya está al día).
class ListFailWatch {
 public:
  static constexpr uint8_t kMinFails = 3;
  static constexpr uint32_t kRepeatMs = 86400000;
  enum Action : uint8_t { kNone, kFail, kRecovered };
  // enabled: ¿el aviso está encendido? Apagado no cuenta como avisado (y no habrá «ya se pudo»).
  // scheduled: false si lo pidió la persona («Actualizar ahora»).
  Action onResult(bool ok, uint32_t nowMs, bool enabled, bool scheduled = true);
  uint32_t failingHours(uint32_t nowMs) const;  // 0 si no está fallando

 private:
  uint32_t since_ = 0, lastNotice_ = 0;
  uint8_t fails_ = 0;
  bool noticed_ = false;
};

}  // namespace siblock

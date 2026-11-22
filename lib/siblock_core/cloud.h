// SiBlock: lo que la placa necesita para hablar con siblock.s-dev.win, sin Arduino.
// Número de instalación, versiones, version.json, cuerpos JSON de los avisos y reportes, y el
// calendario de envíos. Todo lo que sale a internet está apagado de fábrica: este código solo
// arma y revisa; quién decide si sale algo es main.cpp / web.cpp según las opciones guardadas.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "usage.h"

namespace siblock {

bool validInstallId(const std::string& s);        // 32 de [0-9a-f]
std::string makeInstallId(const uint8_t rnd[16]);  // hex en minúsculas
// "vX.Y.Z", cada parte 0..9999, sin ceros a la izquierda, sin espacios ni nada más.
bool parseSemver(const std::string& s, uint32_t out[3]);
// true solo si las dos son vX.Y.Z y latest es mayor ("dev" -> false).
bool isNewer(const std::string& latest, const std::string& current);
// Busca la clave "version" y su texto; false si len > 1024 o el valor no es vX.Y.Z.
bool parseVersionJson(const char* body, size_t len, std::string& version);
// 3..253 B, 2 o más etiquetas de 1..63 de [a-z0-9-], sin '-' al inicio ni al final de una etiqueta.
bool validReportDomain(const std::string& d);

// Opciones fijas del contrato (iguales en la página y en el Worker).
extern const char* const kFabricantes[];
extern const size_t kFabricantesCount;
extern const char* const kMenus[];
extern const size_t kMenusCount;
extern const char* const kMotivos[];
extern const size_t kMotivosCount;
bool inList(const char* const* list, size_t n, const std::string& v);

constexpr uint32_t kMaxBlockedPerDay = 100000000;
// Cuerpos JSON para el Worker. Devuelven "" si algún campo no es válido; como cada campo
// válido solo tiene [a-z0-9.-], no hace falta escapar nada.
std::string pingJson(const std::string& id, const std::string& version, uint32_t blocked);
std::string modemReportJson(const std::string& id, const std::string& fabricante, bool dnsOk,
                            const std::string& menu);
std::string blockedReportJson(const std::string& id, const std::string& domain, const std::string& motivo);

// Bloqueadas de las últimas `hours` horas (como mucho 168) que terminan en nowHour.
uint32_t blockedLastHours(const UsageHistory& h, uint32_t nowHour, size_t hours);
// Lo mismo sobre una foto ya sacada con snapshot (la más vieja primero).
uint32_t sumBlocked(const UsageHistory::Slot slots[UsageHistory::kHours], size_t hours);

// Cuándo toca el próximo envío. Usa restas sin signo: aguanta que millis() dé la vuelta.
class CloudSchedule {
 public:
  static constexpr uint32_t kFirstMs = 60000, kEveryMs = 86400000, kRetryMinMs = 3600000;
  void start(uint32_t nowMs);  // el primero, en kFirstMs
  bool due(uint32_t nowMs) const;  // sin start(), nunca
  // ok: el siguiente en kEveryMs + jitter % 1 h; fallo: min(kRetryMinMs << fallos, kEveryMs).
  void done(bool ok, uint32_t nowMs, uint32_t jitterMs);

 private:
  uint32_t next_ = 0, base_ = 0;  // espera (0 = sin empezar) y desde cuándo se cuenta
  uint8_t fails_ = 0;
};

}  // namespace siblock

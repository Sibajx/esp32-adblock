// SiBlock: aparatos que se saltan el bloqueo y diagnóstico del botón "Algo no funciona".
// Sin dependencias de Arduino. El tiempo se mide con restas sin signo (millis() puede dar la vuelta).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace siblock {

// Aparatos que usan DNS cifrado o Private Relay. Solo en RAM: 16 casillas, se olvidan a las 24 h.
class BypassTable {
 public:
  static constexpr size_t kSlots = 16;
  static constexpr uint32_t kForgetMs = 24UL * 3600 * 1000;
  struct Entry {
    uint32_t ip;
    uint8_t flags;
    uint32_t lastMs;
  };
  // flags 0: nada. Llena: pisa una vencida o, si no hay, la más vieja. Suma flags (OR).
  void note(uint32_t ip, uint8_t flags, uint32_t nowMs);
  uint8_t flagsFor(uint32_t ip, uint32_t nowMs) const;          // 0 si no está o venció
  size_t list(Entry* out, size_t max, uint32_t nowMs) const;    // sin vencidas, más reciente primero

 private:
  bool alive(const Entry& e, uint32_t nowMs) const { return e.flags && nowMs - e.lastMs < kForgetMs; }
  Entry e_[kSlots] = {};
};

enum class ProbeResult : uint8_t { kSkip, kOk, kFail };
ProbeResult parseProbe(const std::string& s);  // "ok" / "fail"; lo demás kSkip

enum class DiagLevel : uint8_t { kOk, kInfo, kWarn, kBad };
const char* levelName(DiagLevel l);  // "ok" "info" "warn" "bad"

struct DiagFinding {
  const char* code;
  DiagLevel level;
};

struct DiagInput {
  ProbeResult probe;
  bool clientSeen;
  uint8_t bypassFlags;
  bool relayBlockOn;
  bool paused;
  bool listLoaded;
  bool upstreamDown;
  bool viaRouter;
  bool devicePaused;       // este aparato tiene su propia pausa (fase 5)
  uint8_t scheduleGroups;  // grupos que un horario le corta ahora a este aparato (kGroup*)
};

// Orden: bad, warn, info, ok (estable dentro de cada nivel). Nunca vacía.
std::vector<DiagFinding> diagnose(const DiagInput& in);

// La puerta de enlace hizo el 80 % o más de al menos 50 consultas. gatewayIp 0 -> false.
bool routerLooksLikeDns(uint32_t gatewayIp, uint32_t gatewayQueries, uint32_t totalQueries);

// true si hubo al menos kUpstreamDownTimeouts timeouts seguidos sin ninguna respuesta en medio, el
// último timeout es más nuevo que la última respuesta y fue hace < 5 min. Un solo reenvío sin
// respuesta (casa tranquila, dominio lento) no basta para decir "sin internet".
constexpr uint32_t kUpstreamDownTimeouts = 3;
bool upstreamLooksDown(uint32_t nowMs, uint32_t lastAnsweredMs, uint32_t lastTimeoutMs,
                       uint32_t timeoutsSinceAnswer);

// Cuenta respuestas y timeouts del DNS real. La tocan varias tareas a la vez (UDP, TCP y la
// callback de respuestas), así que todo es atómico: no se pierde ningún timeout ni ningún reinicio.
class UpstreamWatch {
 public:
  void noteAnswered(uint32_t nowMs) {
    lastAnsweredMs_.store(nowMs);
    timeoutsSinceAnswer_.store(0);
  }
  void noteTimeout(uint32_t nowMs) {
    lastTimeoutMs_.store(nowMs);
    uint32_t c = timeoutsSinceAnswer_.load();
    while (c != UINT32_MAX && !timeoutsSinceAnswer_.compare_exchange_weak(c, c + 1)) {
    }
  }
  bool looksDown(uint32_t nowMs) const {
    return upstreamLooksDown(nowMs, lastAnsweredMs_.load(), lastTimeoutMs_.load(),
                             timeoutsSinceAnswer_.load());
  }
  uint32_t timeoutsSinceAnswer() const { return timeoutsSinceAnswer_.load(); }

 private:
  std::atomic<uint32_t> lastAnsweredMs_{0};
  std::atomic<uint32_t> lastTimeoutMs_{0};
  std::atomic<uint32_t> timeoutsSinceAnswer_{0};
};

}  // namespace siblock

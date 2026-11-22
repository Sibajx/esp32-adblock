// SiBlock: botón BOOT, LED, ventana para crear la contraseña y cuándo abrir el modo de
// configuración por WiFi. Sin dependencias de Arduino. Todo el tiempo se mide con restas sin
// signo (now - inicio), así que millis() puede dar la vuelta (49 días) sin romper nada.
#pragma once
#include <cstdint>

namespace siblock {

enum class ButtonAction { kNone, kOpenSetupWindow, kConfigMode, kFactoryReset };

// Duraciones al soltar: 50 ms..<2 s -> kOpenSetupWindow; 3..8 s -> kConfigMode; lo demás -> kNone.
// kFactoryReset se dispara al llegar a 10 s todavía apretado (sin esperar a soltar), una sola vez.
// Antirrebote de 50 ms: un cambio cuenta solo si dura 50 ms.
class ButtonTracker {
 public:
  void update(bool pressed, uint32_t nowMs);
  ButtonAction take();  // devuelve y limpia la acción pendiente
  bool held() const { return stable_; }
  uint32_t heldMs(uint32_t nowMs) const { return stable_ ? nowMs - pressStart_ : 0; }

 private:
  bool raw_ = false, stable_ = false, resetFired_ = false;
  uint32_t rawSince_ = 0, pressStart_ = 0;
  ButtonAction pending_ = ButtonAction::kNone;
};

// LED mientras se mantiene BOOT: <3 s parpadeo de 100 ms; 3..8 s fijo; 8..10 s parpadeo; >=10 s apagado.
bool ledWhileHeld(uint32_t heldMs, uint32_t nowMs);

// LED de estado (cuando nadie aprieta BOOT): fijo = todo bien; parpadeo lento (1 s) = bajando la
// lista; parpadeo rápido (200 ms) = problema (sin WiFi de la casa, modo de configuración, sin lista
// o el DNS de internet no contesta).
enum class LedState { kOk, kUpdating, kProblem };
LedState ledStateFor(bool staConnected, bool portalOpen, bool updating, uint32_t listCount, bool upstreamDown);
bool ledOn(LedState st, uint32_t nowMs);

// Ventana de 30 min para crear la contraseña del panel. Una vez cerrada queda cerrada hasta el
// próximo open(), aunque millis() dé la vuelta (quien la usa la consulta en cada vuelta).
class SetupWindow {
 public:
  static constexpr uint32_t kMs = 30UL * 60 * 1000;
  void open(uint32_t nowMs);
  bool isOpen(uint32_t nowMs) const;
  uint32_t secondsLeft(uint32_t nowMs) const;

 private:
  mutable bool open_ = false;
  uint32_t start_ = 0;
};

// Cuándo abrir y cerrar el modo de configuración por WiFi (AP + portal):
// - sin WiFi guardado y sin conexión: abre a los 120 s (desde el arranque o el último Improv);
// - con WiFi guardado sin conexión: abre a los 600 s seguidos;
// - abierto con 0 aparatos conectados durante 900 s: cierra y el conteo de 600 s empieza otra vez;
// - abierto y la placa conecta a la casa: cierra. Con aparatos conectados no cierra.
// - abierto a mano (BOOT 3-8 s): no se cierra por estar conectada a la casa, solo tras 900 s sin nadie.
class PortalPolicy {
 public:
  static constexpr uint32_t kNoWifiMs = 120000, kLostWifiMs = 600000, kIdleMs = 900000;
  void tick(uint32_t nowMs, bool hasSavedWifi, bool staConnected, int apStations);
  void forceOpen(uint32_t nowMs);   // BOOT 3–8 s
  void forceClose(uint32_t nowMs);  // WiFi nuevo guardado desde el portal
  void improvSeen(uint32_t nowMs);  // reinicia la espera
  bool wantPortal() const { return open_; }

 private:
  bool started_ = false, open_ = false, forced_ = false;
  uint32_t waitStart_ = 0, idleStart_ = 0;
};

// Direcciones IPv4 en orden "humano": ipv4(192, 168, 4, 1) = 0xC0A80401.
constexpr uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
}
// ¿Las redes a/maskA y b/maskB comparten direcciones? Una máscara 0 es "sin red": nunca choca.
bool subnetsOverlap(uint32_t a, uint32_t maskA, uint32_t b, uint32_t maskB);
// IP de SiBlock en su propia red (siempre /24): la primera de 192.168.4.1, 192.168.243.1,
// 172.31.243.1 y 10.243.77.1 que no choque con la red de la casa (staIp/staMask, 0 si no hay).
// Así un aparato de la casa nunca parece venir de la red de SiBlock.
uint32_t pickApAddress(uint32_t staIp, uint32_t staMask);
constexpr uint32_t kApMask = ipv4(255, 255, 255, 0);
}  // namespace siblock

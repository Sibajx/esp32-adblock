#include "button.h"

namespace {
constexpr uint8_t kBootPin = 0;
constexpr uint8_t kLedPin = 2;
siblock::ButtonTracker gTracker;
int gLed = -1;  // lo último que se escribió en el pin (-1 = nada todavía)
}  // namespace

namespace button {
void begin() {
  pinMode(kBootPin, INPUT_PULLUP);
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  gLed = 0;
}

siblock::ButtonAction poll(uint32_t nowMs, siblock::LedState st) {
  gTracker.update(digitalRead(kBootPin) == LOW, nowMs);
  bool led = gTracker.held() ? siblock::ledWhileHeld(gTracker.heldMs(nowMs), nowMs) : siblock::ledOn(st, nowMs);
  if (int(led) != gLed) {
    gLed = led;
    digitalWrite(kLedPin, led ? HIGH : LOW);
  }
  return gTracker.take();
}
}  // namespace button

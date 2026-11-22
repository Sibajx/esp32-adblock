// Arranque del simulador: igual que el ESP32, setup() una vez y loop() para siempre.
#include <Arduino.h>
void setup();
void loop();
int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setup();
  for (;;) loop();
}

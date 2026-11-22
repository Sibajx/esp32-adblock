// Simulador: Preferences (NVS del ESP32) guardado en $SIM_FLASH/nvs.json con la forma
// {"<espacio>": {"<clave>": "<texto>"}}. Los bytes van como "hex:<hex>" y los enteros como texto
// decimal. Cada put/remove/clear reescribe el archivo entero (nvs.json.tmp + rename).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char* ns, bool readOnly = false);
  void end();
  bool isKey(const char* key);
  String getString(const char* key, const String& def = String());
  size_t putString(const char* key, const String& value);
  size_t getBytesLength(const char* key);
  size_t getBytes(const char* key, void* buf, size_t maxLen);
  size_t putBytes(const char* key, const void* value, size_t len);
  uint32_t getUInt(const char* key, uint32_t def = 0);
  size_t putUInt(const char* key, uint32_t value);
  bool remove(const char* key);
  bool clear();

 private:
  std::string ns_;
  bool open_ = false, readOnly_ = false;
};

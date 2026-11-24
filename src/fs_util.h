// LittleFS escribe "[E] ... does not exist" cada vez que se borra o se abre para leer un archivo que
// no está. Al arrancar se limpian ~70 archivos temporales que casi nunca existen: eran ~70 líneas de
// error falsas en el puerto serie. Por eso se pregunta antes con stat(), que no escribe nada.
// Ojo: LittleFS.exists() no sirve, por dentro hace open() y escribe el mismo error (core 2.0.17).
#pragma once
#include <LittleFS.h>

#ifdef ESP_PLATFORM
#include <sys/stat.h>

#include <string>
#endif

namespace fsu {
inline bool exists(const char* path) {
#ifdef ESP_PLATFORM
  struct stat st;
  std::string full = std::string("/littlefs") + path;  // LittleFS.begin() monta en /littlefs
  return ::stat(full.c_str(), &st) == 0;
#else
  return LittleFS.exists(path);  // simulador
#endif
}
inline void remove(const char* path) {
  if (exists(path)) LittleFS.remove(path);
}
inline void remove(const String& path) { remove(path.c_str()); }
// Igual que LittleFS.open(path, FILE_READ): si no existe, un File vacío (y sin error en el registro).
inline fs::File openRead(const char* path) { return exists(path) ? LittleFS.open(path, FILE_READ) : fs::File(); }
inline fs::File openRead(const String& path) { return openRead(path.c_str()); }
}  // namespace fsu

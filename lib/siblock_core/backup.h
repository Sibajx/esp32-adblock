// SiBlock: formato del respaldo (bajar y subir la configuración) y de /familia.txt.
// Texto por líneas ('\n'; se acepta "\r\n"), máximo 300 bytes por línea, sin líneas vacías ni
// palabras desconocidas:
//
//   SIBLOCK-RESPALDO 1          (o SIBLOCK-FAMILIA 1 en /familia.txt)
//   listas <1, 3, 5 o 7>        (solo respaldo)
//   relay <0|1>                 (solo respaldo)
//   zona <-720..840>
//   sal <32 hex>
//   bloquear <dominio>          (solo respaldo)
//   permitir <dominio>          (solo respaldo)
//   aparato <casilla 0..31> <huella 16 hex> <nombre: el resto de la línea>
//   horario <aparatos 8 hex> <grupos 1..7> <días 1..127> <inicio 0..1439> <fin 0..1439>
//   fin
//
// No lleva WiFi, contraseñas, número de instalación, IPs ni MACs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "family.h"

namespace siblock {

constexpr size_t kBackupMaxBytes = 24576, kBackupMaxLine = 300, kBackupMaxRules = 200;

struct Backup {
  bool full = false;
  uint8_t lists = 1;
  bool relay = false;
  std::vector<std::string> block, allow;
  Family family;
};

// Lee línea por línea (sin el '\n'). Todo o nada: line() devuelve falso en el primer error y desde
// ahí todo es falso. finish() exige cabecera, "sal" y "fin" (y en el respaldo, "listas") y que cada
// horario apunte a aparatos que existen.
class BackupReader {
 public:
  explicit BackupReader(bool full);
  bool line(const char* p, size_t n);
  bool finish();
  // Para quien ve el tamaño antes de leer: deja el lector en error, sin número de línea.
  bool failTooBig();
  const Backup& result() const { return b_; }
  int errorLine() const { return errLine_; }  // 0 = el archivo entero (p. ej. cortado)
  const char* error() const { return err_.c_str(); }

 private:
  bool fail(const char* msg);
  bool parse(const std::string& l);

  Backup b_;
  struct Pending {
    Schedule s;
    int line;
  };
  std::vector<Pending> schedules_;
  int lineNo_ = 0, errLine_ = 0;
  size_t bytes_ = 0;
  bool failed_ = false, header_ = false, salt_ = false, end_ = false, lists_ = false, relay_ = false,
       zone_ = false;
  std::string err_;
};

std::string writeFamily(const Family& f);
std::string writeBackup(uint8_t lists, bool relay, const std::vector<std::string>& block,
                        const std::vector<std::string>& allow, const Family& f);

}  // namespace siblock

// SiBlock: historia de uso de 168 horas (7 días) y top de dominios bloqueados.
// Sin dependencias de Arduino. No es segura entre hilos: el firmware la protege con un lock.
#pragma once
#include <cstddef>
#include <cstdint>

namespace siblock {

// Hora UTC (epoch/3600) o 0 si la hora no es creíble (epoch < 1700000000).
uint32_t epochHour(int64_t epochSeconds);

class UsageHistory {
 public:
  static constexpr size_t kHours = 168;
  // "SBH1" + u32 última hora + 168 × (u32 consultas, u32 bloqueadas), little-endian.
  static constexpr size_t kBytes = 8 + kHours * 8;  // 1352
  struct Slot {
    uint32_t queries, blocked;
  };
  // hour 0 no cuenta. Hora nueva: limpia las casillas entre la última y esta (>= 168 -> todas).
  // Hora vieja dentro de las últimas 168: suma en su casilla. Más vieja: se ignora, salvo que
  // lleguen kStaleReset seguidas: entonces la última hora era falsa (salto al futuro) y la
  // historia empieza de nuevo desde esa hora, en vez de quedar congelada para siempre.
  void add(uint32_t hour, bool blocked);
  static constexpr uint32_t kStaleReset = 64;
  // Las 168 horas que terminan en nowHour, la más vieja primero; sin datos = {0,0}.
  void snapshot(uint32_t nowHour, Slot out[kHours]) const;
  size_t serialize(uint8_t* out, size_t max) const;  // kBytes, o 0 si max < kBytes
  // false (y sin cambios) si el largo o la cabecera no cuadran, o si el contenido es incoherente:
  // última hora fuera de [kMinHour, kMaxHour] (y distinta de 0), casillas con datos sin última hora,
  // o una casilla con más bloqueadas que consultas.
  bool deserialize(const uint8_t* in, size_t len);
  static constexpr uint32_t kMinHour = 472222;   // epochHour(1700000000), noviembre de 2023
  static constexpr uint32_t kMaxHour = 1139568;  // 1 de enero de 2100

 private:
  Slot slots_[kHours] = {};
  uint32_t last_ = 0;
  uint32_t staleRun_ = 0;  // horas demasiado viejas seguidas (no se guarda)
};

// Top de bloqueados con el método Space-Saving: 32 casillas fijas (~2,2 KB). Un dominio nuevo
// con la tabla llena toma la casilla con menos cuenta y hereda esa cuenta + 1, así lo que se
// repite mucho nunca se pierde.
class TopBlocked {
 public:
  static constexpr size_t kSlots = 32, kNameMax = 63;
  struct Entry {
    char name[kNameMax + 1];
    uint32_t count;
  };
  void add(const char* name);                // corta a 63 bytes; vacío o nullptr no cuenta
  size_t top(Entry* out, size_t max) const;  // mayor a menor; empate por nombre
  void halve();                              // cuenta / 2; las que quedan en 0 se sueltan

 private:
  Entry e_[kSlots] = {};
};

// "Cuánto te ahorra": bytes de anuncios que no se bajaron, estimados por consulta bloqueada.
constexpr uint32_t kSavedBytesPerBlocked = 30 * 1024;  // estimado, falta verificar con fuente
uint64_t savedBytes(const UsageHistory::Slot* slots, size_t n);

}  // namespace siblock

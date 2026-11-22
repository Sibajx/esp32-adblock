// SiBlock: aparatos de la familia (huella, nombres), horarios y grupos de sitios.
// Sin dependencias de Arduino ni memoria dinámica en la consulta. No es segura entre hilos: el
// firmware la protege con un lock.
//
// Privacidad: nunca se guarda la MAC ni la IP. Un aparato se guarda por su huella: los primeros
// 8 bytes de SHA-256(sal ‖ MAC), con una sal de 16 bytes al azar propia de cada placa.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace siblock {

constexpr size_t kMaxDevices = 32, kMaxSchedules = 8, kDeviceNameMax = 32;
constexpr uint8_t kGroupSocial = 1, kGroupGames = 2, kGroupAll = 4;

struct DeviceId {
  uint8_t b[8];
};
bool operator==(const DeviceId& a, const DeviceId& b);
inline bool operator!=(const DeviceId& a, const DeviceId& b) { return !(a == b); }

std::string deviceIdHex(const DeviceId& id);                  // 16 hex minúsculas
bool parseDeviceId(const char* s, size_t n, DeviceId& out);  // exactamente 16 hex
DeviceId deviceIdFor(const uint8_t salt[16], const uint8_t mac[6]);

// 1..32 bytes de UTF-8 válido, sin bytes de control (< 0x20, 0x7F) ni espacio al inicio o al final.
bool validDeviceName(const std::string& s);

// devices: bits de casillas 0..31. days: bit0 = lunes … bit6 = domingo. start/end: minutos 0..1439
// en la hora de la casa. Si end < start, cruza la medianoche (cuenta el día en que empieza).
struct Schedule {
  uint32_t devices;
  uint8_t groups, days;
  uint16_t start, end;
};
bool validSchedule(const Schedule& s);

// Minuto de la semana en la hora de la casa (lunes 00:00 = 0), o -1 si la hora no es creíble.
int32_t localMinuteOfWeek(int64_t epochSeconds, int16_t tzMinutes);
bool scheduleActive(const Schedule& s, int32_t minuteOfWeek);
// ¿El nombre (o un padre) es de alguno de los grupos? kGroupAll abarca todo.
bool inGroups(const std::string& name, uint8_t groups);
bool validTzMinutes(int32_t tz);  // -720..840

class Family {
 public:
  Family() { clear(); }
  bool setName(const DeviceId& id, const std::string& name);  // crea o renombra
  bool forget(const DeviceId& id);
  int slotOf(const DeviceId& id) const;  // -1 si no está
  const char* nameAt(int slot) const;    // nullptr si la casilla está libre
  bool used(int slot) const;
  DeviceId idAt(int slot) const;
  // Pone un aparato en una casilla dada (para leer el archivo). Falso si la casilla está usada,
  // la huella ya está en otra o el nombre no es válido.
  bool setSlot(int slot, const DeviceId& id, const std::string& name);

  bool setSchedule(int index, const Schedule& s);  // index -1 = nuevo
  bool removeSchedule(int index);
  size_t scheduleCount() const { return nSchedules_; }
  const Schedule& scheduleAt(size_t i) const { return schedules_[i]; }
  uint32_t usedMask() const;

  bool blockedBySchedule(int slot, const std::string& name, int32_t minuteOfWeek) const;
  // Grupos (OR) de los horarios de esa casilla que están activos ahora (para el diagnóstico).
  uint8_t activeGroups(int slot, int32_t minuteOfWeek) const;
  void clear();

  int16_t tz = 0;
  uint8_t salt[16] = {};
  bool hasSalt = false;

 private:
  struct Slot {
    bool used;
    DeviceId id;
    char name[kDeviceNameMax + 1];
  };
  Slot slots_[kMaxDevices];
  Schedule schedules_[kMaxSchedules];
  size_t nSchedules_ = 0;
};

}  // namespace siblock

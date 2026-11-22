// Aparatos de la familia en la placa: nombres, pausa por aparato y horarios.
// Une siblock::Family con la tabla ARP (lwIP), LittleFS (/familia.txt) y la tarea DNS.
//
// Privacidad: la MAC se usa solo un instante para sacar la huella (SHA-256 con sal); ni la MAC ni
// la IP se guardan en la flash. La caché IP -> huella vive en RAM (16 entradas, 5 min).
// Si la tabla ARP no tiene la IP, el aparato no se reconoce y no se le aplica nada (falla abierto).
#pragma once
#include <Arduino.h>

#include <string>

#include "backup.h"
#include "family.h"

namespace family {

void begin();  // lee /familia.txt; si no está o está dañado: tabla vacía y sal nueva

// Huella del aparato que usa esta IP (orden de red, como uint32_t(IPAddress)). Falso si la tabla
// ARP no la tiene.
bool idForIp(uint32_t ip, siblock::DeviceId& out);
// ¿Esta huella se vio en los últimos 5 min? (en `ip` devuelve su IP de ahora). Sirve para no dejar
// nombrar aparatos inventados.
bool seenRecently(const siblock::DeviceId& id, uint32_t* ip = nullptr);

enum class Decision { kNone, kBlockSchedule, kPaused };
// Para la tarea DNS. Sin horarios ni pausas devuelve kNone de inmediato (sin ARP ni lock).
Decision decide(uint32_t ip, const std::string& name);
// Para «Algo no funciona»: ¿el aparato de esta IP está en pausa y qué grupos le corta un horario
// ahora? Sin nombre, sin ARP o sin hora: falso y 0.
void diagFor(uint32_t ip, bool& paused, uint8_t& scheduleGroups);

int slotOf(const siblock::DeviceId& id);  // -1 si no tiene nombre
size_t namedCount();
uint32_t usedMask();  // casillas con nombre
size_t scheduleCount();
int16_t tz();

bool setName(const siblock::DeviceId& id, const String& name);
bool forget(const siblock::DeviceId& id);
bool pause(const siblock::DeviceId& id, uint32_t minutes);  // 0 = quitar; 1..1440; solo con nombre
uint32_t pauseLeft(int slot);                                 // segundos
bool setSchedule(int index, const siblock::Schedule& s, int16_t tz);  // index -1 = nuevo
bool removeSchedule(int index);
void snapshot(siblock::Family& out);  // copia bajo el lock
bool replace(const siblock::Family& f);  // para restaurar un respaldo (guarda en la flash)
void erase();  // borra /familia.txt y deja la tabla vacía con sal nueva (BOOT 10 s)
bool clockOk();  // ¿hay hora de internet creíble? (sin ella los horarios no bloquean)

// Lee un archivo línea por línea con el lector del respaldo (falso si no se puede abrir o si el
// lector da error). Lo usa también la restauración.
bool readFile(const char* path, siblock::BackupReader& r);

}  // namespace family

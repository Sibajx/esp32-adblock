// Lista de bloqueo guardada en LittleFS como claves de 40 bits ordenadas (5 bytes c/u),
// seguidas del índice, en un solo archivo (/list.bin) para que el cambio de lista sea atómico.
// En RAM solo viven el índice de 4097 posiciones (16 KB) y las reglas propias.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <memory>
#include <string>
#include <vector>

#include "adblock_core.h"
#include "community.h"

enum class Verdict : uint8_t {
  kAllowed,        // no está en ninguna lista
  kBlockedList,    // está en la lista descargada
  kBlockedCustom,  // lo bloqueaste tú
  kAllowedCustom,  // lo permitiste tú (o está en ALLOWLIST)
  kPaused,         // el bloqueo está en pausa
  kBlockedSchedule,  // un horario de la familia lo bloquea para este aparato
  kPausedDevice,     // la lista lo bloquearía, pero este aparato está en pausa
  kAllowedCommunity, // la lista lo bloquearía, pero está en los permitidos de la comunidad
};
inline bool isBlockedVerdict(Verdict v) {
  return v == Verdict::kBlockedList || v == Verdict::kBlockedCustom || v == Verdict::kBlockedSchedule;
}

class BlockList {
 public:
  bool begin();  // carga la lista y las reglas guardadas
  Verdict check(const std::string& name);  // seguro de llamar desde otra tarea

  // Baja todas las listas y reconstruye; si algo falla deja la anterior. allowShrink (cambió la
  // selección de listas): se acepta aunque la lista nueva sea mucho más chica.
  bool update(const char* const* urls, size_t nUrls, bool allowShrink = false);

  // Reglas propias (se guardan en flash).
  bool addRule(const std::string& domain, bool block);
  bool removeRule(const std::string& domain);
  void rules(std::vector<std::string>& blocked, std::vector<std::string>& allowed);
  // Reemplaza todas las reglas (restaurar un respaldo). Todo o nada: si alguna no vale o no se
  // puede guardar, quedan las de antes.
  bool replaceRules(const std::vector<std::string>& block, const std::vector<std::string>& allow);

  // Permitidos de la comunidad: huellas de siblock::communityHash hechas con la llave `key`,
  // ordenadas (vacío = ninguno). Van aparte de las reglas propias: no se guardan en /custom.txt ni
  // salen en el respaldo.
  void setCommunity(const siblock::CommunityKey& key, std::vector<uint64_t> keys);

  // Pausa: 0 segundos = reanudar.
  void pause(uint32_t seconds);
  uint32_t pausedSecondsLeft() const;

  uint32_t count() const { return count_; }
  uint32_t lastUpdateMs() const { return lastUpdateMs_; }
  bool everUpdated() const { return everUpdated_; }
  bool updating() const { return updating_; }
  String lastError() const;
  uint32_t lastSkipped() const { return skipped_; }  // dominios ignorados por el tope
  void noteError(const String& msg);  // error que el panel muestra (p. ej. sin RAM para bajar)

 private:
  bool loadFromFs();
  bool lookupKey(uint64_t key);
  bool download(const char* url, uint32_t& added);
  bool buildFromBuckets(uint32_t& total);
  void cleanupTemp();
  bool saveRules();

  SemaphoreHandle_t mutex_ = nullptr;   // protege index_, file_, rules_, communityKey_, community_, lastError_
  std::unique_ptr<uint32_t[]> index_;
  fs::File file_;
  adblock::CustomRules rules_;
  siblock::CommunityKey communityKey_{};
  std::vector<uint64_t> community_;
  volatile uint32_t count_ = 0;
  volatile uint32_t lastUpdateMs_ = 0;
  volatile bool everUpdated_ = false;
  volatile bool updating_ = false;
  volatile uint32_t pauseUntilMs_ = 0;
  mutable volatile bool paused_ = false;  // se apaga solo al vencer (no depende de millis)
  volatile uint32_t skipped_ = 0;
  uint32_t skipping_ = 0;
  uint32_t rejectedTotal_ = 0;  // tamaño de la última lista rechazada por chica
  bool sizeUnknown_ = false;  // alguna lista de esta actualización llegó sin Content-Length
  bool allowShrink_ = false;  // cambió la selección de listas: se acepta aunque sea más chica
  String lastError_;
};

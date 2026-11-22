// Servidor DNS: contesta 0.0.0.0 a lo bloqueado, guarda en caché y reenvía lo demás al DNS real.
// UDP (lo normal) y TCP (respuestas grandes) en el puerto 53.
#pragma once
#include <Arduino.h>
#include <AsyncUDP.h>

#include "blocklist.h"
#include "diagnose.h"
#include "usage.h"

struct DnsStats {
  uint32_t queries = 0;
  uint32_t blocked = 0;
  uint32_t cached = 0;     // contestadas desde la caché
  uint32_t forwarded = 0;
  uint32_t answered = 0;   // respuestas del DNS real entregadas
  uint32_t dropped = 0;    // mal formadas, de fuera de la red local o cola llena
  uint32_t timeouts = 0;
  uint32_t tcp = 0;        // consultas por TCP
  uint32_t rebind = 0;     // respuestas rechazadas por apuntar a la red local
  uint32_t canary = 0;     // NXDOMAIN al canario de Firefox (use-application-dns.net)
  uint32_t relay = 0;      // NXDOMAIN a iCloud Private Relay (solo con la opción activada)
  uint32_t schedule = 0;   // cortadas por un horario (no cuentan en `blocked`, que es de anuncios y reglas)
};

struct RecentQuery {
  char name[64];
  Verdict verdict;
  uint32_t client;
};

struct ClientStat {
  uint32_t ip;
  uint32_t queries;
  uint32_t blocked;
  uint32_t lastMs;
};

class DnsServer {
 public:
  bool begin(BlockList* list, IPAddress upstream1, IPAddress upstream2);
  DnsStats stats() const { return stats_; }
  // Copia las últimas consultas (más reciente primero). Devuelve cuántas.
  size_t recent(RecentQuery* out, size_t max);
  // Copia los aparatos que más consultan. Devuelve cuántos.
  size_t topClients(ClientStat* out, size_t max);
  size_t cacheEntries();
  void clearCache();
  // Autoprueba: el ESP32 se manda un paquete al puerto 53. Si llega, probesSeen() sube.
  void sendProbe();
  uint32_t probesSeen() const { return probesSeen_; }
  IPAddress currentUpstream() const { return upstream_[upstreamIdx_]; }
  // Modo de configuración (portal cautivo): con apIp != 0, toda consulta de un aparato de la red
  // del AP (apIp/24) se contesta con apIp. 0 = apagado.
  void setCaptive(uint32_t apIp) { captive_ = apIp; }

  // Opción "Apagar iCloud Private Relay en esta casa" (la lee la tarea DNS).
  void setRelayBlock(bool on) { relayBlock_ = on; }
  bool relayBlock() const { return relayBlock_; }
  // Aparatos que se saltan SiBlock (DNS cifrado o Private Relay). Solo en RAM.
  uint8_t bypassFlagsFor(uint32_t ip);
  size_t bypassList(siblock::BypassTable::Entry* out, size_t max);
  // De la tabla de aparatos: ¿preguntó algo en los últimos `ms`? ¿cuántas consultas hizo?
  bool clientSeenWithin(uint32_t ip, uint32_t ms);
  uint32_t clientQueries(uint32_t ip);
  uint32_t totalClientQueries();
  // El DNS de internet no contesta (último timeout más nuevo que la última respuesta, < 5 min).
  bool upstreamDown();
  // Historia de 168 h y top de bloqueados (copias hechas dentro de su lock).
  void historySnapshot(uint32_t nowHour, siblock::UsageHistory::Slot* out);
  size_t topBlocked(siblock::TopBlocked::Entry* out, size_t max);
  size_t saveHistory(uint8_t* out, size_t max);
  bool loadHistory(const uint8_t* in, size_t len);
  void halveTop();

 private:
  static void udpTaskEntry(void* self);
  static void tcpTaskEntry(void* self);
  void udpTask();
  void tcpTask();
  void onClientPacket(AsyncUDPPacket& p);
  void onUpstreamPacket(AsyncUDPPacket& p, uint8_t sock);
  void handleQuery(const uint8_t* data, size_t len, IPAddress ip, uint16_t port);
  // Contesta sin salir a internet (bloqueo o caché). Devuelve el largo o 0 si hay que reenviar.
  size_t answerLocally(const uint8_t* data, const adblock::DnsQuestion& q, uint32_t client,
                       uint8_t* out, Verdict& v);
  bool isLocalClient(IPAddress ip);
  bool isApClient(IPAddress ip) const;
  // Aplica la protección anti-rebinding a una respuesta del DNS real. Devuelve el largo final.
  size_t guardRebind(uint8_t* resp, size_t len, bool allowPrivate);
  void logQuery(const std::string& name, Verdict v, uint32_t client);
  void countClient(uint32_t ip, bool blocked);
  void noteAnswered();
  void noteTimeout();

  BlockList* list_ = nullptr;
  AsyncUDP server_;
  // Varios sockets (cada uno con su puerto al azar) hacia el DNS real: para falsificar una
  // respuesta hay que adivinar el ID de 16 bits y además cuál de los puertos se usó.
  static constexpr uint8_t kUpSocks = 4;
  AsyncUDP upstreamSock_[kUpSocks];
  IPAddress upstream_[2];
  volatile uint8_t upstreamIdx_ = 0;
  volatile uint8_t consecutiveTimeouts_ = 0;
  QueueHandle_t queue_ = nullptr;
  volatile uint32_t probesSeen_ = 0;
  volatile uint32_t captive_ = 0;
  volatile bool relayBlock_ = false;
  siblock::UpstreamWatch upstreamWatch_;  // atómico: lo tocan las tareas UDP, TCP y la callback
  DnsStats stats_;
};

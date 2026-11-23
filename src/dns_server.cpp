#include "dns_server.h"

#include <WiFi.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

#include <time.h>

#include <cstring>

#include "adblock_core.h"
#include "config.h"
#include "dns_names.h"
#include "family_store.h"

using namespace adblock;

namespace {
constexpr size_t kMaxQuery = 512;       // consultas UDP más grandes se descartan
constexpr int kQueueLen = 16;      // ráfagas de consultas (8 KB de RAM)
constexpr int kPending = 64;            // consultas reenviadas esperando respuesta
constexpr uint32_t kPendingTtlMs = 4000;
constexpr int kRecent = 20;
constexpr int kClients = 32;
constexpr size_t kTcpMax = 4096;        // respuestas TCP más grandes -> SERVFAIL
constexpr uint32_t kTcpTimeoutMs = 1500;  // un cliente que abre y no manda nada no traba el TCP

struct QueuedQuery {
  uint8_t data[kMaxQuery];
  uint16_t len;
  uint16_t port;
  uint32_t ip;
};

struct Pending {
  bool used;
  uint16_t upId;
  uint16_t origId;
  uint16_t port;
  uint32_t ip;
  uint32_t sentMs;
  uint32_t nameHash;  // la respuesta tiene que traer la misma pregunta
  uint16_t qtype;
  uint8_t upstream;
  uint8_t sock;       // por cuál socket salió (la respuesta tiene que volver por el mismo)
  bool allowPrivate;  // lo permitiste a mano: puede apuntar a la red local
};

Pending gPending[kPending];
portMUX_TYPE gPendingMux = portMUX_INITIALIZER_UNLOCKED;

RecentQuery gRecent[kRecent];
size_t gRecentHead = 0, gRecentCount = 0;
portMUX_TYPE gRecentMux = portMUX_INITIALIZER_UNLOCKED;

ClientStat gClients[kClients];
portMUX_TYPE gClientsMux = portMUX_INITIALIZER_UNLOCKED;

// Historia de 168 h, top de bloqueados y aparatos que se saltan SiBlock: un lock para cada uno.
// Las lecturas copian dentro del lock y trabajan con la copia afuera.
siblock::UsageHistory gHistory;
portMUX_TYPE gHistoryMux = portMUX_INITIALIZER_UNLOCKED;
siblock::TopBlocked gTop;
portMUX_TYPE gTopMux = portMUX_INITIALIZER_UNLOCKED;
siblock::BypassTable gBypass;
portMUX_TYPE gBypassMux = portMUX_INITIALIZER_UNLOCKED;

DnsCache gCache;
SemaphoreHandle_t gCacheMutex = nullptr;

uint8_t gOut[kMaxDnsPacket];  // solo lo usa la tarea UDP

uint32_t nameHash(const std::string& n) { return uint32_t(hashDomain(n)); }

void writeTcpMessage(WiFiClient& c, const uint8_t* data, size_t len) {
  uint8_t hdr[2] = {uint8_t(len >> 8), uint8_t(len)};
  c.write(hdr, 2);
  c.write(data, len);
}

// Lee exactamente n bytes o falla al vencer el plazo.
bool readExact(WiFiClient& c, uint8_t* buf, size_t n, uint32_t deadline) {
  size_t got = 0;
  while (got < n) {
    if (int32_t(millis() - deadline) > 0 || !c.connected()) return false;
    int avail = c.available();
    if (avail <= 0) {
      delay(2);
      continue;
    }
    int r = c.read(buf + got, std::min(size_t(avail), n - got));
    if (r > 0) got += size_t(r);
  }
  return true;
}
}  // namespace

bool DnsServer::begin(BlockList* list, IPAddress up1, IPAddress up2) {
  list_ = list;
  upstream_[0] = up1;
  upstream_[1] = up2;
  gCacheMutex = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(kQueueLen, sizeof(QueuedQuery));
  if (!queue_ || !gCacheMutex) return false;
  // Pilas de 8 KB: la búsqueda lee la flash y usa std::string.
  if (xTaskCreatePinnedToCore(udpTaskEntry, "dns", 8192, this, 5, nullptr, 1) != pdPASS) return false;
  if (xTaskCreatePinnedToCore(tcpTaskEntry, "dns_tcp", 8192, this, 4, nullptr, 1) != pdPASS)
    return false;
  for (uint8_t i = 0; i < kUpSocks; i++) {
    if (!upstreamSock_[i].listen(0)) return false;  // puerto de origen aleatorio
    upstreamSock_[i].onPacket([this, i](AsyncUDPPacket& p) { onUpstreamPacket(p, i); });
  }
  if (!server_.listen(53)) return false;
  server_.onPacket([this](AsyncUDPPacket& p) { onClientPacket(p); });
  return true;
}

bool DnsServer::isApClient(IPAddress ip) const {
  uint32_t ap = captive_;
  if (!ap) return false;
  uint32_t mask = uint32_t(IPAddress(255, 255, 255, 0));
  if ((uint32_t(ip) & mask) != (ap & mask)) return false;
  // Si la red de la casa choca con la del AP (portal.cpp lo evita al abrir, pero la placa puede
  // conectarse después a otra red) no se sabe de dónde viene: se trata como de la casa.
  if (WiFi.status() == WL_CONNECTED) {
    uint32_t sm = uint32_t(WiFi.subnetMask()), sl = uint32_t(WiFi.localIP());
    if (sm && ((sl & sm & mask) == (ap & sm & mask))) return false;
  }
  return true;
}

bool DnsServer::isLocalClient(IPAddress ip) {
  // Solo se contesta a la red de la casa (y a la del AP en modo de configuración): así no sirve
  // de DNS abierto si alguien abre el puerto. Sin WiFi la máscara es 0: no se acepta a nadie.
  uint32_t mask = uint32_t(WiFi.subnetMask());
  if (mask && (uint32_t(ip) & mask) == (uint32_t(WiFi.localIP()) & mask)) return true;
  return isApClient(ip);
}

// Paquete que el propio ESP32 se manda para comprobar que el puerto 53 sigue recibiendo.
static const uint8_t kProbe[12] = {'a', 'd', 'b', 'l', 'o', 'c', 'k', '-', 'p', 'r', 'b', '!'};

void DnsServer::sendProbe() {
  upstreamSock_[0].writeTo(kProbe, sizeof kProbe, WiFi.localIP(), 53);
}

// Corre en la tarea async_udp (pila de 4 KB): solo copia a la cola.
void DnsServer::onClientPacket(AsyncUDPPacket& p) {
  if (p.length() == sizeof kProbe && p.remoteIP() == WiFi.localIP() &&
      memcmp(p.data(), kProbe, sizeof kProbe) == 0) {
    probesSeen_++;  // no cuenta como consulta
    return;
  }
  stats_.queries++;
  if (p.length() > kMaxQuery || !isLocalClient(p.remoteIP())) {
    stats_.dropped++;
    return;
  }
  static QueuedQuery q;  // solo la toca esta tarea
  memcpy(q.data, p.data(), p.length());
  q.len = uint16_t(p.length());
  q.port = p.remotePort();
  q.ip = uint32_t(p.remoteIP());
  if (xQueueSend(queue_, &q, 0) != pdTRUE) stats_.dropped++;
}

void DnsServer::udpTaskEntry(void* self) { static_cast<DnsServer*>(self)->udpTask(); }
void DnsServer::tcpTaskEntry(void* self) { static_cast<DnsServer*>(self)->tcpTask(); }

void DnsServer::udpTask() {
  // Vigilante: si esta tarea se traba 30 s, el ESP32 se reinicia solo (la casa sin DNS es peor).
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(nullptr);
  static QueuedQuery q;
  while (true) {
    esp_task_wdt_reset();
    if (xQueueReceive(queue_, &q, pdMS_TO_TICKS(1000)) == pdTRUE)
      handleQuery(q.data, q.len, IPAddress(q.ip), q.port);
  }
}

size_t DnsServer::answerLocally(const uint8_t* data, const DnsQuestion& q, uint32_t client,
                                uint8_t* out, Verdict& v) {
  // 1. Nombres especiales (valen también en pausa).
  switch (siblock::classifySpecial(q.name)) {
    case siblock::SpecialName::kProbe:  // "¿este aparato usa SiBlock?": contesta la IP de la placa
      countClient(client, false);
      if (uint32_t ip = siblock::probeAnswerIp(uint32_t(WiFi.localIP()), uint32_t(WiFi.softAPIP())))
        return buildAddressResponse(data, q, ip, out);
      return buildServfail(data, q, out);  // sin IP todavía: la imagen falla y el panel muestra ❌
    case siblock::SpecialName::kCanary:  // apaga el DNS cifrado automático de Firefox
      stats_.canary++;
      countClient(client, false);
      return buildNxdomain(data, q, out);
    case siblock::SpecialName::kRelay:
      if (relayBlock_) {
        stats_.relay++;
        countClient(client, false);
        return buildNxdomain(data, q, out);
      }
      break;
    case siblock::SpecialName::kDdr:  // DDR (RFC 9462): no hay DNS cifrado que anunciar -> NODATA
      countClient(client, false);
      return buildNodata(data, q, out);
    default:
      break;
  }
  // 2. Aparatos que se saltan SiBlock: se anotan (no se bloquean).
  uint8_t flags = siblock::bypassFlagsFor(q.name);
  if (relayBlock_) flags &= uint8_t(~siblock::kBypassRelay);
  if (flags) {
    uint32_t now = millis();
    portENTER_CRITICAL(&gBypassMux);
    gBypass.note(client, flags, now);
    portEXIT_CRITICAL(&gBypassMux);
  }
  // 3. Aparatos de la familia: un horario gana sobre "Permitir" y vale aunque la casa esté en
  //    pausa; la pausa de un aparato deja pasar lo que la lista bloquearía.
  family::Decision fam = family::decide(client, q.name);
  // 4. Lista, caché y reenvío. Cada consulta suma a la historia (si hay hora de internet).
  if (fam == family::Decision::kBlockSchedule) {
    v = Verdict::kBlockedSchedule;
  } else {
    v = list_->check(q.name);
    if (fam == family::Decision::kPaused && isBlockedVerdict(v)) v = Verdict::kPausedDevice;
  }
  bool blocked = isBlockedVerdict(v);
  uint32_t hour = siblock::epochHour(int64_t(time(nullptr)));
  if (hour) {
    // Lo que bloquea un horario cuenta como consulta pero no como bloqueada: las bloqueadas de la
    // historia dan "cuánto te ahorra", que es de anuncios (igual que el top).
    portENTER_CRITICAL(&gHistoryMux);
    gHistory.add(hour, blocked && v != Verdict::kBlockedSchedule);
    portEXIT_CRITICAL(&gHistoryMux);
  }
  if (blocked) {
    // Regla: "bloqueadas" (tarjeta, %, columna por aparato, gráfica, ahorro y top) es solo de
    // anuncios y reglas tuyas. Lo que corta un horario va aparte en stats_.schedule.
    if (v == Verdict::kBlockedSchedule) {
      stats_.schedule++;
      countClient(client, false);
    } else {
      portENTER_CRITICAL(&gTopMux);
      gTop.add(q.name.c_str());
      portEXIT_CRITICAL(&gTopMux);
      stats_.blocked++;
      countClient(client, true);
    }
    logQuery(q.name, v, client);
    return buildBlockedResponse(data, q, out);
  }
  countClient(client, false);
  size_t n = 0;
  if (xSemaphoreTake(gCacheMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    n = gCache.get(DnsCache::key(q), q.id, millis(), out);
    xSemaphoreGive(gCacheMutex);
  }
  if (n) {
    stats_.cached++;
    logQuery(q.name, v, client);
  }
  return n;
}

void DnsServer::handleQuery(const uint8_t* data, size_t len, IPAddress ip, uint16_t port) {
  DnsQuestion dq;
  if (!parseQuery(data, len, dq)) {
    stats_.dropped++;
    return;
  }
  if (isApClient(ip)) {  // portal cautivo: todo nombre apunta a la placa
    uint32_t ap = captive_;
    if (ap) {
      size_t n = buildAddressResponse(data, dq, ap, gOut);
      server_.writeTo(gOut, n, ip, port);
      return;
    }
  }
  Verdict v;
  if (size_t n = answerLocally(data, dq, uint32_t(ip), gOut, v)) {
    server_.writeTo(gOut, n, ip, port);
    return;
  }

  // Reenvío: ID nuevo y aleatorio para que nadie pueda colar respuestas falsas.
  uint32_t now = millis();
  uint16_t upId = uint16_t(esp_random());
  int slot = -1;
  uint8_t up = upstreamIdx_;
  uint8_t sock = uint8_t(esp_random() % kUpSocks);
  portENTER_CRITICAL(&gPendingMux);
  for (int i = 0; i < kPending; i++) {
    Pending& e = gPending[i];
    if (e.used && now - e.sentMs > kPendingTtlMs) {  // vencida: el DNS no contestó
      e.used = false;
      stats_.timeouts++;
      upstreamWatch_.noteTimeout(now);
      if (e.upstream == upstreamIdx_ && ++consecutiveTimeouts_ >= 3) {
        upstreamIdx_ ^= 1;  // cambia al otro DNS
        consecutiveTimeouts_ = 0;
      }
    }
    if (!e.used && slot < 0) slot = i;
  }
  if (slot >= 0)
    gPending[slot] = {true,   upId,     dq.id, port, uint32_t(ip), now, nameHash(dq.name),
                      dq.qtype, up,     sock,  v == Verdict::kAllowedCustom};
  portEXIT_CRITICAL(&gPendingMux);

  if (slot < 0) {  // demasiadas consultas en vuelo
    size_t n = buildServfail(data, dq, gOut);
    server_.writeTo(gOut, n, ip, port);
    stats_.dropped++;
    return;
  }
  memcpy(gOut, data, len);
  gOut[0] = uint8_t(upId >> 8);
  gOut[1] = uint8_t(upId);
  upstreamSock_[sock].writeTo(gOut, len, upstream_[up], 53);
  stats_.forwarded++;
  logQuery(dq.name, v, uint32_t(ip));
}

// Corre en la tarea async_udp: valida, guarda en caché y devuelve al cliente.
void DnsServer::onUpstreamPacket(AsyncUDPPacket& p, uint8_t sock) {
  if (p.length() < 12 || p.length() > kMaxDnsPacket || p.remotePort() != 53) return;
  uint32_t from = uint32_t(p.remoteIP());
  if (from != uint32_t(upstream_[0]) && from != uint32_t(upstream_[1])) return;
  uint8_t* d = p.data();
  DnsQuestion rq;
  if (!parseResponseQuestion(d, p.length(), rq)) return;
  uint32_t h = nameHash(rq.name);

  Pending found{};
  portENTER_CRITICAL(&gPendingMux);
  for (int i = 0; i < kPending; i++) {
    Pending& e = gPending[i];
    if (e.used && e.sock == sock && e.upId == rq.id && uint32_t(upstream_[e.upstream]) == from &&
        e.nameHash == h && e.qtype == rq.qtype) {
      found = e;
      e.used = false;
      break;
    }
  }
  if (found.used) consecutiveTimeouts_ = 0;
  portEXIT_CRITICAL(&gPendingMux);
  if (!found.used) return;

  size_t len = guardRebind(d, p.length(), found.allowPrivate);
  // Mientras se baja la lista (TLS + búferes) o con poca RAM seguida, no se llena la caché: un
  // malloc fallido aquí reiniciaría el ESP32.
  bool roomy = !list_->updating() && ESP.getMaxAllocHeap() >= 24 * 1024;
  if (len == p.length() && roomy && xSemaphoreTake(gCacheMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    gCache.put(DnsCache::key(rq), d, len, millis());
    xSemaphoreGive(gCacheMutex);
  }
  d[0] = uint8_t(found.origId >> 8);
  d[1] = uint8_t(found.origId);
  server_.writeTo(d, len, IPAddress(found.ip), found.port);
  stats_.answered++;
  noteAnswered();
}

// DNS por TCP: lo usan los clientes cuando la respuesta UDP llegó truncada. Una conexión a la
// vez; se reenvía al DNS real también por TCP.
void DnsServer::tcpTask() {
  static WiFiServer srv(53);
  static uint8_t q[kMaxDnsPacket], resp[kTcpMax], hdr[2];
  srv.begin();
  while (true) {
    WiFiClient c = srv.available();
    if (!c) {
      delay(20);
      continue;
    }
    uint32_t deadline = millis() + kTcpTimeoutMs;
    uint32_t client = uint32_t(c.remoteIP());
    if (!isLocalClient(c.remoteIP()) || !readExact(c, hdr, 2, deadline)) {
      c.stop();
      continue;
    }
    size_t len = size_t(hdr[0]) << 8 | hdr[1];
    DnsQuestion dq;
    stats_.queries++;
    stats_.tcp++;
    if (len < 12 || len > sizeof q || !readExact(c, q, len, deadline) || !parseQuery(q, len, dq)) {
      stats_.dropped++;
      c.stop();
      continue;
    }
    // Portal cautivo también por TCP: al aparato de la red de SiBlock se le contesta con la IP de
    // la placa y nunca se reenvía (ni se espera a un DNS que quizá no se alcanza).
    uint32_t ap = isApClient(c.remoteIP()) ? captive_ : 0;
    if (ap) {
      size_t n = buildAddressResponse(q, dq, ap, resp);
      writeTcpMessage(c, resp, n);
      c.stop();
      continue;
    }
    Verdict v;
    size_t n = answerLocally(q, dq, client, resp, v);
    if (!n) {
      WiFiClient up;
      size_t rlen = 0;
      if (up.connect(upstream_[upstreamIdx_], 53, int32_t(kTcpTimeoutMs))) {
        writeTcpMessage(up, q, len);
        uint32_t upDeadline = millis() + kTcpTimeoutMs;
        if (readExact(up, hdr, 2, upDeadline)) {
          rlen = size_t(hdr[0]) << 8 | hdr[1];
          if (rlen < 12 || rlen > sizeof resp || !readExact(up, resp, rlen, upDeadline)) rlen = 0;
        }
        up.stop();
      }
      if (rlen) rlen = guardRebind(resp, rlen, v == Verdict::kAllowedCustom);
      if (rlen) {
        n = rlen;
        stats_.forwarded++;
        stats_.answered++;
        noteAnswered();
      } else {
        n = buildServfail(q, dq, resp);
        stats_.timeouts++;
        noteTimeout();
      }
      logQuery(dq.name, v, client);
    }
    writeTcpMessage(c, resp, n);
    c.stop();
  }
}

size_t DnsServer::guardRebind(uint8_t* resp, size_t len, bool allowPrivate) {
#if REBIND_PROTECTION
  if (!allowPrivate && responseHasPrivateAddress(resp, len)) {
    stats_.rebind++;
    return refuseResponse(resp, len);
  }
#endif
  return len;
}

void DnsServer::countClient(uint32_t ip, bool blocked) {
  uint32_t now = millis();
  portENTER_CRITICAL(&gClientsMux);
  int slot = -1, empty = -1, oldest = -1;
  for (int i = 0; i < kClients; i++) {
    const ClientStat& c = gClients[i];
    if (!c.queries) {
      if (empty < 0) empty = i;
    } else if (c.ip == ip) {
      slot = i;
      break;
    } else if (oldest < 0 || now - c.lastMs > now - gClients[oldest].lastMs) {
      oldest = i;
    }
  }
  if (slot < 0) {  // aparato nuevo: hueco libre o el que lleva más tiempo sin preguntar
    slot = empty >= 0 ? empty : oldest;
    gClients[slot] = {ip, 0, 0, now};
  }
  gClients[slot].queries++;
  if (blocked) gClients[slot].blocked++;
  gClients[slot].lastMs = now;
  portEXIT_CRITICAL(&gClientsMux);
}

size_t DnsServer::topClients(ClientStat* out, size_t max) {
  ClientStat all[kClients];
  portENTER_CRITICAL(&gClientsMux);
  memcpy(all, gClients, sizeof all);
  portEXIT_CRITICAL(&gClientsMux);
  std::sort(all, all + kClients, [](const ClientStat& a, const ClientStat& b) {
    return a.queries > b.queries;
  });
  size_t n = 0;
  while (n < max && n < size_t(kClients) && all[n].queries) {
    out[n] = all[n];
    n++;
  }
  return n;
}

void DnsServer::clearCache() {
  if (xSemaphoreTake(gCacheMutex, portMAX_DELAY) == pdTRUE) {
    gCache.clear();
    xSemaphoreGive(gCacheMutex);
  }
}

size_t DnsServer::cacheEntries() {
  size_t n = 0;
  if (xSemaphoreTake(gCacheMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    n = gCache.size();
    xSemaphoreGive(gCacheMutex);
  }
  return n;
}

void DnsServer::logQuery(const std::string& name, Verdict v, uint32_t client) {
  portENTER_CRITICAL(&gRecentMux);
  RecentQuery& r = gRecent[gRecentHead];
  strlcpy(r.name, name.c_str(), sizeof r.name);
  r.verdict = v;
  r.client = client;
  gRecentHead = (gRecentHead + 1) % kRecent;
  if (gRecentCount < kRecent) gRecentCount++;
  portEXIT_CRITICAL(&gRecentMux);
}

size_t DnsServer::recent(RecentQuery* out, size_t max) {
  portENTER_CRITICAL(&gRecentMux);
  size_t n = std::min(max, gRecentCount);
  for (size_t i = 0; i < n; i++) out[i] = gRecent[(gRecentHead + kRecent - 1 - i) % kRecent];
  portEXIT_CRITICAL(&gRecentMux);
  return n;
}

void DnsServer::noteAnswered() { upstreamWatch_.noteAnswered(millis()); }

void DnsServer::noteTimeout() { upstreamWatch_.noteTimeout(millis()); }

bool DnsServer::upstreamDown() { return upstreamWatch_.looksDown(millis()); }

uint8_t DnsServer::bypassFlagsFor(uint32_t ip) {
  uint32_t now = millis();
  portENTER_CRITICAL(&gBypassMux);
  uint8_t f = gBypass.flagsFor(ip, now);
  portEXIT_CRITICAL(&gBypassMux);
  return f;
}

size_t DnsServer::bypassList(siblock::BypassTable::Entry* out, size_t max) {
  uint32_t now = millis();
  portENTER_CRITICAL(&gBypassMux);
  siblock::BypassTable copy = gBypass;
  portEXIT_CRITICAL(&gBypassMux);
  return copy.list(out, max, now);
}

bool DnsServer::clientSeenWithin(uint32_t ip, uint32_t ms) {
  uint32_t now = millis();
  bool seen = false;
  portENTER_CRITICAL(&gClientsMux);
  for (const ClientStat& c : gClients)
    if (c.queries && c.ip == ip) {
      seen = now - c.lastMs < ms;
      break;
    }
  portEXIT_CRITICAL(&gClientsMux);
  return seen;
}

uint32_t DnsServer::clientQueries(uint32_t ip) {
  uint32_t q = 0;
  portENTER_CRITICAL(&gClientsMux);
  for (const ClientStat& c : gClients)
    if (c.queries && c.ip == ip) {
      q = c.queries;
      break;
    }
  portEXIT_CRITICAL(&gClientsMux);
  return q;
}

uint32_t DnsServer::totalClientQueries() {
  uint64_t t = 0;
  portENTER_CRITICAL(&gClientsMux);
  for (const ClientStat& c : gClients) t += c.queries;
  portEXIT_CRITICAL(&gClientsMux);
  return t > UINT32_MAX ? UINT32_MAX : uint32_t(t);
}

void DnsServer::historySnapshot(uint32_t nowHour, siblock::UsageHistory::Slot* out) {
  portENTER_CRITICAL(&gHistoryMux);
  gHistory.snapshot(nowHour, out);
  portEXIT_CRITICAL(&gHistoryMux);
}

size_t DnsServer::topBlocked(siblock::TopBlocked::Entry* out, size_t max) {
  static siblock::TopBlocked copy;  // 2,2 KB: fuera de la pila; solo la usa la tarea del panel
  portENTER_CRITICAL(&gTopMux);
  copy = gTop;
  portEXIT_CRITICAL(&gTopMux);
  return copy.top(out, max);
}

size_t DnsServer::saveHistory(uint8_t* out, size_t max) {
  portENTER_CRITICAL(&gHistoryMux);
  size_t n = gHistory.serialize(out, max);
  portEXIT_CRITICAL(&gHistoryMux);
  return n;
}

bool DnsServer::loadHistory(const uint8_t* in, size_t len) {
  portENTER_CRITICAL(&gHistoryMux);
  bool ok = gHistory.deserialize(in, len);
  portEXIT_CRITICAL(&gHistoryMux);
  return ok;
}

void DnsServer::halveTop() {
  portENTER_CRITICAL(&gTopMux);
  gTop.halve();
  portEXIT_CRITICAL(&gTopMux);
}

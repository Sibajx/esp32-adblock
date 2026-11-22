#include "portal.h"

#include <WiFi.h>

#include "modes.h"
#include "settings.h"

namespace {
DnsServer* gDns = nullptr;
bool gActive = false;
bool gSaved = false;
uint32_t gSavedAt = 0;
uint32_t gApIp = siblock::ipv4(192, 168, 4, 1);  // orden "humano"
constexpr uint32_t kReconnectDelayMs = 2000;

uint32_t hostOrder(IPAddress ip) { return siblock::ipv4(ip[0], ip[1], ip[2], ip[3]); }
IPAddress fromHost(uint32_t a) { return IPAddress(uint8_t(a >> 24), uint8_t(a >> 16), uint8_t(a >> 8), uint8_t(a)); }
}  // namespace

namespace portal {
void begin(DnsServer* dns) { gDns = dns; }

void start() {
  if (gActive) return;
  String name = settings::apName();
  WiFi.mode(WIFI_AP_STA);
  // Mientras el portal está abierto la placa no salta de canal buscando el WiFi guardado (eso
  // desconectaría al teléfono); main.cpp lo reintenta a mano cada 2 min.
  WiFi.setAutoReconnect(false);
  // La red de SiBlock no puede compartir direcciones con la de la casa: si la casa también es
  // 192.168.4.x, un aparato de la casa parecería venir de la red de SiBlock.
  bool sta = WiFi.status() == WL_CONNECTED;
  uint32_t want = siblock::pickApAddress(sta ? hostOrder(WiFi.localIP()) : 0, sta ? hostOrder(WiFi.subnetMask()) : 0);
  IPAddress ip = fromHost(want);
  bool configured = WiFi.softAPConfig(ip, ip, fromHost(siblock::kApMask));
  if (!configured) Serial.println("[portal] no se pudo elegir la dirección de la red de SiBlock");
  if (!WiFi.softAP(name.c_str(), settings::apPassword().c_str())) {  // WPA2: la clave tiene 10
    Serial.println("[portal] no se pudo abrir la red de SiBlock");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    return;
  }
  gActive = true;
  gSaved = false;
  gApIp = configured ? want : hostOrder(WiFi.softAPIP());
  if (gDns) gDns->setCaptive(uint32_t(WiFi.softAPIP()));
  Serial.printf("[portal] abierto: %s\n", name.c_str());  // la contraseña no se imprime aquí
}

void stop() {
  if (!gActive) return;
  gActive = false;
  gSaved = false;
  if (gDns) gDns->setCaptive(0);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.println("[portal] cerrado");
}

bool active() { return gActive; }

String apIpText() { return fromHost(gApIp).toString(); }

bool homeClashesWithAp() {
  if (WiFi.status() != WL_CONNECTED) return false;
  return siblock::subnetsOverlap(hostOrder(WiFi.softAPIP()), siblock::kApMask, hostOrder(WiFi.localIP()),
                                 hostOrder(WiFi.subnetMask()));
}

bool isApClient(IPAddress remote, IPAddress local) {
  if (!gActive || homeClashesWithAp()) return false;
  IPAddress ap = WiFi.softAPIP();
  if (uint32_t(local) != uint32_t(ap)) return false;  // llegó a la IP de la casa, no a la del AP
  return siblock::subnetsOverlap(hostOrder(remote), 0xFFFFFFFFu, hostOrder(ap), siblock::kApMask);
}

void noteWifiSaved(uint32_t nowMs) {
  gSaved = true;
  gSavedAt = nowMs;
}

bool reconnectDue(uint32_t nowMs) {
  if (!gSaved || nowMs - gSavedAt < kReconnectDelayMs) return false;
  gSaved = false;
  return true;
}
}  // namespace portal

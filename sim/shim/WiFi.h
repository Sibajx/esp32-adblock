// Simulador: el "WiFi" es la interfaz local (127.0.0.1/8). El AP también es 127.0.0.1.
//   SIM_WIFI=down       -> sin conexión, salvo después de begin("simulador-ok", ...)
//   SIM_AP_STATIONS=<n> -> aparatos conectados al AP (0 por defecto)
//   SIM_GATEWAY=<ip>    -> puerta de enlace (127.0.0.254 por defecto, así 127.0.0.1 no lo es)
// Sin SIM_WIFI=down la placa está conectada a "CasaDemo"; begin() con otra red que no sea
// "simulador-ok" no cambia nada (como el estado viejo que tarda en cambiar en la placa real).
#pragma once
#include "Arduino.h"
#include "WiFiClient.h"
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
enum wl_status_t { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK };
struct WiFiClass {
  bool mode(wifi_mode_t m) { mode_ = m; return true; }
  wifi_mode_t getMode() const { return mode_; }
  void setHostname(const char*) {}
  void config(IPAddress, IPAddress, IPAddress, IPAddress = {}, IPAddress = {}) {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void begin(const char* ssid, const char* pass);
  void disconnect(bool = false, bool = false) {}
  wl_status_t status();
  void setSleep(bool) {}
  IPAddress localIP() { return IPAddress(127, 0, 0, 1); }
  IPAddress subnetMask() { return IPAddress(255, 0, 0, 0); }
  IPAddress gatewayIP();
  // AP
  bool softAP(const char* ssid, const char* pass = nullptr);
  bool softAPdisconnect(bool = false);
  // softAPConfig se acepta pero el AP sigue siendo 127.0.0.1; la IP pedida sale en la salida.
  bool softAPConfig(IPAddress ip, IPAddress, IPAddress);
  IPAddress softAPIP() { return IPAddress(127, 0, 0, 1); }
  IPAddress softAPSubnetMask() { return IPAddress(255, 255, 255, 0); }
  uint8_t softAPgetStationNum();
  uint8_t* macAddress(uint8_t* mac);
  // Escaneo: dos redes fijas.
  int16_t scanNetworks();
  String SSID(uint8_t i);
  String SSID();  // red conectada ahora ("" sin conexión)
  int32_t RSSI(uint8_t i);
  wifi_auth_mode_t encryptionType(uint8_t i);
  void scanDelete() {}

 private:
  wifi_mode_t mode_ = WIFI_STA;
  bool simOk_ = false;
};
inline WiFiClass WiFi;

// Simulador: AsyncUDP con sockets reales. Todas las llamadas de vuelta corren en un solo hilo
// ("async_udp"), igual que en el ESP32.
#pragma once
#include <functional>

#include "Arduino.h"

class AsyncUDPPacket {
 public:
  AsyncUDPPacket(uint8_t* d, size_t n, IPAddress ip, uint16_t port) : d_(d), n_(n), ip_(ip), port_(port) {}
  uint8_t* data() { return d_; }
  size_t length() { return n_; }
  IPAddress remoteIP() { return ip_; }
  uint16_t remotePort() { return port_; }

 private:
  uint8_t* d_;
  size_t n_;
  IPAddress ip_;
  uint16_t port_;
};

class AsyncUDP {
 public:
  using Handler = std::function<void(AsyncUDPPacket&)>;
  ~AsyncUDP();
  bool listen(uint16_t port);
  void onPacket(Handler h);
  size_t writeTo(const uint8_t* data, size_t len, const IPAddress addr, uint16_t port);
  void dispatch();  // uso interno del simulador

 private:
  int fd_ = -1;
  Handler handler_;
};

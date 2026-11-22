// Simulador: WiFiClient sirve para dos cosas: el cuerpo de una descarga (archivo de curl) y
// conexiones TCP reales (DNS por TCP).
#pragma once
#include <cstdio>
#include <memory>

#include "Arduino.h"

class WiFiClient {
 public:
  WiFiClient() = default;
  explicit WiFiClient(int fd, IPAddress remote);
  // Modo descarga
  FILE* f_ = nullptr;
  // Modo TCP
  bool connect(IPAddress ip, uint16_t port, int32_t timeoutMs = 3000);
  size_t write(const uint8_t* buf, size_t n);
  int available();
  int read(uint8_t* buf, size_t n);
  int readBytes(uint8_t* buf, size_t n);
  bool connected();
  void stop();
  IPAddress remoteIP() const { return remote_; }
  explicit operator bool() const { return sock_ && *sock_ >= 0; }

 private:
  std::shared_ptr<int> sock_;  // compartido como en Arduino (copiar el cliente no lo cierra)
  IPAddress remote_;
};

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port) : port_(port) {}
  void begin();
  WiFiClient available();

 private:
  uint16_t port_;
  int fd_ = -1;
};

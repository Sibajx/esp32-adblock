// Simulador: servidor HTTP mínimo con la API de WebServer de Arduino (una petición a la vez).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

// Igual que el WebServer real: si la ruta tiene función de "upload", el cuerpo (que no sea
// multipart) llega en pedazos de HTTP_RAW_BUFLEN en vez de copiarse entero.
#define HTTP_RAW_BUFLEN 1436
enum HTTPRawStatus { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
struct HTTPRaw {
  HTTPRawStatus status;
  size_t totalSize;
  size_t currentSize;
  uint8_t buf[HTTP_RAW_BUFLEN];
};

struct ShimClient {
  IPAddress ip;
  IPAddress remoteIP() const { return ip; }
  IPAddress localIP() const { return IPAddress(127, 0, 0, 1); }  // el AP del simulador es 127.0.0.1
};

class WebServer {
 public:
  explicit WebServer(int port) : port_(port) {}
  void on(const char* path, HTTPMethod m, std::function<void()> fn) { routes_.push_back({path, m, fn, nullptr}); }
  void on(const char* path, HTTPMethod m, std::function<void()> fn, std::function<void()> ufn) {
    routes_.push_back({path, m, fn, ufn});
  }
  HTTPRaw& raw() { return raw_; }
  String hostHeader() { return header("Host"); }
  ShimClient& client() { return client_; }
  void onNotFound(std::function<void()> fn) { notFound_ = fn; }
  void collectHeaders(const char** names, size_t n) { (void)names; (void)n; }
  void begin();
  void handleClient();
  void send(int code, const char* type, const String& body);
  void send_P(int code, const char* type, const char* body) { send(code, type, String(body)); }
  void sendHeader(const char* name, const String& value) { extra_ += std::string(name) + ": " + value.c_str() + "\r\n"; }
  String arg(const char* name);
  bool hasArg(const char* name) { return args_.count(name) > 0; }
  String header(const char* name);
  bool authenticate(const char* user, const char* pass);

 private:
  struct Route { std::string path; HTTPMethod method; std::function<void()> fn, ufn; };
  HTTPRaw raw_{};
  ShimClient client_;
  int port_, listenFd_ = -1, clientFd_ = -1;
  std::vector<Route> routes_;
  std::function<void()> notFound_;
  std::map<std::string, std::string> args_, headers_;
  std::string extra_;
  bool sent_ = false;
};

// Simulador: HTTPClient con curl (que también verifica HTTPS con los certificados del sistema).
// Variables de entorno para probar fallos: SIM_HTTP_FAIL=1 hace que toda descarga falle.
// Nube: si existe SIM_CLOUD_URL (p. ej. http://127.0.0.1:9000), una URL que empieza con
// https://siblock.s-dev.win se manda a ese prefijo (el firmware no sabe del simulador) y no le
// afecta SIM_HTTP_FAIL. Sin SIM_CLOUD_URL, el simulador nunca llama al siblock.s-dev.win de verdad.
// Con SIM_CLOUD_TLS_FILTRO la nube no contesta y el cliente queda con el error de certificado.
// Cada petición a la nube se anota en el registro: "[sim] nube <MÉTODO> <ruta>".
// Telegram: igual con SIM_TELEGRAM_URL para https://api.telegram.org; sin ella nunca sale. Se anota
// "[sim] telegram <MÉTODO> /bot<oculto>/<método>" (el token de la ruta no va al registro).
#pragma once
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Arduino.h"

#define HTTP_CODE_OK 200
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS = 0, HTTPC_STRICT_FOLLOW_REDIRECTS = 1 };

#include "WiFiClient.h"

// SIM_CLOUD_TLS_FILTRO=1: la nube falla como detrás de un FortiGate (certificado que no es el de
// verdad, MBEDTLS_ERR_X509_CERT_VERIFY_FAILED). Las demás peticiones no cambian.
class WiFiClientSecure : public WiFiClient {
 public:
  void setCACert(const char*) {}
  int lastError(char* buf, size_t size) {
    if (buf && size) buf[0] = 0;
    return lastError_;
  }
  int lastError_ = 0;
};

class HTTPClient {
 public:
  ~HTTPClient() { end(); }
  void useHTTP10(bool) {}
  void setTimeout(uint16_t) {}
  void setConnectTimeout(int32_t) {}
  void setReuse(bool) {}
  void setFollowRedirects(followRedirects_t f) { follow_ = f != HTTPC_DISABLE_FOLLOW_REDIRECTS; }
  void setUserAgent(const String& ua) { userAgent_ = ua.str(); }
  void addHeader(const String& name, const String& value) { headers_.emplace_back(name.str(), value.str()); }
  bool begin(WiFiClient& c, const char* url);
  bool begin(WiFiClient& c, const String& url) { return begin(c, url.c_str()); }
  int GET();
  int POST(const String& body);
  static String errorToString(int code) { return code < 0 ? "error de conexión" : ""; }
  WiFiClient* getStreamPtr() { return client_; }
  int getSize() { return size_; }
  bool connected();
  void end();

 private:
  WiFiClient* client_ = nullptr;
  int run(const char* method, const std::string* body);
  std::string url_, tmp_, userAgent_ = "ESP32HTTPClient";
  std::vector<std::pair<std::string, std::string>> headers_;
  bool follow_ = false, cloud_ = false, blocked_ = false, telegram_ = false;
  int size_ = -1;
};

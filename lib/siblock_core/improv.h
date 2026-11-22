// SiBlock: Improv Serial (https://www.improv-wifi.com/serial/), el protocolo con el que
// ESP Web Tools le da el WiFi a la placa por USB. Sin dependencias de Arduino.
//
// Paquete: "IMPROV" + versión 0x01 + tipo + largo + datos + checksum (suma de todos los bytes
// anteriores, mod 256). Al enviar se agrega '\n'. Datos de RPC: id, largo y textos con prefijo
// de largo.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace siblock::improv {
enum : uint8_t { kState = 1, kError = 2, kRpc = 3, kRpcResult = 4 };
enum : uint8_t { kReady = 2, kProvisioning = 3, kProvisioned = 4 };
enum : uint8_t { kErrNone = 0, kErrInvalidRpc = 1, kErrUnknownCmd = 2, kErrNoConnect = 3, kErrUnknown = 0xFF };
enum : uint8_t { kCmdWifi = 1, kCmdState = 2, kCmdInfo = 3, kCmdScan = 4, kCmdBackupAp = 0xA0 };

struct Packet {
  uint8_t type = 0;
  uint8_t len = 0;
  uint8_t data[255];
};

// Lee paquetes byte por byte e ignora todo lo demás (líneas STATS, mensajes). Si un paquete
// llega cortado, se vuelve a buscar la cabecera dentro de lo ya recibido, sin perder un paquete
// que haya empezado ahí.
class Parser {
 public:
  enum Status { kNone, kPacket, kBadChecksum };
  Status feed(uint8_t byte, Packet& out);

 private:
  static constexpr size_t kMax = 6 + 3 + 255 + 1;
  uint8_t buf_[kMax];
  size_t n_ = 0;
  void dropFront(size_t k);
  void resync();  // tira bytes hasta que lo que queda pueda ser el inicio de una cabecera
};

struct Command {
  uint8_t id = 0;
  std::vector<std::string> args;
};
bool parseRpc(const Packet& p, Command& out);  // false si los largos no cuadran
std::vector<uint8_t> buildPacket(uint8_t type, const uint8_t* data, uint8_t len);
std::vector<uint8_t> buildState(uint8_t state);
std::vector<uint8_t> buildError(uint8_t err);
// Vacío si los datos no caben en 255 bytes.
std::vector<uint8_t> buildRpcResult(uint8_t cmd, const std::vector<std::string>& strings);

// Prueba de un WiFi nuevo. El estado del WiFi cambia por eventos: justo después de
// WiFi.begin() puede seguir diciendo "conectado" a la red vieja. Por eso solo cuenta como
// conectado si pasó un rato (kTrySettleMs) y la red conectada es la que se está probando.
enum class TryResult { kWaiting, kConnected, kFailed };
constexpr uint32_t kTrySettleMs = 500, kTryTimeoutMs = 20000;
TryResult evalTry(uint32_t elapsedMs, bool connected, bool sameSsid);
}  // namespace siblock::improv

// Cliente HTTPS hacia siblock.s-dev.win. Quien llama decide si se puede salir a internet (las
// opciones del panel, apagadas de fábrica); esto solo hace la petición.
#pragma once
#include <string>

// Este archivo y lib/siblock_core/cloud.h se llaman igual: desde src/ "cloud.h" es siempre este,
// así que la parte sin Arduino se trae por ruta. En PlatformIO, relativa a src/; en el
// simulador, src/ se copia a sim/build/src/.
#if __has_include("../lib/siblock_core/cloud.h")
#include "../lib/siblock_core/cloud.h"
#else
#include "../../../lib/siblock_core/cloud.h"
#endif
#include "community.h"

namespace cloud {
// kRejected: el servidor contestó 3xx, 4xx (salvo 429) o 5xx; kLimit: 429.
enum class Result : uint8_t { kOk, kNoNet, kRejected, kLimit };
// POST de JSON a SIBLOCK_CLOUD_BASE + path. 8 s de tope, User-Agent "SiBlock/<versión>",
// sin seguir redirecciones. No hace nada si no hay WiFi o RAM seguida para TLS (kNoNet).
Result postJson(const char* path, const std::string& body);
// GET de SIBLOCK_CLOUD_BASE/version.json (no manda nada nuestro). Lee como mucho 1024 B.
bool fetchLatestVersion(std::string& out);
// GET de SIBLOCK_CLOUD_BASE/api/v1/permitidos (no manda nada nuestro). Pasa el cuerpo de a pedazos
// al parser, sin guardarlo entero, y corta pasado siblock::kCommunityMaxBody. true si llegó entero
// con 200 (falta parser.finish() para saber si es válido).
bool fetchCommunity(siblock::CommunityParser& parser);

// Estado en RAM (se pierde al reiniciar; nada de esto va a la flash). Las opciones son una copia
// de lo guardado en NVS: main.cpp las carga al arrancar y web.cpp las cambia tras guardarlas.
enum class Ping : uint8_t { kNever, kOk, kFail };
struct Status {
  bool share = false;         // "Compartir datos anónimos"
  bool versionCheck = false;  // "Avisarme de versiones nuevas"
  bool community = false;     // "Usar los permitidos de la comunidad"
  Ping lastPing = Ping::kNever;
  std::string latest;         // última versión publicada que se vio ("" = todavía no)
  // La última conexión a siblock.s-dev.win falló porque el certificado no era el de verdad (con la
  // hora ya puesta): un filtro de la red (FortiGate, antivirus del router) se pone en medio. Se
  // apaga con la siguiente conexión que sí llega.
  bool filtered = false;
};
Status& status();
const char* pingName(Ping p);  // "never" | "ok" | "fail"
}  // namespace cloud

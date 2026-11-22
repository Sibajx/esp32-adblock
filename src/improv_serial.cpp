#include "improv_serial.h"

#include <Arduino.h>
#include <WiFi.h>

#include <string>
#include <vector>

#include "config.h"
#include "improv.h"
#include "settings.h"
#include "siblock_text.h"

namespace imp = siblock::improv;

namespace {
siblock::PortalPolicy* gPolicy = nullptr;
imp::Parser gParser;

// WiFi nuevo a prueba: se guarda solo si conecta.
bool gTrying = false;
uint32_t gTryStart = 0;
String gTrySsid, gTryPass;

void send(const std::vector<uint8_t>& p) {
  if (!p.empty()) Serial.write(p.data(), p.size());
}

String panelUrl() { return "http://" + WiFi.localIP().toString() + "/"; }

void restoreSavedWifi() {
  WiFi.disconnect();
  if (settings::hasWifi()) {
    String ssid = settings::wifiSsid(), pass = settings::wifiPass();
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
}

void startTry(const std::string& ssid, const std::string& pass) {
  gTrySsid = String(ssid.c_str());
  gTryPass = String(pass.c_str());
  gTrying = true;
  gTryStart = millis();
  send(imp::buildState(imp::kProvisioning));
  WiFi.disconnect();
  WiFi.begin(gTrySsid.c_str(), gTryPass.c_str());
}

void finishTry(uint32_t now) {
  if (!gTrying) return;
  // Justo después de WiFi.begin() el estado puede seguir diciendo "conectado" a la red vieja:
  // solo vale si ya pasó un rato y la red conectada es la que se está probando.
  imp::TryResult r = imp::evalTry(now - gTryStart, WiFi.status() == WL_CONNECTED, WiFi.SSID() == gTrySsid);
  if (r == imp::TryResult::kConnected) {
    gTrying = false;
    if (!settings::setWifi(gTrySsid, gTryPass)) {
      send(imp::buildError(imp::kErrUnknown));
    } else {
      Serial.printf("[improv] WiFi guardado: \"%s\"\n", gTrySsid.c_str());
      send(imp::buildState(imp::kProvisioned));
      send(imp::buildRpcResult(imp::kCmdWifi, {panelUrl().c_str()}));
    }
    gTryPass = String();
  } else if (r == imp::TryResult::kFailed) {
    gTrying = false;
    gTryPass = String();
    send(imp::buildError(imp::kErrNoConnect));
    restoreSavedWifi();  // los datos malos no se guardan
  }
}

void handle(const imp::Command& c) {
  switch (c.id) {
    case imp::kCmdWifi: {
      if (c.args.size() != 2 || !siblock::validWifi(c.args[0], c.args[1])) {
        send(imp::buildError(imp::kErrInvalidRpc));
        return;
      }
      if (gTrying) {  // uno a la vez
        send(imp::buildError(imp::kErrUnknown));
        return;
      }
      startTry(c.args[0], c.args[1]);
      return;
    }
    case imp::kCmdState:
      if (WiFi.status() == WL_CONNECTED && !gTrying) {
        send(imp::buildState(imp::kProvisioned));
        send(imp::buildRpcResult(imp::kCmdState, {panelUrl().c_str()}));
      } else {
        send(imp::buildState(gTrying ? imp::kProvisioning : imp::kReady));
      }
      return;
    case imp::kCmdInfo:
      send(imp::buildRpcResult(imp::kCmdInfo, {"SiBlock", SIBLOCK_VERSION, "ESP32", settings::apName().c_str()}));
      return;
    case imp::kCmdScan: {
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n && i < 30; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // redes ocultas
        send(imp::buildRpcResult(imp::kCmdScan, {ssid.c_str(), std::to_string(WiFi.RSSI(i)),
                                                 WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"}));
      }
      WiFi.scanDelete();
      send(imp::buildRpcResult(imp::kCmdScan, {}));
      return;
    }
    case imp::kCmdBackupAp:
      send(imp::buildRpcResult(imp::kCmdBackupAp, {settings::apName().c_str(), settings::apPassword().c_str()}));
      return;
    default:
      send(imp::buildError(imp::kErrUnknownCmd));
  }
}
}  // namespace

namespace improv_serial {
void begin(siblock::PortalPolicy* policy) { gPolicy = policy; }

bool provisioning() { return gTrying; }

void loop() {
  uint32_t now = millis();
  // Un tope por vuelta para no quedarse aquí si llega basura sin fin.
  for (int budget = 512; budget > 0 && Serial.available() > 0; budget--) {
    int b = Serial.read();
    if (b < 0) break;
    imp::Packet p;
    imp::Parser::Status st = gParser.feed(uint8_t(b), p);
    if (st == imp::Parser::kNone) continue;
    if (gPolicy) gPolicy->improvSeen(now);
    if (st == imp::Parser::kBadChecksum) {
      send(imp::buildError(imp::kErrInvalidRpc));
      continue;
    }
    imp::Command c;
    if (p.type != imp::kRpc) continue;  // solo se atienden comandos
    if (!imp::parseRpc(p, c)) {
      send(imp::buildError(imp::kErrInvalidRpc));
      continue;
    }
    handle(c);
  }
  finishTry(millis());  // no `now`: startTry pudo tomar gTryStart después y la resta daría la vuelta
}
}  // namespace improv_serial

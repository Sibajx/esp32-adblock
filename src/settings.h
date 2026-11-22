// Configuración guardada en la placa (NVS, espacio "siblock"): WiFi de la casa, contraseña del
// panel (solo su hash PBKDF2, nunca en texto) y contraseña del modo de configuración (AP).
#pragma once
#include <Arduino.h>

namespace settings {
constexpr uint32_t kPanelIterations = 10000;  // medir en la placa (fase 4): ~0,3-0,5 s
void begin();                                 // abre el espacio "siblock"
bool hasWifi();
String wifiSsid();
String wifiPass();
bool setWifi(const String& ssid, const String& pass);  // valida con validWifi
// Exige panel_salt, panel_iter y panel_hash: si un corte de luz dejó la sal sin hash, no hay
// contraseña (y se puede crear otra vez dentro de la ventana).
bool hasPanelPassword();
// Valida; sal de 16 B al azar; borra el hash viejo y escribe sal, iteraciones y al final el hash.
bool setPanelPassword(const String& pw);
bool checkPanelPassword(const String& pw);  // PBKDF2 con la sal e iteraciones guardadas
String apPassword();                         // la genera y la guarda si falta
String apName();                             // "SiBlock-XXXX" (4 últimos hex de la MAC)
// Solo llena lo que falta con WIFI_SSID/WIFI_PASSWORD/WEB_PASSWORD de secrets.h (si existe y
// los valores son válidos). Para quien compila su propio firmware.
void seedFromSecrets();
// Opción de Private Relay (NVS relay_block). Si falta o no se puede leer: apagada.
bool relayBlock();
bool setRelayBlock(bool on);  // false si no se pudo guardar
// Listas elegidas (NVS lists) y las de la lista guardada (lists_built). Inválido o faltante:
// solo la general (siblock::kListsDefault).
uint8_t lists();
bool setLists(uint8_t mask);  // false si no es válida o no se pudo guardar
uint8_t listsBuilt();
bool setListsBuilt(uint8_t mask);
// Nube (todo apagado de fábrica). Número de instalación: lo crea (esp_random) si falta o no es
// válido; si no se puede guardar, vale el mismo hasta que se apague.
String installId();
bool cloudShare();  // "Compartir datos anónimos": solo el valor 1 guardado cuenta como encendido
bool setCloudShare(bool on);  // false si no se pudo guardar
bool versionCheck();  // "Avisarme de versiones nuevas": solo el valor 1 cuenta
bool setVersionCheck(bool on);
bool community();  // "Usar los permitidos de la comunidad": solo el valor 1 cuenta
bool setCommunity(bool on);
void factoryReset();  // borra el espacio "siblock" entero (no toca la lista ni las reglas)
}  // namespace settings

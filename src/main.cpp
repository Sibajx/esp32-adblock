// SiBlock: bloqueador de anuncios para toda la casa en un ESP32 (servidor DNS que bloquea
// anuncios y rastreadores).
#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_random.h>
#include <lwip/dns.h>
#include <time.h>

#include <atomic>

#include "fs_util.h"
#include "blocklist.h"
#include "button.h"
#include "cloud.h"
#include "community_store.h"
#include "config.h"
#include "dns_server.h"
#include "family_store.h"
#include "improv_serial.h"
#include "lists.h"
#include "modes.h"
#include "portal.h"
#include "settings.h"
#include "siblock_crypto.h"
#include "telegram.h"
#include "web.h"

// secrets.h es opcional: si existe, settings::seedFromSecrets() lo usa como valores de fábrica.

static BlockList blocklist;
static DnsServer dns;
static volatile bool updateRequested = false;
// Ventana de 30 min para crear la contraseña del panel: al arrancar y tras una pulsación corta de BOOT.
static siblock::SetupWindow setupWindow;
// Cuándo abrir el modo de configuración por WiFi (AP + portal).
static siblock::PortalPolicy portalPolicy;
static uint32_t lastStatsPrint = 0;
static uint32_t lastRefresh = 0;
// Si una actualización falla se reintenta a los 1, 5 y 30 min (y luego cada 30 min).
static const uint32_t kRetryMs[] = {60000UL, 5 * 60000UL, 30 * 60000UL};
static uint8_t failedTries = 0;
static uint32_t lastTry = 0;
static uint32_t retryDelayMs() { return kRetryMs[failedTries < 3 ? failedTries - 1 : 2]; }
static uint8_t lowMemTries = 0;  // actualizaciones saltadas por falta de RAM seguida
// La primera bajada al arrancar sin lista la pide la placa sola (no la persona): cuenta como automática.
static bool bootUpdate = false;

// WiFi: el arranque no espera a que conecte (así Improv por USB y el modo de configuración
// funcionan sin WiFi). Si se cae y la reconexión automática no lo arregla, se fuerza a los 2 min.
static uint32_t wifiLostMs = 0;
static bool wifiLost = true, wifiKicked = false, wifiEverUp = false, mdnsUp = false;

// Autoprueba del puerto 53 cada 5 min. Solo reinicia si alguna vez funcionó: así, si la
// placa no entrega paquetes a su propia IP, nunca reinicia por error.
static constexpr uint32_t kProbeEveryMs = 5 * 60000UL;
static uint32_t lastProbe = 0, probeSeenBefore = 0;
static bool probePending = false, probeEverOk = false;
static uint8_t probeFails = 0;

// Historia de 168 h del panel: se guarda cada 60 min en /history.bin (vía /history.tmp y un
// rename, así un corte de luz deja la vieja o la nueva). Al reiniciar se pierde como mucho 1 h.
// El top de bloqueados vive solo en RAM y se parte a la mitad cada 24 h.
static constexpr uint32_t kHistorySaveMs = 60 * 60000UL;
static constexpr uint32_t kTopHalveMs = 24 * 3600000UL;
static constexpr const char* kHistoryFile = "/history.bin";
static constexpr const char* kHistoryTmp = "/history.tmp";
static uint32_t lastHistorySave = 0, lastTopHalve = 0;

// Nube (apagada de fábrica). El aviso diario ("compartir datos anónimos") y la consulta de
// versión ("avisarme de versiones nuevas") tienen cada uno su calendario: el primero sale al
// minuto de encender la opción (o de arrancar con ella encendida) y luego cada 24 h (+ hasta 1 h
// al azar, para que no lleguen todas las placas a la vez); si falla, a la 1, 2, 4... h.
// «Usar los permitidos de la comunidad» tiene el mismo calendario.
static siblock::CloudSchedule pingSched, verSched, comSched;
// Foto de la historia de 168 h (1,3 KB fuera de la pila) para el aviso diario y el resumen semanal:
// los dos corren en loop(), uno después del otro.
static siblock::UsageHistory::Slot gSlots[siblock::UsageHistory::kHours];
static bool pingOn = false, verOn = false, comOn = false;

static void watchCloud(uint32_t now) {
  cloud::Status& st = cloud::status();
  if (st.share != pingOn) {
    pingOn = st.share;
    if (pingOn) pingSched.start(now);
  }
  if (st.versionCheck != verOn) {
    verOn = st.versionCheck;
    if (verOn) verSched.start(now);
  }
  if (st.community != comOn) {
    comOn = st.community;
    if (comOn) comSched.start(now);  // al apagarla, el panel ya borró la lista (community::clear)
  }
  // Nada sale mientras se baja la lista (la RAM para TLS es la misma) ni sin WiFi.
  if (updateRequested || blocklist.updating() || WiFi.status() != WL_CONNECTED) return;
  if (pingOn && pingSched.due(now)) {
    uint32_t hour = siblock::epochHour(int64_t(time(nullptr)));
    if (!hour) return;  // sin hora no se sabe qué es "hoy": se espera a NTP
    dns.historySnapshot(hour, gSlots);
    uint32_t blocked = siblock::sumBlocked(gSlots, 24);
    if (blocked > siblock::kMaxBlockedPerDay) blocked = siblock::kMaxBlockedPerDay;
    std::string body = siblock::pingJson(settings::installId().c_str(), SIBLOCK_VERSION, blocked);
    bool ok = cloud::postJson("/api/v1/placa", body) == cloud::Result::kOk;
    st.lastPing = ok ? cloud::Ping::kOk : cloud::Ping::kFail;
    Serial.printf("[nube] aviso diario: %s\n", ok ? "ok" : "falló");
    pingSched.done(ok, millis(), esp_random() % 3600000UL);
    return;  // uno por vuelta
  }
  if (verOn && verSched.due(now)) {
    std::string latest;
    bool ok = cloud::fetchLatestVersion(latest);
    if (ok) st.latest = latest;
    Serial.printf("[nube] versión publicada: %s\n", ok ? latest.c_str() : "no se pudo leer");
    verSched.done(ok, millis(), esp_random() % 3600000UL);
    return;
  }
  if (comOn && comSched.due(now)) {
    bool ok = community::refresh();
    if (ok) Serial.printf("[nube] permitidos de la comunidad: %u\n", unsigned(community::info().count));
    else Serial.println("[nube] permitidos de la comunidad: no se pudo leer");
    comSched.done(ok, millis(), esp_random() % 3600000UL);
  }
}

// Avisos por Telegram (apagados de fábrica). Cada aviso espera su turno en telegram::status().pending
// y sale desde aquí, uno por vuelta, con WiFi, con hora de internet (el certificado la necesita) y
// nunca mientras se baja la lista. Topes: 20 por hora entre todos (tgLimiter) y uno de prueba cada
// 30 s (lo cuida web.cpp). Si un envío falla por algo pasajero (poca RAM, sin conexión) vuelve a la
// cola y se reintenta al minuto, como mucho 5 veces (tgRetry; cada intento cuenta en los 20 por
// hora). Si falla por otra cosa no se reintenta: queda el resultado en el panel.
static siblock::SendLimiter tgLimiter;
static siblock::TelegramRetry tgRetry;
static siblock::WeeklyOnce tgWeekly;
static siblock::ListFailWatch listWatch;
static siblock::BootCause bootCause = siblock::BootCause::kNormal;
static uint32_t tgFailHours = 0;

static bool clockSet();

static bool telegramWants(uint8_t eventBit) {
  return settings::telegramOn() && (settings::telegramEvents() & eventBit);
}

static void watchTelegram(uint32_t now) {
  telegram::Status& st = telegram::status();
  // Resumen semanal: domingo de 20:00 a 23:59 en la hora de la casa (la de los horarios); una vez.
  static uint32_t lastWeekCheck = 0;
  if (now - lastWeekCheck >= 60000) {
    lastWeekCheck = now;
    if (telegramWants(settings::kTgWeekly)) {
      int32_t day = siblock::weeklySummaryDay(int64_t(time(nullptr)), family::tz());
      // La RAM (tgWeekly) se acuerda del día aunque la NVS no deje guardarlo: si no, saldría cada minuto.
      if (tgWeekly.take(day, settings::telegramWeek())) {
        if (!settings::setTelegramWeek(uint32_t(day))) Serial.println("[telegram] no se pudo guardar el día del resumen");
        st.pending |= telegram::kWeekly;
      }
    }
  }
  if (!st.pending || updateRequested || blocklist.updating() || WiFi.status() != WL_CONNECTED || !clockSet() ||
      tgRetry.waiting(now))
    return;
  uint8_t bit = 0;
  for (uint8_t b : {telegram::kTest, telegram::kBoot, telegram::kListFail, telegram::kListOk, telegram::kWeekly})
    if (st.pending & b) {
      bit = b;
      break;
    }
  st.pending &= uint8_t(~bit);
  std::string text;
  uint32_t list = uint32_t(blocklist.count());
  switch (bit) {
    case telegram::kTest: text = siblock::testMessage(); break;
    case telegram::kBoot:
      if (telegramWants(settings::kTgReboot)) text = siblock::bootMessage(bootCause, list);
      break;
    case telegram::kListFail:
      if (telegramWants(settings::kTgListFail)) text = siblock::listFailMessage(tgFailHours, list);
      break;
    case telegram::kListOk:
      if (telegramWants(settings::kTgListFail)) text = siblock::listOkMessage(list);
      break;
    default:
      if (telegramWants(settings::kTgWeekly)) {
        uint32_t hour = siblock::epochHour(int64_t(time(nullptr)));
        dns.historySnapshot(hour, gSlots);
        text = siblock::weeklyMessage(siblock::sumBlocked(gSlots, siblock::UsageHistory::kHours));
      }
      break;
  }
  if (text.empty()) return;  // se apagó mientras esperaba
  int code;
  if (!tgLimiter.allow(millis())) {
    code = 429;
    st.last = siblock::TelegramResult::kLimit;
  } else {
    code = telegram::send(text);
    st.last = siblock::telegramResultFor(code);
  }
  st.lastCode = code > 0 ? code : 0;
  st.lastAt = clockSet() ? uint32_t(time(nullptr)) : 0;
  bool again = siblock::telegramTransient(st.last) && tgRetry.failed(bit, millis());
  if (again) st.pending |= bit;  // vuelve a la cola: otro intento en un minuto
  else tgRetry.done();
  Serial.printf("[telegram] aviso: %s%s\n", siblock::telegramResultName(st.last), again ? " (se reintenta en 1 min)" : "");
}

// Tras cada intento de bajar la lista: ¿toca avisar que falla o que ya se pudo? scheduled: false si
// lo pidió la persona con «Actualizar ahora» (sus fallas no cuentan para el aviso).
static void noteListResult(bool ok, uint32_t now, bool scheduled) {
  switch (listWatch.onResult(ok, now, telegramWants(settings::kTgListFail), scheduled)) {
    case siblock::ListFailWatch::kFail:
      tgFailHours = listWatch.failingHours(now);
      telegram::status().pending |= telegram::kListFail;
      break;
    case siblock::ListFailWatch::kRecovered:
      telegram::status().pending |= telegram::kListOk;
      break;
    default:
      break;
  }
}

// Al arrancar: el aviso de reinicio (si está encendido) y la versión, para saber si fue una
// actualización. Con los avisos apagados no se escribe nada en la NVS.
static void startTelegram() {
  if (!settings::telegramOn()) return;
  String prev = settings::telegramFirmware();
  bool updated = prev.length() && prev != SIBLOCK_VERSION;
  if (prev != SIBLOCK_VERSION && !settings::setTelegramFirmware(SIBLOCK_VERSION))
    Serial.println("[telegram] no se pudo guardar la versión");
  bootCause = telegram::bootCause(updated);
  if (settings::telegramEvents() & settings::kTgReboot) telegram::status().pending |= telegram::kBoot;
}

static void loadHistory() {
  uint8_t buf[siblock::UsageHistory::kBytes];
  fs::File f = fsu::openRead(kHistoryFile);
  if (!f) return;
  size_t n = f.size() == sizeof buf ? f.read(buf, sizeof buf) : 0;
  f.close();
  Serial.println(dns.loadHistory(buf, n) ? "[uso] historia leída" : "[uso] historia guardada dañada: se ignora");
}

static void saveHistory() {
  if (time(nullptr) < 1700000000) return;  // sin hora la historia no cambia: no se gasta la flash
  static uint8_t buf[siblock::UsageHistory::kBytes];
  size_t n = dns.saveHistory(buf, sizeof buf);
  if (!n) return;
  fs::File f = LittleFS.open(kHistoryTmp, FILE_WRITE);
  bool ok = f && f.write(buf, n) == n;
  if (f) f.close();
  if (ok && !LittleFS.rename(kHistoryTmp, kHistoryFile)) {
    fsu::remove(kHistoryFile);  // por si alguna versión no pisa el destino
    ok = LittleFS.rename(kHistoryTmp, kHistoryFile);
  }
  if (!ok) {
    fsu::remove(kHistoryTmp);
    Serial.println("[uso] no se pudo guardar la historia");
  }
}

static void watchHistory(uint32_t now) {
  if (now - lastHistorySave >= kHistorySaveMs) {
    lastHistorySave = now;
    saveHistory();
  }
  if (now - lastTopHalve >= kTopHalveMs) {
    lastTopHalve = now;
    dns.halveTop();
  }
}

static void startWifi() {
  // El WiFi vive solo en el espacio "siblock" (así "borrar la configuración" lo borra de verdad):
  // el ESP32 no guarda su propia copia.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
#if USE_STATIC_IP
  WiFi.config(STATIC_IP, STATIC_GATEWAY, STATIC_SUBNET, UPSTREAM_DNS_1, UPSTREAM_DNS_2);
#endif
  WiFi.setAutoReconnect(true);
}

static void connectSavedWifi() {
  if (!settings::hasWifi()) {
    Serial.println("[wifi] sin WiFi guardado: dalo desde la página de instalación");
    return;
  }
  String ssid = settings::wifiSsid(), pass = settings::wifiPass();
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("[wifi] conectando a \"%s\"\n", ssid.c_str());
}

// El ESP32 resuelve sus propias descargas con el DNS real, no consigo mismo
// (cuando el router lo reparta como DNS de la casa, el DHCP se lo daría a él también).
static void useUpstreamForOwnDns() {
  ip_addr_t a;
  IPAddress u1 = UPSTREAM_DNS_1, u2 = UPSTREAM_DNS_2;
  IP_ADDR4(&a, u1[0], u1[1], u1[2], u1[3]);
  dns_setserver(0, &a);
  IP_ADDR4(&a, u2[0], u2[1], u2[2], u2[3]);
  dns_setserver(1, &a);
}

// Hora por NTP: la validación de certificados de las descargas la necesita.
static void startClock() { configTime(0, 0, "pool.ntp.org", "time.cloudflare.com"); }
static bool clockSet() { return time(nullptr) > 1700000000; }

static void onWifiUp() {
  WiFi.setSleep(false);  // sin ahorro de energía: responde más rápido
  useUpstreamForOwnDns();
  String ip = WiFi.localIP().toString();
  if (!wifiEverUp) {
    Serial.printf("[wifi] conectado. IP: %s\n", ip.c_str());
    Serial.printf("[listo] DNS en %s:53 · panel en http://%s/\n", ip.c_str(), ip.c_str());
  } else {
    Serial.printf("[wifi] reconectado. IP: %s\n", ip.c_str());
  }
  wifiEverUp = true;
  if (!mdnsUp && MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
}

static void watchWifi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiLost) onWifiUp();
    wifiLost = wifiKicked = false;
    return;
  }
  if (!wifiLost) {
    wifiLost = true;
    wifiLostMs = now;
    Serial.println("[wifi] se perdió la conexión");
    return;
  }
  // Sin conexión: cada 2 min se reintenta a mano con el WiFi guardado.
  if (settings::hasWifi() && !improv_serial::provisioning() && now - wifiLostMs > 2 * 60000UL) {
    wifiLostMs = now;
    if (!wifiKicked) Serial.println("[wifi] sigue sin conexión: reconectando a mano");
    wifiKicked = true;
    WiFi.disconnect();
    connectSavedWifi();
  }
}

// Botón y LED en su propia tarea (cada 20 ms): así la luz parpadea también mientras loop() está
// ocupado bajando la lista. La acción del botón la atiende loop() (watchButton).
static std::atomic<int> pendingButton{int(siblock::ButtonAction::kNone)};

static siblock::LedState currentLedState() {
  return siblock::ledStateFor(WiFi.status() == WL_CONNECTED, portal::active(), blocklist.updating(), blocklist.count(),
                              dns.upstreamDown());
}

static void buttonTask(void*) {
  while (true) {
    siblock::ButtonAction a = button::poll(millis(), currentLedState());
    if (a != siblock::ButtonAction::kNone) pendingButton = int(a);
    delay(20);
  }
}

static void watchButton(uint32_t now) {
  switch (siblock::ButtonAction(pendingButton.exchange(int(siblock::ButtonAction::kNone)))) {
    case siblock::ButtonAction::kOpenSetupWindow:
      setupWindow.open(now);
      Serial.println("[boot] ventana de 30 min para crear la contraseña");
      break;
    case siblock::ButtonAction::kConfigMode:
      Serial.println("[boot] modo de configuración por WiFi");
      portalPolicy.forceOpen(now);
      break;
    case siblock::ButtonAction::kFactoryReset:
      // La configuración, los aparatos y horarios (/familia.txt), la historia de las gráficas (dice
      // a qué horas hay gente en casa) y los permitidos de la comunidad. /list.bin y /custom.txt
      // (tus reglas) se quedan. LittleFS ya está montado desde setup().
      Serial.println("[boot] borrando la configuración");
      settings::factoryReset();
      family::erase();  // nombres de los aparatos y horarios (son datos de la familia)
      community::clear();
      fsu::remove("/restore.tmp");
      fsu::remove(kHistoryFile);
      fsu::remove(kHistoryTmp);
      WiFi.disconnect(true, true);  // y la copia del WiFi que pudiera tener el propio ESP32
      delay(100);
      ESP.restart();
      break;
    default:
      break;
  }
}

// Modo de configuración por WiFi: lo decide PortalPolicy (sin WiFi 2 min, WiFi caído 10 min,
// BOOT 3-8 s; se cierra al conectar o tras 15 min sin nadie).
static void watchPortal(uint32_t now) {
  if (portal::reconnectDue(now)) {  // el portal guardó un WiFi nuevo
    portalPolicy.forceClose(now);
    portal::stop();
    WiFi.disconnect();
    connectSavedWifi();
    wifiLostMs = now;
    return;
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  int stations = portal::active() ? WiFi.softAPgetStationNum() : 0;
  portalPolicy.tick(now, settings::hasWifi(), connected, stations);
  if (portalPolicy.wantPortal() && !portal::active()) portal::start();
  else if (!portalPolicy.wantPortal() && portal::active()) portal::stop();
}

static void watchDnsPort(uint32_t now) {
  if (probePending && now - lastProbe > 3000) {
    probePending = false;
    if (WiFi.status() != WL_CONNECTED) {
      // sin WiFi la prueba no dice nada; de eso se encarga watchWifi
    } else if (dns.probesSeen() != probeSeenBefore) {
      if (!probeEverOk) Serial.println("[dns] autoprueba del puerto 53: ok");
      probeEverOk = true;
      probeFails = 0;
    } else if (probeEverOk && ++probeFails >= 3) {
      Serial.println("[dns] el puerto 53 dejó de recibir; reinicio");
      delay(100);
      ESP.restart();
    } else if (probeEverOk) {
      Serial.printf("[dns] autoprueba sin respuesta (%u/3)\n", unsigned(probeFails));
    }
  }
  if (!probePending && now - lastProbe > kProbeEveryMs && WiFi.status() == WL_CONNECTED) {
    lastProbe = now;
    probeSeenBefore = dns.probesSeen();
    probePending = true;
    dns.sendProbe();
  }
}

void setup() {
  Serial.setRxBufferSize(512);  // un paquete de Improv entero cabe (265 B)
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== SiBlock " SIBLOCK_VERSION " ===");

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] no se pudo montar LittleFS");
  }
  // La radio primero: esp_random() solo es de verdad al azar con ella encendida, y la
  // contraseña del AP y la sal de la contraseña del panel salen de ahí.
  startWifi();
  settings::begin();
  settings::seedFromSecrets();
  Serial.println(siblock::pbkdf2SelfTest() ? "[clave] autoprueba ok" : "[clave] AUTOPRUEBA FALLÓ");
  // Quien tiene el cable USB ya puede borrar todo con BOOT: mostrarla aquí no abre nada nuevo.
  Serial.printf("[ap] respaldo: %s / %s\n", settings::apName().c_str(), settings::apPassword().c_str());
  connectSavedWifi();
  wifiLostMs = millis();
  startClock();

  dns.setRelayBlock(settings::relayBlock());  // apagada de fábrica
  cloud::status().share = settings::cloudShare();  // las tres apagadas de fábrica
  cloud::status().versionCheck = settings::versionCheck();
  cloud::status().community = settings::community();
  startTelegram();
  loadHistory();
  lastHistorySave = lastTopHalve = millis();
  bool haveList = blocklist.begin();
  Serial.printf("[lista] guardada: %u dominios\n", unsigned(blocklist.count()));
  community::begin(&blocklist);  // después de blocklist.begin() (crea el candado)
  family::begin();  // nombres y horarios (después de startWifi: la sal nueva sale de esp_random)

  if (!dns.begin(&blocklist, UPSTREAM_DNS_1, UPSTREAM_DNS_2)) {
    Serial.println("[dns] no se pudo abrir el puerto 53; reinicio");
    delay(3000);
    ESP.restart();
  }
  setupWindow.open(millis());
  web::begin(&blocklist, &dns, &updateRequested, &setupWindow);

  button::begin();
  if (xTaskCreatePinnedToCore(buttonTask, "boton", 3072, nullptr, 1, nullptr, 1) != pdPASS)
    Serial.println("[boot] no se pudo crear la tarea del botón");
  improv_serial::begin(&portalPolicy);
  portal::begin(&dns);

  // El DNS ya funciona (reenviando todo) mientras baja la primera lista.
  if (!haveList) updateRequested = bootUpdate = true;
  lastRefresh = millis();
  lastProbe = millis() - kProbeEveryMs + 60000;  // la primera autoprueba, al minuto
}

void loop() {
  web::loop();

  uint32_t now = millis();
  setupWindow.isOpen(now);  // la cierra a los 30 min (y no se reabre si millis() da la vuelta)
  watchButton(now);
  improv_serial::loop();
  watchPortal(now);
  watchWifi(now);
  watchDnsPort(now);
  watchHistory(now);
  watchCloud(now);
  watchTelegram(now);
  bool due = failedTries
                 ? now - lastTry > retryDelayMs()
                 : now - lastRefresh > uint32_t(REFRESH_HOURS) * 3600UL * 1000UL;
  if ((updateRequested || due) && WiFi.status() == WL_CONNECTED) {
    // Automática: le tocaba por calendario (o es la primera al arrancar). Si no, la pidió la persona.
    const bool scheduled = due || bootUpdate;
    updateRequested = bootUpdate = false;
    useUpstreamForOwnDns();
    dns.clearCache();  // libera ~32 KB de RAM para la descarga
    // Sin hora no se pueden validar bien los certificados: se espera hasta 10 s a NTP y, si no
    // llega, se intenta igual (si el TLS la exige, fallará y se reintenta más tarde).
    for (int i = 0; i < 100 && !clockSet(); i++) delay(100);
    if (!clockSet()) Serial.println("[hora] sin hora de NTP todavía; se intenta igual");
    bool ok;
    // TLS y los búferes de la lista necesitan bloques seguidos de RAM. Si la memoria está muy
    // fragmentada, no se intenta (fallaría a medias); a la tercera vez se reinicia, que la deja
    // limpia en 5 s sin perder la lista guardada.
    if (ESP.getMaxAllocHeap() < 32 * 1024) {
      ok = false;
      blocklist.noteError("poca RAM seguida para bajar la lista");
      Serial.printf("[lista] RAM seguida %u KB: no se baja\n", unsigned(ESP.getMaxAllocHeap() / 1024));
      if (++lowMemTries >= 3) {
        Serial.println("[lista] RAM fragmentada 3 veces seguidas: reinicio");
        delay(100);
        ESP.restart();
      }
    } else {
      lowMemTries = 0;
      // Orden: estafas, español, general (si no cabe todo, lo que queda fuera son anuncios).
      uint8_t lists = settings::lists();
      const char* urls[siblock::kMaxListUrls];
      size_t nUrls = siblock::listUrls(lists, urls, siblock::kMaxListUrls);
      ok = blocklist.update(urls, nUrls, lists != settings::listsBuilt());
      if (ok && lists != settings::listsBuilt() && !settings::setListsBuilt(lists))
        Serial.println("[lista] no se pudo guardar qué listas tiene la lista nueva");
    }
    lastTry = millis();
    noteListResult(ok, lastTry, scheduled);
    if (ok) {
      lastRefresh = lastTry;
      failedTries = 0;
    } else if (failedTries < 255) {
      failedTries++;
      Serial.printf("[lista] reintento en %u min\n", unsigned(retryDelayMs() / 60000));
    }
  }

  // Línea fija para que otra placa (p. ej. el Arduino con una pantallita) la lea por serie.
  if (now - lastStatsPrint > 5000) {
    lastStatsPrint = now;
    DnsStats s = dns.stats();
    Serial.printf("STATS,%u,%u,%u,%u\n", unsigned(s.queries), unsigned(s.blocked),
                  unsigned(blocklist.count()), unsigned(ESP.getFreeHeap()));
  }
  delay(2);
}

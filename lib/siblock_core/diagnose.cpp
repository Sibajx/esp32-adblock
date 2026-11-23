#include "diagnose.h"

#include <algorithm>

#include "dns_names.h"
#include "family.h"

namespace siblock {

void BypassTable::note(uint32_t ip, uint8_t flags, uint32_t nowMs) {
  if (flags == 0) return;
  Entry* free = nullptr;
  Entry* oldest = nullptr;
  for (auto& e : e_) {
    if (alive(e, nowMs)) {
      if (e.ip == ip) {
        e.flags |= flags;
        e.lastMs = nowMs;
        return;
      }
      if (!oldest || nowMs - e.lastMs > nowMs - oldest->lastMs) oldest = &e;
    } else if (!free) {
      free = &e;
    }
  }
  Entry* dst = free ? free : oldest;
  *dst = {ip, flags, nowMs};
}

uint8_t BypassTable::flagsFor(uint32_t ip, uint32_t nowMs) const {
  for (auto& e : e_)
    if (alive(e, nowMs) && e.ip == ip) return e.flags;
  return 0;
}

size_t BypassTable::list(Entry* out, size_t max, uint32_t nowMs) const {
  Entry tmp[kSlots];
  size_t n = 0;
  for (auto& e : e_)
    if (alive(e, nowMs)) tmp[n++] = e;
  std::stable_sort(tmp, tmp + n,
                   [nowMs](const Entry& a, const Entry& b) { return nowMs - a.lastMs < nowMs - b.lastMs; });
  size_t k = std::min(n, max);
  for (size_t i = 0; i < k; i++) out[i] = tmp[i];
  return k;
}

ProbeResult parseProbe(const std::string& s) {
  if (s == "ok") return ProbeResult::kOk;
  if (s == "fail") return ProbeResult::kFail;
  return ProbeResult::kSkip;
}

const char* levelName(DiagLevel l) {
  switch (l) {
    case DiagLevel::kOk: return "ok";
    case DiagLevel::kInfo: return "info";
    case DiagLevel::kWarn: return "warn";
    case DiagLevel::kBad: return "bad";
  }
  return "ok";
}

DeviceOs osFromUserAgent(const std::string& ua) {
  if (ua.empty()) return DeviceOs::kNone;
  auto has = [&ua](const char* t) { return ua.find(t) != std::string::npos; };
  if (has("CrOS")) return DeviceOs::kChromeOs;
  if (has("Android")) return DeviceOs::kAndroid;  // antes que Linux: Android también dice "Linux"
  if (has("iPhone") || has("iPad") || has("iPod")) return DeviceOs::kIos;  // dicen "like Mac OS X"
  if (has("Windows")) return DeviceOs::kWindows;
  if (has("Macintosh") || has("Mac OS X")) return DeviceOs::kMac;
  if (has("Linux") || has("X11")) return DeviceOs::kLinux;
  return DeviceOs::kOther;
}

// Pasos según el sistema para un aparato que no usa SiBlock. SiBlock no ve qué programa es (el
// DNS cifrado de Windows o un antivirus no le preguntan nada): solo dice dónde mirar.
static void osSteps(std::vector<DiagFinding>& v, const DiagInput& in) {
  switch (in.os) {
    case DeviceOs::kNone:
      return;
    case DeviceOs::kWindows:
      v.push_back({"windows_dns_cifrado", DiagLevel::kInfo});
      v.push_back({"antivirus_dns", DiagLevel::kInfo});
      v.push_back({"vpn", DiagLevel::kInfo});
      return;
    case DeviceOs::kMac:
    case DeviceOs::kIos:
      v.push_back({"apple_perfil_dns", DiagLevel::kInfo});
      v.push_back({"vpn", DiagLevel::kInfo});
      return;
    case DeviceOs::kAndroid:
      if (!(in.bypassFlags & kBypassDoh)) v.push_back({"android_dns_privado", DiagLevel::kInfo});
      v.push_back({"vpn", DiagLevel::kInfo});
      return;
    default:
      v.push_back({"revisar_aparato", DiagLevel::kInfo});
      return;
  }
}

std::vector<DiagFinding> diagnose(const DiagInput& in) {
  std::vector<DiagFinding> v;
  v.reserve(16);
  if (in.probe == ProbeResult::kOk) v.push_back({"usa_siblock", DiagLevel::kOk});
  if (in.probe == ProbeResult::kFail) {
    // En los dos casos va el consejo de IPv6: un aparato que alterna entre SiBlock y el DNS por
    // IPv6 del módem también pregunta a SiBlock y aun así la imagen puede fallar.
    v.push_back(in.clientSeen ? DiagFinding{"navegador_dns_propio", DiagLevel::kWarn}
                              : DiagFinding{"no_usa_siblock", DiagLevel::kBad});
    v.push_back({"ipv6", DiagLevel::kInfo});
  }
  if (in.probe == ProbeResult::kSkip && !in.clientSeen) v.push_back({"sin_consultas", DiagLevel::kWarn});
  if (in.bypassFlags & kBypassDoh) v.push_back({"dns_cifrado", DiagLevel::kWarn});
  if ((in.bypassFlags & kBypassRelay) && !in.relayBlockOn) v.push_back({"private_relay", DiagLevel::kWarn});
  if (in.paused) v.push_back({"en_pausa", DiagLevel::kWarn});
  if (in.devicePaused) v.push_back({"aparato_en_pausa", DiagLevel::kWarn});
  // "Todo internet" explica que nada abra; redes sociales o juegos solo explican esos sitios.
  if (in.scheduleGroups & kGroupAll) v.push_back({"horario_todo", DiagLevel::kWarn});
  else if (in.scheduleGroups) v.push_back({"horario_activo", DiagLevel::kInfo});
  if (!in.listLoaded) v.push_back({"sin_lista", DiagLevel::kBad});
  if (in.upstreamDown) v.push_back({"sin_internet", DiagLevel::kBad});
  if (in.viaRouter) v.push_back({"router_dns", DiagLevel::kWarn});
  if (in.probe == ProbeResult::kFail || (in.probe == ProbeResult::kSkip && !in.clientSeen)) osSteps(v, in);
  bool trouble = false;
  for (auto& f : v) trouble = trouble || f.level >= DiagLevel::kWarn;
  if (!trouble) v.push_back({"todo_bien", DiagLevel::kOk});
  std::stable_sort(v.begin(), v.end(),
                   [](const DiagFinding& a, const DiagFinding& b) { return a.level > b.level; });
  return v;
}

bool routerLooksLikeDns(uint32_t gatewayIp, uint32_t gatewayQueries, uint32_t totalQueries) {
  if (gatewayIp == 0 || totalQueries < 50) return false;
  return uint64_t(gatewayQueries) * 10 >= uint64_t(totalQueries) * 8;
}

bool upstreamLooksDown(uint32_t nowMs, uint32_t lastAnsweredMs, uint32_t lastTimeoutMs,
                       uint32_t timeoutsSinceAnswer) {
  if (timeoutsSinceAnswer < kUpstreamDownTimeouts) return false;
  uint32_t timeoutAge = nowMs - lastTimeoutMs;
  uint32_t answeredAge = nowMs - lastAnsweredMs;
  return timeoutAge < answeredAge && timeoutAge < 300000UL;
}

}  // namespace siblock

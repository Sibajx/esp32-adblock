#include "backup.h"

#include <cstdio>
#include <cstring>

#include "../adblock_core/adblock_core.h"
#include "lists.h"
#include "siblock_crypto.h"

namespace siblock {

namespace {
const char kHeadFull[] = "SIBLOCK-RESPALDO 1";
const char kHeadFamily[] = "SIBLOCK-FAMILIA 1";

// Entero decimal sin signo, solo dígitos (1 a 9), <= max.
bool parseUint(const std::string& t, uint32_t max, uint32_t& out) {
  if (t.empty() || t.size() > 9) return false;
  uint32_t v = 0;
  for (char c : t) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + uint32_t(c - '0');
  }
  if (v > max) return false;
  out = v;
  return true;
}

bool parseInt(const std::string& t, int32_t min, int32_t max, int32_t& out) {
  bool neg = !t.empty() && t[0] == '-';
  uint32_t v;
  if (!parseUint(neg ? t.substr(1) : t, 0x7FFFFFFF, v)) return false;
  int32_t s = neg ? -int32_t(v) : int32_t(v);
  if (s < min || s > max) return false;
  out = s;
  return true;
}

// Parte por espacios simples; un espacio doble deja un pedazo vacío (y luego falla).
std::vector<std::string> split(const std::string& l, size_t maxParts) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (out.size() + 1 < maxParts) {
    size_t sp = l.find(' ', pos);
    if (sp == std::string::npos) break;
    out.push_back(l.substr(pos, sp - pos));
    pos = sp + 1;
  }
  out.push_back(l.substr(pos));
  return out;
}

void appendLine(std::string& out, const char* word, const std::string& rest) {
  out += word;
  out += ' ';
  out += rest;
  out += '\n';
}

void writeFamilyBody(std::string& out, const Family& f) {
  for (size_t i = 0; i < kMaxDevices; i++) {
    if (!f.used(int(i))) continue;
    appendLine(out, "aparato", std::to_string(i) + " " + deviceIdHex(f.idAt(int(i))) + " " + f.nameAt(int(i)));
  }
  for (size_t i = 0; i < f.scheduleCount(); i++) {
    const Schedule& s = f.scheduleAt(i);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%08x %u %u %u %u", unsigned(s.devices), unsigned(s.groups), unsigned(s.days),
                  unsigned(s.start), unsigned(s.end));
    appendLine(out, "horario", buf);
  }
  out += "fin\n";
}

void writeHead(std::string& out, const Family& f) {
  appendLine(out, "zona", std::to_string(f.tz));
  appendLine(out, "sal", toHex(f.salt, sizeof f.salt));
}
}  // namespace

BackupReader::BackupReader(bool full) { b_.full = full; }

bool BackupReader::fail(const char* msg) {
  failed_ = true;
  errLine_ = lineNo_;
  if (lineNo_ > 0) err_ = "línea " + std::to_string(lineNo_) + ": " + msg;
  else err_ = msg;
  return false;
}

bool BackupReader::failTooBig() {
  if (failed_) return false;
  lineNo_ = 0;
  return fail("el archivo es demasiado grande");
}

bool BackupReader::line(const char* p, size_t n) {
  if (failed_) return false;
  lineNo_++;
  bytes_ += n + 1;
  if (bytes_ > kBackupMaxBytes) return fail("el archivo es demasiado grande");
  if (n > 0 && p[n - 1] == '\r') n--;
  if (n > kBackupMaxLine) return fail("la línea es demasiado larga");
  if (n == 0) return fail("hay una línea vacía");
  if (end_) return fail("hay algo después de \"fin\"");
  return parse(std::string(p, n));
}

bool BackupReader::parse(const std::string& l) {
  if (!header_) {
    if (l != (b_.full ? kHeadFull : kHeadFamily)) return fail("no es un respaldo de SiBlock (o es de otra versión)");
    header_ = true;
    return true;
  }
  if (l == "fin") {
    end_ = true;
    return true;
  }
  std::vector<std::string> t = split(l, 4);
  const std::string& w = t[0];
  const char* bad = "no se entiende";
  if (w == "listas" && b_.full) {
    uint32_t v;
    if (t.size() != 2 || !parseUint(t[1], 255, v) || !validListMask(v)) return fail(bad);
    if (lists_) return fail("\"listas\" está repetida");
    lists_ = true;
    b_.lists = uint8_t(v);
  } else if (w == "relay" && b_.full) {
    uint32_t v;
    if (t.size() != 2 || !parseUint(t[1], 1, v)) return fail(bad);
    if (relay_) return fail("\"relay\" está repetida");
    relay_ = true;
    b_.relay = v == 1;
  } else if (w == "zona") {
    int32_t v;
    if (t.size() != 2 || !parseInt(t[1], -720, 840, v)) return fail(bad);
    if (zone_) return fail("\"zona\" está repetida");
    zone_ = true;
    b_.family.tz = int16_t(v);
  } else if (w == "sal") {
    if (t.size() != 2 || !fromHex(t[1], b_.family.salt, sizeof b_.family.salt)) return fail(bad);
    if (salt_) return fail("\"sal\" está repetida");
    salt_ = b_.family.hasSalt = true;
  } else if ((w == "bloquear" || w == "permitir") && b_.full) {
    if (t.size() != 2) return fail(bad);
    std::string d = t[1];
    if (!adblock::normalizeDomain(d)) return fail("el dominio no es válido");
    for (const auto* v : {&b_.block, &b_.allow})
      for (const auto& x : *v)
        if (x == d) return fail("la regla está repetida");
    if (b_.block.size() + b_.allow.size() >= kBackupMaxRules) return fail("demasiadas reglas");
    (w == "bloquear" ? b_.block : b_.allow).push_back(d);
  } else if (w == "aparato") {
    uint32_t slot;
    DeviceId id;
    if (t.size() != 4 || !parseUint(t[1], kMaxDevices - 1, slot) || !parseDeviceId(t[2].data(), t[2].size(), id))
      return fail(bad);
    if (!validDeviceName(t[3])) return fail("el nombre del aparato no es válido");
    if (!b_.family.setSlot(int(slot), id, t[3])) return fail("el aparato está repetido");
  } else if (w == "horario") {
    std::vector<std::string> h = split(l, 7);
    uint32_t dev = 0, g, d, ini, fin;
    if (h.size() != 6 || h[1].size() != 8) return fail(bad);
    uint8_t raw[4];
    if (!fromHex(h[1], raw, 4)) return fail(bad);
    dev = uint32_t(raw[0]) << 24 | uint32_t(raw[1]) << 16 | uint32_t(raw[2]) << 8 | raw[3];
    if (!parseUint(h[2], 7, g) || !parseUint(h[3], 127, d) || !parseUint(h[4], 1439, ini) ||
        !parseUint(h[5], 1439, fin))
      return fail(bad);
    Schedule s{dev, uint8_t(g), uint8_t(d), uint16_t(ini), uint16_t(fin)};
    if (!validSchedule(s)) return fail("el horario no es válido");
    if (schedules_.size() >= kMaxSchedules) return fail("demasiados horarios");
    schedules_.push_back({s, lineNo_});
  } else {
    return fail(bad);
  }
  return true;
}

bool BackupReader::finish() {
  if (failed_) return false;
  lineNo_ = 0;
  if (!header_ || !end_) return fail("el archivo está cortado");
  if (!salt_) return fail("falta la línea \"sal\"");
  if (b_.full && !lists_) return fail("falta la línea \"listas\"");
  for (const Pending& p : schedules_) {
    if (!b_.family.setSchedule(-1, p.s)) {
      lineNo_ = p.line;
      return fail("el horario usa un aparato que no existe");
    }
  }
  return true;
}

std::string writeFamily(const Family& f) {
  std::string out;
  out.reserve(256);
  out += kHeadFamily;
  out += '\n';
  writeHead(out, f);
  writeFamilyBody(out, f);
  return out;
}

std::string writeBackup(uint8_t lists, bool relay, const std::vector<std::string>& block,
                        const std::vector<std::string>& allow, const Family& f) {
  std::string out;
  out.reserve(1024);
  out += kHeadFull;
  out += '\n';
  appendLine(out, "listas", std::to_string(unsigned(lists)));
  appendLine(out, "relay", relay ? "1" : "0");
  writeHead(out, f);
  for (const auto& d : block) appendLine(out, "bloquear", d);
  for (const auto& d : allow) appendLine(out, "permitir", d);
  writeFamilyBody(out, f);
  return out;
}

}  // namespace siblock

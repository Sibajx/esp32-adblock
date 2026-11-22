#include "family.h"

#include <cstring>

#include "../adblock_core/adblock_core.h"
#include "siblock_crypto.h"

namespace siblock {

namespace {
// Grupos de sitios para los horarios ("Redes sociales" y "Juegos"). Ver docs/grupos.md.
//
// Fuente: AdGuard HostlistsRegistry, "blocked services"
//   https://github.com/AdguardTeam/HostlistsRegistry
//   licencia GPL-3.0 (la misma de SiBlock), commit ae662d803ad6bfb4b9de09661f9396c978952dac
//   (main, 2026-11-22). Se tomaron solo los dominios base de cada servicio.
// Agregados a mano (no están en AdGuard): threads.com, threads.net, fortnite.com, xbox.com,
//   Supercell (Brawl Stars, Clash), Garena / Free Fire, HoYoverse, Niantic, Innersloth (Among Us),
//   Stumble Guys y Moonton (Mobile Legends).
//   Falta verificar con un aparato real: ggblueshark.com, ggwhitehawk.com, stumbleguys.com,
//   kitkagames.com.
// Nunca van aquí (se cae media casa): microsoft.com, live.com, google.com, googleapis.com,
//   gstatic.com, apple.com ni CDNs compartidos (akamai, cloudfront). WhatsApp y Messenger son
//   mensajes, no redes. YouTube no está (se usa para la escuela).
//
// IMPORTANTE: ordenados por bytes (strcmp, LC_ALL=C) porque se buscan con búsqueda binaria.
// Un nombre coincide si es igual a uno de la lista o es un subdominio suyo.
const char* const kSocial[] = {
    "ads-twitter.com", "bsky.app", "bsky.social", "cdninstagram.com", "dis.gd", "discord.co", "discord.com",
    "discord.gg", "discord.gift", "discord.media", "discord.new", "discordapp.com", "discordapp.net",
    "discordcdn.com", "discordsays.com", "ext-twitch.tv", "facebook.com", "facebook.net", "fb.com", "fb.gg",
    "fb.me", "fb.watch", "fbcdn.com", "fbcdn.net", "fbsbx.com", "ig.me", "igcdn.com", "igsonar.com",
    "igtv.com", "impala-media-production.s3.amazonaws.com", "instagram.com", "jtvnw.net", "muscdn.com",
    "musical.ly", "periscope.tv", "pin.it", "pinimg.com", "pinterest.cl", "pinterest.co", "pinterest.com",
    "pinterest.com.mx", "pinterest.es", "pinterest.mx", "pscp.tv", "redd.it", "reddit.com",
    "redditmedia.com", "redditstatic.com", "sc-cdn.net", "snap-dev.net", "snapads.com", "snapchat.com",
    "snapkit.co", "t.co", "threads.com", "threads.net", "tiktok.com", "tiktokcdn-eu.com",
    "tiktokcdn-us.com", "tiktokcdn.com", "tiktokrow-cdn.com", "tiktokv.com", "tiktokv.eu", "ttlivecdn.com",
    "ttvnw.net", "tweetdeck.com", "twimg.com", "twitch.tv", "twitchcdn.net", "twitchsvc.net", "twitter.com",
    "twttr.com", "twvid.com", "x.com",
};
const char* const kGames[] = {
    "activision.com", "activisionblizzard.com", "among.us", "battle.net", "blizzard.com", "blox.com",
    "brawlstars.com", "brawlstarsgame.com", "callofduty.com", "clashofclans.com", "clashroyale.com",
    "clashroyaleapp.com", "cubecraft.net", "demonware.net", "ea.com", "eac-cdn.com", "eamobile.com",
    "easports.com", "easy.ac", "easyanticheat.net", "epicgames.com", "fortnite.com", "freefiremobile.com",
    "gaikai.com", "gamepass.com", "garena.com", "garenanow.com",
    "ggblueshark.com",  // falta verificar con un aparato real
    "ggwhitehawk.com",  // falta verificar con un aparato real
    "hoyolab.com", "hoyoverse.com", "innersloth.com",
    "kitkagames.com",  // falta verificar con un aparato real
    "leagueoflegends.com", "lolstatic.com", "lolusercontent.com", "mihoyo.com", "minecraft-services.net",
    "minecraft.net", "minecraftservices.com", "mobilelegends.com", "mobilelegends.net", "mojang.com",
    "moonton.com", "nianticlabs.com", "nintendo-europe.com", "nintendo.co.jp", "nintendo.com", "nintendo.es",
    "nintendo.eu", "nintendo.jp", "nintendo.net", "nintendowifi.net", "origin.com", "playstation-cloud.com",
    "playstation.com", "playstation.net", "playvalorant.com", "pokemongolive.com", "pvp.net", "rbx.com",
    "rbxcdn.com", "rbxcdn.net", "rbxinfra.com", "rbxinfra.net", "rgpub.io", "riotcdn.com", "riotcdn.net",
    "riotgames.com", "roblox.com", "robloxcdn.com", "s.team", "sonyentertainmentnetwork.com",
    "steam-chat.com", "steamcommunity.com", "steamcontent.com", "steamdeck.com", "steamgames.com",
    "steampowered.com", "steamserver.net", "steamstatic.com", "steamusercontent.com",
    "stumbleguys.com",  // falta verificar con un aparato real
    "supercell.com", "supercellid.com", "tnt-ea.com", "valvesoftware.com", "xbox.com", "xboxab.com",
    "xboxlive.com", "xboxservices.com",
};

bool strictlySorted(const char* const* arr, size_t count) {
  for (size_t i = 1; i < count; i++)
    if (std::strcmp(arr[i - 1], arr[i]) >= 0) return false;
  return true;
}

// Compara (p, n) con una cadena terminada en '\0' como lo haría strcmp.
int cmpN(const char* p, size_t n, const char* s) {
  for (size_t i = 0; i < n; i++) {
    unsigned char a = static_cast<unsigned char>(p[i]), b = static_cast<unsigned char>(s[i]);
    if (b == 0) return 1;  // p es más largo
    if (a != b) return a < b ? -1 : 1;
  }
  return s[n] == 0 ? 0 : -1;
}

bool inSorted(const char* const* arr, size_t count, const char* p, size_t n) {
  size_t lo = 0, hi = count;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    int c = cmpN(p, n, arr[mid]);
    if (c == 0) return true;
    if (c < 0) hi = mid;
    else lo = mid + 1;
  }
  return false;
}

// Largo de la secuencia UTF-8 válida en p[0..left) (0 si no es válida). Misma regla que
// adblock::jsonEscape: sin formas largas ni sustitutos.
size_t utf8Len(const unsigned char* p, size_t left) {
  unsigned char c = p[0];
  size_t n;
  uint32_t cp;
  if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1F;
  else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0F;
  else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07;
  else return 0;
  if (n > left) return 0;
  for (size_t i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
    return 0;
  return n;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

bool operator==(const DeviceId& a, const DeviceId& b) { return std::memcmp(a.b, b.b, sizeof a.b) == 0; }

std::string deviceIdHex(const DeviceId& id) { return toHex(id.b, sizeof id.b); }

bool parseDeviceId(const char* s, size_t n, DeviceId& out) {
  if (!s || n != 16) return false;
  DeviceId tmp;
  for (size_t i = 0; i < 8; i++) {
    int hi = hexVal(s[2 * i]), lo = hexVal(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    tmp.b[i] = uint8_t(hi << 4 | lo);
  }
  out = tmp;
  return true;
}

DeviceId deviceIdFor(const uint8_t salt[16], const uint8_t mac[6]) {
  uint8_t buf[22], h[32];
  std::memcpy(buf, salt, 16);
  std::memcpy(buf + 16, mac, 6);
  sha256(buf, sizeof buf, h);
  DeviceId id;
  std::memcpy(id.b, h, sizeof id.b);
  return id;
}

bool validDeviceName(const std::string& s) {
  if (s.empty() || s.size() > kDeviceNameMax) return false;
  if (s.front() == ' ' || s.back() == ' ') return false;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = p[i];
    if (c < 0x20 || c == 0x7F) return false;
    if (c < 0x80) {
      i++;
      continue;
    }
    size_t len = utf8Len(p + i, n - i);
    if (!len) return false;
    i += len;
  }
  return true;
}

bool validSchedule(const Schedule& s) {
  return s.devices != 0 && s.groups >= 1 && s.groups <= 7 && s.days >= 1 && s.days <= 127 && s.start <= 1439 &&
         s.end <= 1439 && s.start != s.end;
}

int32_t localMinuteOfWeek(int64_t epochSeconds, int16_t tzMinutes) {
  if (epochSeconds < 1700000000) return -1;
  int64_t local = epochSeconds + int64_t(tzMinutes) * 60;
  int64_t days = local / 86400;
  int32_t minute = int32_t((local % 86400) / 60);
  int32_t dow = int32_t((days + 3) % 7);  // 1970-01-01 fue jueves; lunes = 0
  return dow * 1440 + minute;
}

bool scheduleActive(const Schedule& s, int32_t mow) {
  if (mow < 0 || mow >= 7 * 1440) return false;
  int day = mow / 1440, m = mow % 1440;
  int prev = (day + 6) % 7;
  bool today = s.days & (1u << day), yesterday = s.days & (1u << prev);
  if (s.start < s.end) return today && m >= s.start && m < s.end;
  if (s.end < s.start) return (today && m >= s.start) || (yesterday && m < s.end);
  return false;
}

bool inGroups(const std::string& name, uint8_t groups) {
  if (groups & kGroupAll) return true;
  return adblock::forEachSuffix(name, [&](const char* p, size_t n) {
    if ((groups & kGroupSocial) && inSorted(kSocial, sizeof kSocial / sizeof *kSocial, p, n)) return true;
    if ((groups & kGroupGames) && inSorted(kGames, sizeof kGames / sizeof *kGames, p, n)) return true;
    return false;
  });
}

// Solo para las pruebas (test_host); no está en family.h. Dice si las dos listas siguen
// ordenadas sin repetidos y cuántos dominios tiene cada una.
bool groupListsSortedForTest(size_t& social, size_t& games) {
  social = sizeof kSocial / sizeof *kSocial;
  games = sizeof kGames / sizeof *kGames;
  return strictlySorted(kSocial, social) && strictlySorted(kGames, games);
}

bool validTzMinutes(int32_t tz) { return tz >= -720 && tz <= 840; }

// ---------- Family ----------

void Family::clear() {
  std::memset(slots_, 0, sizeof slots_);
  std::memset(schedules_, 0, sizeof schedules_);
  nSchedules_ = 0;
  tz = 0;
  std::memset(salt, 0, sizeof salt);
  hasSalt = false;
}

int Family::slotOf(const DeviceId& id) const {
  for (size_t i = 0; i < kMaxDevices; i++)
    if (slots_[i].used && slots_[i].id == id) return int(i);
  return -1;
}

const char* Family::nameAt(int slot) const { return used(slot) ? slots_[slot].name : nullptr; }

bool Family::used(int slot) const { return slot >= 0 && size_t(slot) < kMaxDevices && slots_[slot].used; }

DeviceId Family::idAt(int slot) const { return used(slot) ? slots_[slot].id : DeviceId{}; }

uint32_t Family::usedMask() const {
  uint32_t m = 0;
  for (size_t i = 0; i < kMaxDevices; i++)
    if (slots_[i].used) m |= 1u << i;
  return m;
}

bool Family::setSlot(int slot, const DeviceId& id, const std::string& name) {
  if (slot < 0 || size_t(slot) >= kMaxDevices || slots_[slot].used) return false;
  if (!validDeviceName(name) || slotOf(id) >= 0) return false;
  Slot& s = slots_[slot];
  s.used = true;
  s.id = id;
  std::memcpy(s.name, name.data(), name.size());
  s.name[name.size()] = 0;
  return true;
}

bool Family::setName(const DeviceId& id, const std::string& name) {
  if (!validDeviceName(name)) return false;
  int at = slotOf(id);
  if (at >= 0) {
    std::memcpy(slots_[at].name, name.data(), name.size());
    slots_[at].name[name.size()] = 0;
    return true;
  }
  for (size_t i = 0; i < kMaxDevices; i++)
    if (!slots_[i].used) return setSlot(int(i), id, name);
  return false;
}

bool Family::forget(const DeviceId& id) {
  int at = slotOf(id);
  if (at < 0) return false;
  std::memset(&slots_[at], 0, sizeof slots_[at]);
  uint32_t bit = 1u << at;
  for (size_t i = 0; i < nSchedules_;) {
    schedules_[i].devices &= ~bit;
    if (schedules_[i].devices == 0) removeSchedule(int(i));
    else i++;
  }
  return true;
}

bool Family::setSchedule(int index, const Schedule& s) {
  if (!validSchedule(s) || (s.devices & ~usedMask())) return false;
  if (index == -1) {
    if (nSchedules_ >= kMaxSchedules) return false;
    schedules_[nSchedules_++] = s;
    return true;
  }
  if (index < 0 || size_t(index) >= nSchedules_) return false;
  schedules_[index] = s;
  return true;
}

bool Family::removeSchedule(int index) {
  if (index < 0 || size_t(index) >= nSchedules_) return false;
  for (size_t i = size_t(index); i + 1 < nSchedules_; i++) schedules_[i] = schedules_[i + 1];
  nSchedules_--;
  std::memset(&schedules_[nSchedules_], 0, sizeof schedules_[nSchedules_]);
  return true;
}

uint8_t Family::activeGroups(int slot, int32_t minuteOfWeek) const {
  if (!used(slot) || minuteOfWeek < 0) return 0;
  uint32_t bit = 1u << slot;
  uint8_t g = 0;
  for (size_t i = 0; i < nSchedules_; i++) {
    const Schedule& s = schedules_[i];
    if ((s.devices & bit) && scheduleActive(s, minuteOfWeek)) g |= s.groups;
  }
  return g;
}

bool Family::blockedBySchedule(int slot, const std::string& name, int32_t minuteOfWeek) const {
  if (!used(slot) || minuteOfWeek < 0) return false;
  uint32_t bit = 1u << slot;
  for (size_t i = 0; i < nSchedules_; i++) {
    const Schedule& s = schedules_[i];
    if ((s.devices & bit) && scheduleActive(s, minuteOfWeek) && inGroups(name, s.groups)) return true;
  }
  return false;
}

}  // namespace siblock

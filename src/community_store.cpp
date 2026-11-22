#include "community_store.h"

#include <LittleFS.h>
#include <time.h>

#include <vector>

#include "blocklist.h"
#include "cloud.h"

namespace {
constexpr const char* kFile = "/comunidad.bin";
constexpr const char* kTmp = "/comunidad.new";
constexpr size_t kMaxFile = 16 + siblock::kCommunityMax * 8;

BlockList* gList = nullptr;
community::Info gInfo;  // lo tocan solo el loop (main.cpp) y el panel, que corren en la misma tarea

void apply(siblock::CommunityList& l) {
  gInfo.count = uint16_t(l.keys.size());
  gInfo.version = l.version;
  gInfo.updated = l.updated;
  if (gList) gList->setCommunity(std::move(l.keys));
}

// Se escribe en /comunidad.new y se renombra: un corte de luz deja la vieja o la nueva.
bool save(const siblock::CommunityList& l) {
  std::string data = siblock::encodeCommunity(l);
  fs::File f = LittleFS.open(kTmp, FILE_WRITE);
  bool ok = f && f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
  if (f) f.close();
  if (ok && !LittleFS.rename(kTmp, kFile)) {
    LittleFS.remove(kFile);  // por si alguna versión no pisa el destino
    ok = LittleFS.rename(kTmp, kFile);
  }
  if (!ok) LittleFS.remove(kTmp);
  return ok;
}
}  // namespace

namespace community {

void begin(BlockList* list) {
  gList = list;
  LittleFS.remove(kTmp);
  if (!cloud::status().community) {
    LittleFS.remove(kFile);
    return;
  }
  fs::File f = LittleFS.open(kFile, FILE_READ);
  if (!f) return;
  std::vector<uint8_t> buf;
  size_t size = f.size();
  if (size <= kMaxFile) {
    buf.resize(size);
    if (f.read(buf.data(), size) != size) buf.clear();
  }
  f.close();
  siblock::CommunityList l;
  if (!buf.empty() && siblock::decodeCommunity(buf.data(), buf.size(), l)) {
    apply(l);
    Serial.printf("[comunidad] %u permitidos guardados\n", unsigned(gInfo.count));
  } else {
    Serial.println("[comunidad] /comunidad.bin dañado: se ignora hasta la próxima bajada");
  }
}

bool refresh() {
  siblock::CommunityParser parser;
  siblock::CommunityList l;
  if (!cloud::fetchCommunity(parser) || !parser.finish(l)) return false;
  time_t now = time(nullptr);
  l.updated = now > 1700000000 ? uint32_t(now) : 0;
  if (!save(l)) Serial.println("[comunidad] no se pudo guardar en la flash; se usa hasta reiniciar");
  apply(l);
  return true;
}

void clear() {
  siblock::CommunityList vacia;
  apply(vacia);
  LittleFS.remove(kFile);
  LittleFS.remove(kTmp);
}

Info info() { return gInfo; }

}  // namespace community

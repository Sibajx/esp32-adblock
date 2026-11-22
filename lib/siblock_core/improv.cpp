#include "improv.h"

#include <cstring>

namespace siblock::improv {
namespace {
constexpr uint8_t kHeader[6] = {'I', 'M', 'P', 'R', 'O', 'V'};
constexpr uint8_t kVersion = 1;
}  // namespace

void Parser::dropFront(size_t k) {
  if (k >= n_) {
    n_ = 0;
    return;
  }
  memmove(buf_, buf_ + k, n_ - k);
  n_ -= k;
}

// Lo que hay en buf_ debe ser un prefijo de "IMPROV" + versión. Si no, se corre hasta la
// siguiente posición que sí lo sea (o se vacía).
void Parser::resync() {
  while (n_) {
    size_t check = n_ < 7 ? n_ : 7;
    bool ok = true;
    for (size_t i = 0; i < check && ok; i++)
      ok = i < 6 ? buf_[i] == kHeader[i] : buf_[i] == kVersion;
    if (ok) return;
    dropFront(1);
  }
}

Parser::Status Parser::feed(uint8_t byte, Packet& out) {
  if (n_ == kMax) dropFront(1);  // no pasa: un paquete completo se procesa antes de llenarse
  buf_[n_++] = byte;
  resync();
  Status st = kNone;
  // Puede haber más de un intento en una sola llamada: tras un checksum malo, lo ya recibido
  // puede contener otro paquete completo.
  while (n_ >= 9) {
    size_t total = 6 + 3 + size_t(buf_[8]) + 1;
    if (n_ < total) break;
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < total; i++) sum = uint8_t(sum + buf_[i]);
    if (sum == buf_[total - 1]) {
      out.type = buf_[7];
      out.len = buf_[8];
      memcpy(out.data, buf_ + 9, out.len);
      dropFront(total);
      resync();
      return kPacket;
    }
    st = kBadChecksum;
    dropFront(1);
    resync();
  }
  return st;
}

bool parseRpc(const Packet& p, Command& out) {
  out.id = 0;
  out.args.clear();
  if (p.type != kRpc || p.len < 2) return false;
  size_t inner = p.data[1];
  if (2 + inner != p.len) return false;
  out.id = p.data[0];
  size_t pos = 2;
  while (pos < p.len) {
    size_t n = p.data[pos++];
    if (pos + n > p.len) {
      out.args.clear();
      return false;
    }
    out.args.emplace_back(reinterpret_cast<const char*>(p.data + pos), n);
    pos += n;
  }
  return true;
}

std::vector<uint8_t> buildPacket(uint8_t type, const uint8_t* data, uint8_t len) {
  std::vector<uint8_t> v(kHeader, kHeader + 6);
  v.push_back(kVersion);
  v.push_back(type);
  v.push_back(len);
  if (len) v.insert(v.end(), data, data + len);
  uint8_t sum = 0;
  for (uint8_t b : v) sum = uint8_t(sum + b);
  v.push_back(sum);
  v.push_back('\n');
  return v;
}

std::vector<uint8_t> buildState(uint8_t state) { return buildPacket(kState, &state, 1); }
std::vector<uint8_t> buildError(uint8_t err) { return buildPacket(kError, &err, 1); }

std::vector<uint8_t> buildRpcResult(uint8_t cmd, const std::vector<std::string>& strings) {
  size_t inner = 0;
  for (const auto& s : strings) {
    if (s.size() > 255) return {};
    inner += 1 + s.size();
  }
  if (2 + inner > 255) return {};
  std::vector<uint8_t> d;
  d.reserve(2 + inner);
  d.push_back(cmd);
  d.push_back(uint8_t(inner));
  for (const auto& s : strings) {
    d.push_back(uint8_t(s.size()));
    d.insert(d.end(), s.begin(), s.end());
  }
  return buildPacket(kRpcResult, d.data(), uint8_t(d.size()));
}

TryResult evalTry(uint32_t elapsedMs, bool connected, bool sameSsid) {
  if (elapsedMs >= kTryTimeoutMs) return TryResult::kFailed;
  if (elapsedMs >= kTrySettleMs && connected && sameSsid) return TryResult::kConnected;
  return TryResult::kWaiting;
}
}  // namespace siblock::improv

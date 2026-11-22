#include "usage.h"

#include <algorithm>
#include <cstring>

namespace siblock {

uint32_t epochHour(int64_t epochSeconds) {
  if (epochSeconds < 1700000000LL) return 0;
  int64_t h = epochSeconds / 3600;
  if (h > int64_t(UINT32_MAX)) return 0;
  return uint32_t(h);
}

void UsageHistory::add(uint32_t hour, bool blocked) {
  if (hour == 0) return;
  if (last_ != 0 && hour < last_ && last_ - hour >= kHours) {
    // Demasiado vieja. Si pasa muchas veces seguidas, la "última hora" estaba mal (una hora
    // equivocada hacia el futuro): se empieza de nuevo para que la historia no quede congelada.
    if (++staleRun_ < kStaleReset) return;
    memset(slots_, 0, sizeof(slots_));
    last_ = 0;
  }
  staleRun_ = 0;
  if (last_ == 0) {
    last_ = hour;
  } else if (hour > last_) {
    uint32_t gap = hour - last_;
    if (gap >= kHours) {
      memset(slots_, 0, sizeof(slots_));
    } else {
      for (uint32_t k = 1; k <= gap; k++) slots_[(last_ + k) % kHours] = {0, 0};
    }
    last_ = hour;
  }
  Slot& s = slots_[hour % kHours];
  if (s.queries != UINT32_MAX) s.queries++;
  if (blocked && s.blocked != UINT32_MAX) s.blocked++;
}

void UsageHistory::snapshot(uint32_t nowHour, Slot out[kHours]) const {
  for (size_t i = 0; i < kHours; i++) {
    out[i] = {0, 0};
    uint32_t back = uint32_t(kHours - 1 - i);
    if (back >= nowHour) continue;  // antes de la hora 1: no existe
    uint32_t hour = nowHour - back;
    if (last_ == 0 || hour > last_ || last_ - hour >= kHours) continue;
    out[i] = slots_[hour % kHours];
  }
}

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}
static uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

size_t UsageHistory::serialize(uint8_t* out, size_t max) const {
  if (max < kBytes) return 0;
  memcpy(out, "SBH1", 4);
  put32(out + 4, last_);
  for (size_t i = 0; i < kHours; i++) {
    put32(out + 8 + i * 8, slots_[i].queries);
    put32(out + 12 + i * 8, slots_[i].blocked);
  }
  return kBytes;
}

bool UsageHistory::deserialize(const uint8_t* in, size_t len) {
  if (in == nullptr || len != kBytes || memcmp(in, "SBH1", 4) != 0) return false;
  // Se revisa todo antes de tocar nada: un archivo dañado no debe congelar la historia (última
  // hora enorme) ni mostrar horas fantasma (casillas con datos sin última hora).
  uint32_t last = get32(in + 4);
  if (last != 0 && (last < kMinHour || last > kMaxHour)) return false;
  Slot tmp[kHours];
  for (size_t i = 0; i < kHours; i++) {
    tmp[i].queries = get32(in + 8 + i * 8);
    tmp[i].blocked = get32(in + 12 + i * 8);
    if (tmp[i].blocked > tmp[i].queries) return false;  // add() nunca deja más bloqueadas que consultas
    if (last == 0 && tmp[i].queries != 0) return false;
  }
  last_ = last;
  memcpy(slots_, tmp, sizeof(slots_));
  staleRun_ = 0;
  return true;
}

void TopBlocked::add(const char* name) {
  if (name == nullptr || name[0] == '\0') return;
  char n[kNameMax + 1];
  size_t len = strnlen(name, kNameMax);
  memcpy(n, name, len);
  n[len] = '\0';
  Entry* empty = nullptr;
  Entry* min = nullptr;
  for (auto& e : e_) {
    if (e.count == 0) {
      if (!empty) empty = &e;
      continue;
    }
    if (strcmp(e.name, n) == 0) {
      if (e.count != UINT32_MAX) e.count++;
      return;
    }
    if (!min || e.count < min->count) min = &e;
  }
  Entry* dst = empty ? empty : min;
  uint32_t base = empty ? 0 : min->count;
  memcpy(dst->name, n, len + 1);
  dst->count = base == UINT32_MAX ? base : base + 1;
}

size_t TopBlocked::top(Entry* out, size_t max) const {
  const Entry* sorted[kSlots];
  size_t n = 0;
  for (auto& e : e_)
    if (e.count) sorted[n++] = &e;
  std::sort(sorted, sorted + n, [](const Entry* a, const Entry* b) {
    if (a->count != b->count) return a->count > b->count;
    return strcmp(a->name, b->name) < 0;
  });
  size_t k = std::min(n, max);
  for (size_t i = 0; i < k; i++) out[i] = *sorted[i];
  return k;
}

void TopBlocked::halve() {
  for (auto& e : e_) {
    e.count /= 2;
    if (e.count == 0) e.name[0] = '\0';
  }
}

uint64_t savedBytes(const UsageHistory::Slot* slots, size_t n) {
  uint64_t blocked = 0;
  for (size_t i = 0; i < n; i++) blocked += slots[i].blocked;
  return blocked * kSavedBytesPerBlocked;
}

}  // namespace siblock

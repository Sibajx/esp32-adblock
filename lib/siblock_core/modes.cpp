#include "modes.h"

namespace siblock {

namespace {
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kShortMaxMs = 2000;      // < 2 s
constexpr uint32_t kConfigMinMs = 3000;     // 3..8 s
constexpr uint32_t kConfigMaxMs = 8000;
constexpr uint32_t kResetMs = 10000;        // >= 10 s
constexpr uint32_t kBlinkMs = 100;
}  // namespace

void ButtonTracker::update(bool pressed, uint32_t nowMs) {
  if (pressed != raw_) {
    raw_ = pressed;
    rawSince_ = nowMs;
  }
  if (raw_ != stable_ && nowMs - rawSince_ >= kDebounceMs) {
    stable_ = raw_;
    if (stable_) {
      pressStart_ = rawSince_;
      resetFired_ = false;
    } else if (!resetFired_) {
      uint32_t d = rawSince_ - pressStart_;
      if (d < kShortMaxMs) pending_ = ButtonAction::kOpenSetupWindow;
      else if (d >= kConfigMinMs && d <= kConfigMaxMs) pending_ = ButtonAction::kConfigMode;
    }
  }
  if (stable_ && !resetFired_ && nowMs - pressStart_ >= kResetMs) {
    resetFired_ = true;
    pending_ = ButtonAction::kFactoryReset;
  }
}

ButtonAction ButtonTracker::take() {
  ButtonAction a = pending_;
  pending_ = ButtonAction::kNone;
  return a;
}

bool ledWhileHeld(uint32_t heldMs, uint32_t nowMs) {
  bool blink = (nowMs / kBlinkMs) % 2 == 0;
  if (heldMs < kConfigMinMs) return blink;
  if (heldMs < kConfigMaxMs) return true;
  if (heldMs < kResetMs) return blink;
  return false;
}

LedState ledStateFor(bool staConnected, bool portalOpen, bool updating, uint32_t listCount, bool upstreamDown) {
  if (updating) return LedState::kUpdating;
  if (!staConnected || portalOpen || listCount == 0 || upstreamDown) return LedState::kProblem;
  return LedState::kOk;
}

bool ledOn(LedState st, uint32_t nowMs) {
  switch (st) {
    case LedState::kUpdating:
      return (nowMs / 1000) % 2 == 0;
    case LedState::kProblem:
      return (nowMs / 200) % 2 == 0;
    default:
      return true;
  }
}

void SetupWindow::open(uint32_t nowMs) {
  open_ = true;
  start_ = nowMs;
}

bool SetupWindow::isOpen(uint32_t nowMs) const {
  if (open_ && nowMs - start_ >= kMs) open_ = false;
  return open_;
}

uint32_t SetupWindow::secondsLeft(uint32_t nowMs) const {
  if (!isOpen(nowMs)) return 0;
  return (kMs - (nowMs - start_) + 999) / 1000;
}

void PortalPolicy::tick(uint32_t nowMs, bool hasSavedWifi, bool staConnected, int apStations) {
  if (!started_) {
    started_ = true;
    waitStart_ = nowMs;
  }
  if (open_) {
    if (staConnected && !forced_) {
      open_ = false;
      waitStart_ = nowMs;
    } else if (apStations > 0) {
      idleStart_ = nowMs;
    } else if (nowMs - idleStart_ >= kIdleMs) {
      open_ = forced_ = false;
      waitStart_ = nowMs;
    }
    return;
  }
  if (staConnected) {
    waitStart_ = nowMs;
    return;
  }
  if (nowMs - waitStart_ >= (hasSavedWifi ? kLostWifiMs : kNoWifiMs)) {
    open_ = true;
    forced_ = false;
    idleStart_ = nowMs;
  }
}

void PortalPolicy::forceOpen(uint32_t nowMs) {
  started_ = true;
  open_ = forced_ = true;
  idleStart_ = nowMs;
}

void PortalPolicy::forceClose(uint32_t nowMs) {
  started_ = true;
  open_ = forced_ = false;
  waitStart_ = nowMs;
}

void PortalPolicy::improvSeen(uint32_t nowMs) {
  started_ = true;
  waitStart_ = nowMs;
}

bool subnetsOverlap(uint32_t a, uint32_t maskA, uint32_t b, uint32_t maskB) {
  if (maskA == 0 || maskB == 0) return false;
  uint32_t m = maskA & maskB;  // la red más grande de las dos
  return (a & m) == (b & m);
}

uint32_t pickApAddress(uint32_t staIp, uint32_t staMask) {
  static const uint32_t kCandidates[] = {ipv4(192, 168, 4, 1), ipv4(192, 168, 243, 1), ipv4(172, 31, 243, 1),
                                         ipv4(10, 243, 77, 1)};
  if (staIp == 0) staMask = 0;  // sin IP no hay red de la casa
  for (uint32_t c : kCandidates)
    if (!subnetsOverlap(c, kApMask, staIp, staMask)) return c;
  return kCandidates[0];  // una red de la casa que abarca todo: quien llama lo detecta y no confía
}
}  // namespace siblock

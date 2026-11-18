#include "schedule.h"

namespace core {

uint8_t sfOf(int slot) { return slot == 0 ? 7 : 12; }

TxAction txAction(int64_t nowMs, int64_t txAtMs) {
  if (nowMs - txAtMs > kTxMaxLateMs) return TxAction::Late;
  if (txAtMs - nowMs > kTxPrepareMs) return TxAction::Wait;
  return TxAction::Send;
}

Slot slotAt(int64_t utcMs) {
  // Shift by the SF7 lead so that window boundaries fall on multiples of the cycle.
  const int64_t shifted = utcMs + kWindowLeadMs;
  const int64_t cycle = shifted / kCycleMs;
  const int64_t base = cycle * kCycleMs;  // cycle start
  const int64_t sinceStart = utcMs - base;
  Slot s;
  s.cycle = static_cast<uint32_t>(cycle);
  if (sinceStart < kSf12WindowStartMs) {
    s.slot = 0;
    s.startMs = base - kWindowLeadMs;
    s.endMs = base + kSf12WindowStartMs;
  } else {
    s.slot = 1;
    s.startMs = base + kSf12WindowStartMs;
    s.endMs = base + kCycleMs - kWindowLeadMs;
  }
  return s;
}

int64_t txTimeMs(uint32_t cycle, int slot) {
  return static_cast<int64_t>(cycle) * kCycleMs + kTxOffsetMs[slot == 0 ? 0 : 1];
}

bool inTxWindow(int64_t utcS, int startMin, int endMin, bool enabled) {
  if (!enabled) return true;
  const int minute = static_cast<int>((utcS % 86400) / 60);
  if (startMin <= endMin) return minute >= startMin && minute < endMin;
  return minute >= startMin || minute < endMin;  // wraps past midnight
}

}  // namespace core

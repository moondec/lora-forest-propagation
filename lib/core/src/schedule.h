// GNSS-time schedule shared by the tower and mobile nodes (methodology Section 4.2).
#pragma once

#include <stdint.h>

namespace core {

constexpr uint32_t kCycleMs = 15000;
// Slot 0 = SF7, slot 1 = SF12; transmit offsets from the cycle start.
constexpr int32_t kTxOffsetMs[2] = {0, 1000};
// Receive windows relative to the cycle start: SF7 [-400, +700), SF12 [+700, +14600).
// They tile the timeline, so every moment belongs to exactly one window.
constexpr int32_t kWindowLeadMs = 400;
constexpr int32_t kSf12WindowStartMs = 700;

struct Slot {
  uint32_t cycle;
  int slot;         // 0 = SF7, 1 = SF12
  int64_t startMs;  // UTC ms, inclusive
  int64_t endMs;    // UTC ms, exclusive
};

// Tower decision for the next slot: wait, start the transmission (prepared up to 30 ms ahead,
// still sent up to 100 ms late; tx_offset_us records the delay), or skip it as late.
enum class TxAction { Wait, Send, Late };
constexpr int64_t kTxPrepareMs = 30, kTxMaxLateMs = 100;
TxAction txAction(int64_t nowMs, int64_t txAtMs);

uint8_t sfOf(int slot);
Slot slotAt(int64_t utcMs);
int64_t txTimeMs(uint32_t cycle, int slot);
// Daily UTC transmit window [startMin, endMin), may wrap past midnight.
bool inTxWindow(int64_t utcS, int startMin, int endMin, bool enabled);

}  // namespace core

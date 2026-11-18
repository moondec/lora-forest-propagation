// LoRa time on air and the 10 % duty-cycle guard (ETSI EN 300 220-2 band 869.40-869.65 MHz).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

// Semtech SX126x time-on-air formula, in milliseconds.
double loraAirtimeMs(uint8_t sf, uint32_t bwHz, uint8_t crDenom, uint16_t preamble, size_t payload,
                     bool explicitHeader, bool crc, bool ldro);

class DutyGuard {
 public:
  explicit DutyGuard(uint32_t windowMs = 3600000, uint32_t budgetMs = 360000);
  bool allow(int64_t nowMs, uint32_t airMs) const;
  void record(int64_t nowMs, uint32_t airMs);
  uint32_t usedMs(int64_t nowMs) const;

 private:
  static constexpr int kEntries = 1024;  // > 480 transmissions per hour of the schedule
  int64_t at_[kEntries];
  uint32_t air_[kEntries];
  int head_ = 0;
  uint32_t windowMs_, budgetMs_;
};

}  // namespace core

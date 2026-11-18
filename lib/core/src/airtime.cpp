#include "airtime.h"

#include <math.h>

namespace core {

double loraAirtimeMs(uint8_t sf, uint32_t bwHz, uint8_t crDenom, uint16_t preamble, size_t payload,
                     bool explicitHeader, bool crc, bool ldro) {
  const double tSym = static_cast<double>(1u << sf) / bwHz * 1000.0;
  const int de = ldro ? 1 : 0;
  const int ih = explicitHeader ? 0 : 1;
  const int crcBits = crc ? 16 : 0;
  const double num = 8.0 * payload - 4.0 * sf + 28 + crcBits - 20 * ih;
  const double den = 4.0 * (sf - 2 * de);
  const double nPayload = 8 + fmax(ceil(num / den) * (crDenom), 0.0);
  return (preamble + 4.25) * tSym + nPayload * tSym;
}

DutyGuard::DutyGuard(uint32_t windowMs, uint32_t budgetMs) : windowMs_(windowMs), budgetMs_(budgetMs) {
  for (int i = 0; i < kEntries; ++i) {
    at_[i] = 0;
    air_[i] = 0;
  }
}

uint32_t DutyGuard::usedMs(int64_t nowMs) const {
  uint32_t sum = 0;
  for (int i = 0; i < kEntries; ++i)
    if (air_[i] > 0 && nowMs - at_[i] < static_cast<int64_t>(windowMs_)) sum += air_[i];
  return sum;
}

bool DutyGuard::allow(int64_t nowMs, uint32_t airMs) const { return usedMs(nowMs) + airMs <= budgetMs_; }

void DutyGuard::record(int64_t nowMs, uint32_t airMs) {
  at_[head_] = nowMs;
  air_[head_] = airMs;
  head_ = (head_ + 1) % kEntries;
}

}  // namespace core

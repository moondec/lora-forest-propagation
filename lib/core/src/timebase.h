// Local monotonic clock (us) disciplined to UTC by NMEA RMC sentences (no PPS on Unit GPS v1.1).
#pragma once

#include <stdint.h>

namespace core {

class TimeBase {
 public:
  // utcS: UTC second of the RMC; markUs: local time of its '$';
  // latencyMs: delay between the start of that second and the '$' (bench-calibrated).
  void discipline(int64_t utcS, int64_t markUs, int32_t latencyMs);
  // Fresh: last valid RMC at most 10 s old. Valid: at most 600 s old (holdover). The ESP32
  // crystal drifts < 20 ppm, i.e. < 12 ms in 600 s, far inside the +-300 ms receive windows,
  // so a short loss of fix under the canopy does not stop the schedule.
  bool fresh(int64_t localUs) const;
  bool valid(int64_t localUs) const;
  int64_t utcMs(int64_t localUs) const;
  int64_t utcUs(int64_t localUs) const;

  static constexpr int64_t kFreshUs = 10000000, kHoldoverUs = 600000000;

 private:
  bool set_ = false;
  int64_t utcS_ = 0, markUs_ = 0;
  int32_t latencyMs_ = 0;
};

}  // namespace core

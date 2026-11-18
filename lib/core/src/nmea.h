// Minimal NMEA 0183 parser for the AT6668 receiver: RMC (time, date, position) and GGA (quality).
#pragma once

#include <stdint.h>

namespace core {

int64_t unixFromDate(int y, int mo, int d, int h, int mi, int s);

struct GnssState {
  bool rmcValid = false;  // last RMC had status A and a date
  int64_t utcS = 0;       // UTC second of the last valid RMC
  double lat = 0, lon = 0;
  uint8_t quality = 0, nsat = 0;
  float hdop = 99.9f;
};

class NmeaParser {
 public:
  // Returns true when a checksum-valid RMC with status A completes.
  bool feed(char c, int64_t localUs);
  const GnssState& state() const { return st_; }
  // Local time (us) of the first '$' of the NMEA burst that carried the last valid RMC.
  // The receiver starts each burst at a fixed delay after the UTC second, whereas the RMC's
  // position inside the burst varies with the number of GSV sentences before it.
  int64_t rmcMarkUs() const { return markUs_; }
  static constexpr int64_t kBurstGapUs = 20000;  // silence that separates two bursts

 private:
  bool finishLine();
  bool parseRmc(char** f, int n);
  void parseGga(char** f, int n);

  static constexpr int kMaxLine = 96;
  char line_[kMaxLine + 1];
  int len_ = -1;  // -1 = waiting for '$'
  int64_t burstStartUs_ = 0, lastByteUs_ = -1000000000;
  int64_t markUs_ = 0;
  GnssState st_;
};

}  // namespace core

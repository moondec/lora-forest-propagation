// Persistent per-board settings in NVS (namespace "lora"). No credentials are stored here in stage 1.
#pragma once

#include <stdint.h>

namespace settings {

struct S {
  uint8_t nodeId;
  int32_t latencyMs;  // NMEA latency, from the two-node bench test
  bool windowOn;      // tower transmit window enabled
  int16_t winStartMin, winEndMin;
  uint32_t fileCounter;  // next log file number
  // Mobile context, kept across restarts (e.g. a brown-out at a stop).
  char session[15], stop[15];
  uint16_t heightCm;
  uint8_t mode;
};

S load();
void save(const S& s);

}  // namespace settings

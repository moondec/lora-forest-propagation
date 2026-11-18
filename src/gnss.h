// Unit GPS v1.1 on the Grove UART: NMEA parsing in the UART callback, UTC time base for the schedule.
#pragma once

#include <stdint.h>

#include "nmea.h"

namespace gnss {

void begin(int32_t latencyMs);
void setLatencyMs(int32_t latencyMs);
// Thread-safe snapshot of the last parsed state.
core::GnssState state();
bool timeValid();  // within 600 s holdover of the last valid RMC
bool timeFresh();  // last RMC valid and at most 10 s old
int64_t utcMs();  // valid only if timeValid()
int64_t utcUs();
// 0 none, 1 time only, 2 position fix (as in the packet).
uint8_t fixCode();

}  // namespace gnss

// Shared application frame (app.cpp) and the role-specific hooks (tower.cpp or mobile.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "command.h"
#include "settings.h"

namespace app {

void setup();
void loop();

settings::S& cfgRef();
void logRecord(const uint8_t* rec);  // appends to the open file, if any
void say(const char* fmt, ...);      // formatted line to USB and BLE
void flashLed(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);
void writeMarker(const char* text);

// Implemented once per role.
void roleSetup();
void roleLogOpened();  // a new log file was started: write the role's current state into it
void roleLoop();
bool roleCommand(const core::Command& c);  // false if not handled by the role
void roleStatus();
void roleDisplay(char lines[3][12]);
bool roleRadioQuiet();  // true when no radio deadline is near (safe for dumps and redraws)

}  // namespace app

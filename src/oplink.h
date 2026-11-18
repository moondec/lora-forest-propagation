// Operator links: USB serial and BLE Nordic UART service share one command and output stream.
#pragma once

#include <stddef.h>

namespace oplink {

void begin(const char* bleName);
// Returns true and fills buf with one complete command line from USB or BLE.
bool readLine(char* buf, size_t n);
// Status/live text to USB (unless a dump is running) and to BLE.
void println(const char* s);
// Starts sending a log file over USB: "BEGIN <name> <size>\n", <size> raw bytes, "\nEND <crc32hex>\n".
bool startDump(const char* name);
bool dumping();
// Sends the next dump chunk (<= 2 kB). Call from the main loop when no radio event is near.
void service();
bool bleConnected();

}  // namespace oplink

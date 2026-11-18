// Text commands from USB serial and BLE (same parser for both).
#pragma once

#include <stdint.h>

namespace core {

enum class Cmd { None, Session, Stop, Height, Mode, Status, Files, Dump, Erase, Window, Latency, NodeId, Marker, Format, Help, Error };

struct Command {
  Cmd cmd = Cmd::None;
  char arg[32] = {0};  // text argument (id, file name, marker text) or error message
  float value = 0;     // height in m
  int a = 0, b = 0;    // window start/end minutes, latency ms, node ID, mode
  bool confirmed = false;
};

constexpr int kMaxIdLen = 14;  // session and stop IDs fill the 14-byte record fields, e.g. W1_ME_20270112

// Accepted: session <id> | stop <id> | h <0-9.99> | mode static|sweep|reference|drift | status | files |
// dump <file> | erase <file> confirm | window HH:MM-HH:MM | window off | latency <-999..999> |
// id <1-254> | marker <text> | format confirm | help
Command parseCommand(const char* line);

}  // namespace core

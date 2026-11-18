#include "command.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "record.h"

namespace core {

static Command error(const char* msg) {
  Command c;
  c.cmd = Cmd::Error;
  copyText(c.arg, msg, sizeof c.arg);
  return c;
}

static bool validId(const char* s) {
  const size_t n = strlen(s);
  if (n == 0 || n > static_cast<size_t>(kMaxIdLen)) return false;
  for (size_t i = 0; i < n; ++i)
    if (!isalnum(static_cast<unsigned char>(s[i])) && s[i] != '-' && s[i] != '_') return false;
  return true;
}

static bool parseHhMm(const char* s, int* minutes) {
  int h, m;
  char extra;
  if (sscanf(s, "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
  *minutes = h * 60 + m;
  return true;
}

Command parseCommand(const char* line) {
  char buf[96];
  copyText(buf, line, sizeof buf);
  // Trim trailing whitespace (CR/LF from terminals).
  for (int i = static_cast<int>(strlen(buf)) - 1; i >= 0 && isspace(static_cast<unsigned char>(buf[i])); --i)
    buf[i] = '\0';
  char* p = buf;
  while (isspace(static_cast<unsigned char>(*p))) ++p;
  if (!*p) return Command{};

  char* verb = strtok(p, " ");
  char* rest = strtok(nullptr, "");
  while (rest && isspace(static_cast<unsigned char>(*rest))) ++rest;
  Command c;

  if (!strcmp(verb, "status")) { c.cmd = Cmd::Status; return c; }
  if (!strcmp(verb, "files")) { c.cmd = Cmd::Files; return c; }
  if (!strcmp(verb, "help")) { c.cmd = Cmd::Help; return c; }
  if (!rest || !*rest) return error("missing argument");

  if (!strcmp(verb, "session") || !strcmp(verb, "stop")) {
    if (!validId(rest)) return error("id: 1-14 chars A-Z 0-9 - _");
    c.cmd = verb[1] == 'e' ? Cmd::Session : Cmd::Stop;
    copyText(c.arg, rest, sizeof c.arg);
  } else if (!strcmp(verb, "h")) {
    char* end;
    c.value = strtof(rest, &end);
    if (*end || !isfinite(c.value) || c.value < 0.0f || c.value > 9.99f) return error("h: 0-9.99 m");
    c.cmd = Cmd::Height;
  } else if (!strcmp(verb, "mode")) {
    static const char* names[] = {"static", "sweep", "reference", "drift"};
    c.a = -1;
    for (int i = 0; i < 4; ++i)
      if (!strcmp(rest, names[i])) c.a = i;
    if (c.a < 0) return error("mode: static|sweep|reference|drift");
    c.cmd = Cmd::Mode;
  } else if (!strcmp(verb, "dump") || !strcmp(verb, "erase")) {
    char* name = strtok(rest, " ");
    char* conf = strtok(nullptr, " ");
    copyText(c.arg, name, sizeof c.arg);
    c.cmd = verb[0] == 'd' ? Cmd::Dump : Cmd::Erase;
    c.confirmed = conf && !strcmp(conf, "confirm");
    if (c.cmd == Cmd::Erase && !c.confirmed) return error("erase <file|all> confirm");
  } else if (!strcmp(verb, "window")) {
    c.cmd = Cmd::Window;
    if (!strcmp(rest, "off")) return c;  // confirmed = false means disabled
    char* dash = strchr(rest, '-');
    if (!dash) return error("window HH:MM-HH:MM | off");
    *dash = '\0';
    if (!parseHhMm(rest, &c.a) || !parseHhMm(dash + 1, &c.b) || c.a == c.b) return error("window HH:MM-HH:MM | off");
    c.confirmed = true;
  } else if (!strcmp(verb, "latency") || !strcmp(verb, "id")) {
    char* end;
    const long v = strtol(rest, &end, 10);
    const bool lat = verb[0] == 'l';
    if (*end || (lat && (v < -999 || v > 999)) || (!lat && (v < 1 || v > 254)))
      return error(lat ? "latency: -999..999 ms" : "id: 1-254");
    c.cmd = lat ? Cmd::Latency : Cmd::NodeId;
    c.a = static_cast<int>(v);
  } else if (!strcmp(verb, "marker")) {
    c.cmd = Cmd::Marker;
    copyText(c.arg, rest, 23);
  } else {
    return error("unknown command; try help");
  }
  return c;
}

}  // namespace core

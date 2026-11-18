#include "app.h"

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

#include "board.h"
#include "button.h"
#include "config.h"
#include "display.h"
#include "gnss.h"
#include "oplink.h"
#include "radio.h"
#include "record.h"
#include "schedule.h"
#include "storage.h"

namespace app {

static settings::S st;
static core::ButtonGestures gestures;
static uint32_t lastButtonMs = 0, lastDisplayMs = 0, ledOffAt = 0, noticeUntil = 0;
static char notice[12] = "";

settings::S& cfgRef() { return st; }

void say(const char* fmt, ...) {
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  oplink::println(line);
}

void logRecord(const uint8_t* rec) { storage::append(rec); }

void flashLed(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
  board::led(r, g, b);
  ledOffAt = millis() + ms;
}

void writeMarker(const char* text) {
  core::MarkerRec m{};
  const int64_t utc = gnss::timeValid() ? gnss::utcMs() : 0;
  m.cycle = utc ? core::slotAt(utc).cycle : 0;
  m.utcS = static_cast<uint32_t>(utc / 1000);
  core::copyText(m.text, text, sizeof m.text);
  uint8_t rec[core::kRecordLen];
  m.encode(rec);
  logRecord(rec);
  flashLed(40, 40, 40, 300);
  say("MARK %lu %s", static_cast<unsigned long>(m.cycle), m.text);
}

// Starts a new log file; with force it rotates even if one is open (before dumping it).
static void openLog(bool force) {
  if ((storage::isOpen() && !force) || !gnss::timeValid()) return;
  core::HeaderRec h{};
  h.role = cfg::kRole;
  h.nodeId = st.nodeId;
  h.fwVersion = cfg::kFwVersion;
  h.freqHz = 869525000;
  h.bwHz100 = 1250;
  h.crDenom = cfg::kCrDenom;
  h.txPowerDbm = cfg::kTxPowerDbm;
  h.syncWord = cfg::kSyncWord;
  h.preamble = cfg::kPreamble;
  h.cycleS = core::kCycleMs / 1000;
  h.sfA = 7;
  h.sfB = 12;
  h.payloadLen = 20;
  h.utcS = static_cast<uint32_t>(gnss::utcMs() / 1000);
  core::copyText(h.fwTag, cfg::kFwTag, sizeof h.fwTag);
  if (storage::openNew(st.fileCounter, h)) {
    say("LOG %s", storage::currentName());
    ++st.fileCounter;
    settings::save(st);
    roleLogOpened();
  }
}

static void printStatus() {
  const core::GnssState g = gnss::state();
  say("%s node %u fw %s", cfg::kTower ? "TOWER" : "MOBILE", st.nodeId, cfg::kFwTag);
  say("time %s utc %lld fix %u sat %u hdop %.1f", gnss::timeFresh() ? "ok" : (gnss::timeValid() ? "HOLDOVER" : "NONE"),
      gnss::timeValid() ? gnss::utcMs() / 1000 : 0LL, gnss::fixCode(), g.nsat, g.hdop);
  say("log %s free %u kB%s dropped %lu latency %ld ms", storage::isOpen() ? storage::currentName() : "-",
      static_cast<unsigned>(storage::freeKb()), storage::full() ? " FULL" : "",
      static_cast<unsigned long>(storage::droppedRecords()), static_cast<long>(st.latencyMs));
  roleStatus();
}

static void printHelp() {
  say("status | files | dump <file> | erase <file|all> confirm | latency <ms> | id <n> | marker <text>");
  if (cfg::kTower) say("window HH:MM-HH:MM | window off   (UTC transmit window)");
  else say("session <id> | stop <id> | h <m> | mode static|sweep|reference|drift");
}

static void emitLine(const char* l) { oplink::println(l); }

static void handle(const core::Command& c) {
  switch (c.cmd) {
    case core::Cmd::None: return;
    case core::Cmd::Error: say("ERR %s", c.arg); return;
    case core::Cmd::Help: printHelp(); return;
    case core::Cmd::Status: printStatus(); return;
    case core::Cmd::Files: storage::listFiles(emitLine); say("OK files"); return;
    case core::Cmd::Dump:
      // Dumping the file being written: close it first, so a downloaded file never changes.
      if (storage::isOpen() && strcmp(c.arg, storage::currentName()) == 0) openLog(true);
      if (!oplink::startDump(c.arg)) say("ERR dump %s", c.arg);
      return;
    case core::Cmd::Erase: say(storage::erase(c.arg) ? "OK erase %s" : "ERR erase %s", c.arg); return;
    case core::Cmd::Latency:
      st.latencyMs = c.a;
      gnss::setLatencyMs(c.a);
      settings::save(st);
      say("OK latency %d", c.a);
      return;
    case core::Cmd::NodeId:
      st.nodeId = static_cast<uint8_t>(c.a);
      settings::save(st);
      say("OK id %d (display updates after restart)", c.a);
      return;
    case core::Cmd::Marker: writeMarker(c.arg); return;
    default:
      if (!roleCommand(c)) say("ERR not available on this node");
  }
}

static void serviceButton() {
  const uint32_t now = millis();
  if (now - lastButtonMs < 20) return;
  lastButtonMs = now;
  const core::Gesture g = gestures.update(board::buttonPressed(), now);
  if (g == core::Gesture::Triple) writeMarker("button");
  if (g == core::Gesture::Long) {
    snprintf(notice, sizeof notice, "WIFI:stage2");
    noticeUntil = now + 3000;
    say("WIFI action requested: Wi-Fi comes in stage 2");
  }
  const uint32_t held = gestures.heldMs(now);
  if (held >= core::ButtonGestures::kLongMinMs && held <= core::ButtonGestures::kLongMaxMs) {
    snprintf(notice, sizeof notice, "LET GO=WIFI");  // tell the operator to release now
    noticeUntil = now + 300;
  }
}

static void serviceDisplay() {
  const uint32_t now = millis();
  if (now - lastDisplayMs < 500 || !roleRadioQuiet()) return;
  lastDisplayMs = now;
  char lines[3][12] = {"", "", ""};
  roleDisplay(lines);
  if (storage::full()) snprintf(lines[2], sizeof lines[2], "LOG FULL");
  if (now < noticeUntil) snprintf(lines[2], sizeof lines[2], "%s", notice);
  display::show(lines[0], lines[1], lines[2]);
}

void setup() {
  st = settings::load();
  char name[24];
  snprintf(name, sizeof name, "LORA-%s-%u", cfg::kTower ? "TOWER" : "MOBILE", st.nodeId);
  oplink::begin(name);
  const bool boardOk = board::begin();
  display::begin(cfg::kTower, st.nodeId);
  const bool fsOk = storage::begin();
  gnss::begin(st.latencyMs);
  const bool radioOk = boardOk && radio::begin();
  if (!radioOk || !fsOk) {
    say("FATAL board %d radio %d fs %d", boardOk, radioOk, fsOk);
    display::show(radioOk ? "" : "RADIO ERR", fsOk ? "" : "FLASH ERR", "see serial");
    for (;;) {
      board::led(60, 0, 0);
      delay(500);
      board::led(0, 0, 0);
      delay(500);
    }
  }
  say("%s ready; type help", name);
  roleSetup();
}

void loop() {
  roleLoop();
  // Everything below may block for milliseconds (flash, NVS, USB), so it runs only when no
  // radio deadline is near; commands simply wait a few hundred ms in their queues.
  if (roleRadioQuiet()) {
    char line[96];
    if (oplink::readLine(line, sizeof line)) handle(core::parseCommand(line));
    openLog(false);
    oplink::service();
    storage::service();
  }
  serviceButton();
  serviceDisplay();
  if (ledOffAt && static_cast<int32_t>(millis() - ledOffAt) >= 0) {
    board::led(0, 0, 0);
    ledOffAt = 0;
  }
}

}  // namespace app

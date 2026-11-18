#include "app.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
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
static bool bootMarked = false;

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

// Starts a new log file when none is open and GNSS time is valid.
static void openLog() {
  if (storage::isOpen() || !gnss::timeValid()) return;
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
  uint32_t number = st.fileCounter;
  if (storage::openNew(&number, h)) {
    say("LOG %s", storage::currentName());
    st.fileCounter = number + 1;
    settings::save(st);
    roleLogOpened();
    if (!bootMarked) {  // a watchdog or brown-out reset shows up in the log, not as a silent gap
      bootMarked = true;
      char text[23];
      snprintf(text, sizeof text, "boot reset=%d", static_cast<int>(esp_reset_reason()));
      writeMarker(text);
    }
  }
}

// Erasing 5 MB of flash takes longer than the watchdog timeout.
static void formatLog() {
  disableLoopWDT();
  const bool ok = storage::format();
  enableLoopWDT();
  say(ok ? "OK format" : "ERR format");
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
  say("status | files | dump <file> | erase <file|all> confirm | format confirm | latency <ms> | id <n> | marker <text>");
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
      // Dumping the file being written: close it first, so a downloaded file never changes. This
      // works without GNSS time (indoors); the next file opens once time is valid.
      if (storage::isOpen() && strcmp(c.arg, storage::currentName()) == 0 && storage::close()) openLog();
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
    case core::Cmd::Format: formatLog(); return;
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
    if (!fsOk) say("log partition not mounted; download what you can, then: format confirm");
    display::show(radioOk ? "" : "RADIO ERR", fsOk ? "" : "FLASH ERR", "see serial");
    for (;;) {  // not watched by the watchdog: waits for the operator
      board::led(60, 0, 0);
      delay(500);
      board::led(0, 0, 0);
      delay(500);
      char line[96];
      if (!fsOk && oplink::readLine(line, sizeof line) && core::parseCommand(line).cmd == core::Cmd::Format) {
        say(storage::format() ? "OK format, restarting" : "ERR format");
        delay(100);
        ESP.restart();
      }
    }
  }
  say("%s ready; type help", name);
  roleSetup();
  // A hung loop (e.g. the SX1262 BUSY wait inside RadioLib) resets the board after 8 s instead of
  // stopping the node for good. The core starts the task watchdog with panic enabled.
  const esp_task_wdt_config_t wdt = {.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  enableLoopWDT();
}

void loop() {
  // Before the role: the records of the first slot after GNSS time becomes valid need an open file.
  openLog();
  roleLoop();
  // Everything below may block for milliseconds (flash, NVS, USB), so it runs only when no
  // radio deadline is near; commands simply wait a few hundred ms in their queues.
  if (roleRadioQuiet()) {
    char line[96];
    if (oplink::readLine(line, sizeof line)) handle(core::parseCommand(line));
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

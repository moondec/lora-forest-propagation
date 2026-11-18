// Mobile node: listens in each scheduled window and writes exactly one record per slot.
// A lost packet is recorded only for a window that was monitored from before its transmit
// time; any other slot gets a "not listened" record, so no slot disappears silently.
#ifdef ROLE_MOBILE

#include <Arduino.h>

#include "app.h"
#include "board.h"
#include "config.h"
#include "gnss.h"
#include "packet.h"
#include "radio.h"
#include "record.h"
#include "schedule.h"

namespace app {

constexpr int64_t kLateOpenMs = 100;  // later than this before the transmit time: not monitored
constexpr int kMaxGapRecords = 16;    // slots skipped by a stalled loop that get their own record

struct Window {
  bool active = false, listening = false;
  core::Slot slot{};
  uint64_t key = 0;
  uint16_t preambles = 0, headers = 0;
  bool received = false, crcErr = false, foreign = false;
  radio::Status st{};
  core::Packet pkt{};
};

static Window win;
static core::ContextRec ctx{};
static bool yellow = false;
static char lastResult[12] = "";
static uint32_t slots = 0, rxCount = 0;
static const char* kModes[] = {"static", "sweep", "reference", "drift"};

// Slots in time order; never goes backwards, so a small step of the clock cannot reopen a slot.
static uint64_t keyOf(const core::Slot& s) { return static_cast<uint64_t>(s.cycle) * 2 + s.slot; }
static core::Slot slotOfKey(uint64_t k) {
  return core::slotAt(core::txTimeMs(static_cast<uint32_t>(k / 2), static_cast<int>(k % 2)));
}

static void logContext() {
  uint8_t rec[core::kRecordLen];
  ctx.encode(rec);
  logRecord(rec);
}

void roleSetup() {
  const settings::S& c = cfgRef();
  ctx.mode = c.mode < 4 ? c.mode : core::kModeStatic;
  ctx.heightCm = c.heightCm;
  core::copyText(ctx.stopId, c.stop[0] ? c.stop : "none", sizeof ctx.stopId);
  core::copyText(ctx.sessionId, c.session[0] ? c.session : "none", sizeof ctx.sessionId);
}

void roleLogOpened() { logContext(); }

static int16_t clamp16(float v) {
  return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : lroundf(v)));
}

static void writeRecord(const Window& w, uint8_t extraFlags) {
  core::RxSlotRec r{};
  r.sf = core::sfOf(w.slot.slot);
  r.cycle = w.slot.cycle;
  r.flags = extraFlags | (w.received ? core::kFlagReceived | core::kFlagCrcOk : 0) |
            (w.foreign ? core::kFlagForeign : 0) | (w.crcErr && !w.received ? core::kFlagCrcErr : 0) |
            (w.listening ? 0 : core::kFlagNotListened) | (gnss::timeFresh() ? 0 : core::kFlagHoldover);
  const int extra = w.preambles > w.headers ? w.preambles - w.headers : 0;
  r.preambleOnly = static_cast<uint8_t>(extra > 255 ? 255 : extra);
  if (w.received || w.crcErr) {  // radio fields also kept for a CRC-failed frame (crc_ok = 0)
    r.rssiPkt10 = clamp16(w.st.rssiPkt * 10);
    r.snr100 = clamp16(w.st.snr * 100);
    r.signalRssi10 = clamp16(w.st.signalRssi * 10);
    r.freqErrHz = clamp16(static_cast<float>(w.st.freqErrHz));
  }
  const core::GnssState g = gnss::state();
  r.lat1e7 = static_cast<int32_t>(lround(g.lat * 1e7));
  r.lon1e7 = static_cast<int32_t>(lround(g.lon * 1e7));
  r.hdop10 = static_cast<uint8_t>(g.hdop * 10 > 255 ? 255 : g.hdop * 10);  // 255 = no HDOP
  r.nsat = g.nsat;
  r.tempC = board::tempC();
  if (w.received) {
    r.txFix = w.pkt.fix;
    r.txSeq = w.pkt.seq;
    r.txTempC = w.pkt.tempC;
  }
  r.rxFix = gnss::fixCode();
  uint8_t rec[core::kRecordLen];
  r.encode(rec);
  logRecord(rec);
  ++slots;
  if (w.received) ++rxCount;
  if (!w.listening) return;  // not-listened slots are logged but not shown as results

  char line[96];
  core::formatRxCsv(r, ctx, line, sizeof line);
  say("%s", line);
  if (w.received) {
    snprintf(lastResult, sizeof lastResult, "%u:%.1f", r.sf, w.st.rssiPkt);
    flashLed(0, 50, 0, 150);
  } else {
    snprintf(lastResult, sizeof lastResult, "%u:LOST", r.sf);
    flashLed(50, 0, 0, 150);
  }
}

static void serviceRadio() {
  if (!win.listening) return;
  const radio::Event e = radio::poll();
  if (e.preamble) ++win.preambles;
  if (e.headerValid) ++win.headers;
  if (!e.rxDone) return;
  uint8_t buf[64];
  const size_t n = radio::read(buf, sizeof buf);
  const radio::Status st = radio::packetStatus();
  if (e.crcErr) {
    if (!win.received) {
      win.crcErr = true;
      win.st = st;
    }
    return;
  }
  core::Packet p{};
  if (!win.received && core::decodePacket(buf, n, &p) &&
      core::matchesSlot(p, win.slot.cycle, core::sfOf(win.slot.slot))) {
    win.received = true;
    win.st = st;
    win.pkt = p;
  } else if (!win.received) {
    win.foreign = true;  // valid LoRa CRC but not our packet for this slot
  }
}

// Starts listening for slot s (or marks it as not monitored if it is already too late).
static void openWindow(const core::Slot& s) {
  win = Window{};
  win.active = true;
  win.slot = s;
  win.key = keyOf(s);
  const int64_t now = gnss::utcMs();  // fresh: the previous record may have taken time
  if (core::txTimeMs(s.cycle, s.slot) - now < kLateOpenMs) {
    radio::standby();
    return;
  }
  win.listening = radio::setSf(core::sfOf(s.slot)) && radio::startRx();
}

void roleLoop() {
  if (!gnss::timeValid()) {  // beyond the 600 s holdover: no schedule, no records
    if (win.active) {
      serviceRadio();
      radio::standby();
      writeRecord(win, 0);
      win.active = false;
    }
    if (!yellow) board::led(30, 30, 0);
    yellow = true;
    return;
  }
  if (yellow) {
    board::led(0, 0, 0);
    yellow = false;
  }
  const core::Slot s = core::slotAt(gnss::utcMs());
  const uint64_t key = keyOf(s);
  if (!win.active) {
    openWindow(s);
  } else if (key > win.key) {
    serviceRadio();  // a packet may be waiting in the radio buffer: read it before closing
    const Window done = win;
    openWindow(s);  // receive the next slot first, then do the slower logging
    writeRecord(done, core::kFlagTimeValid);
    // Slots skipped entirely (the loop stalled) still get a record each.
    for (uint64_t k = done.key + 1, n = 0; k < key && n < kMaxGapRecords; ++k, ++n) {
      Window gap{};
      gap.slot = slotOfKey(k);
      writeRecord(gap, core::kFlagTimeValid);
    }
  }
  serviceRadio();
}

bool roleCommand(const core::Command& c) {
  settings::S& st = cfgRef();
  switch (c.cmd) {
    case core::Cmd::Session: core::copyText(ctx.sessionId, c.arg, sizeof ctx.sessionId); break;
    case core::Cmd::Stop: core::copyText(ctx.stopId, c.arg, sizeof ctx.stopId); break;
    case core::Cmd::Height: ctx.heightCm = static_cast<uint16_t>(lroundf(c.value * 100)); break;
    case core::Cmd::Mode: ctx.mode = static_cast<uint8_t>(c.a); break;
    default: return false;
  }
  core::copyText(st.session, ctx.sessionId, sizeof st.session);
  core::copyText(st.stop, ctx.stopId, sizeof st.stop);
  st.heightCm = ctx.heightCm;
  st.mode = ctx.mode;
  settings::save(st);  // survives a restart; the log gets the context again in roleLogOpened()
  logContext();
  say("OK session %s stop %s h %.2f mode %s", ctx.sessionId, ctx.stopId, ctx.heightCm / 100.0, kModes[ctx.mode & 3]);
  return true;
}

void roleStatus() {
  say("session %s stop %s h %.2f mode %s", ctx.sessionId, ctx.stopId, ctx.heightCm / 100.0, kModes[ctx.mode & 3]);
  say("slots %lu received %lu last %s", static_cast<unsigned long>(slots), static_cast<unsigned long>(rxCount),
      lastResult);
}

void roleDisplay(char l[3][12]) {
  snprintf(l[0], 12, "%s", ctx.stopId);
  if (gnss::timeFresh()) snprintf(l[1], 12, "h%.1f GPS%u", ctx.heightCm / 100.0, gnss::state().nsat);
  else if (gnss::timeValid()) snprintf(l[1], 12, "h%.1f HOLD", ctx.heightCm / 100.0);
  else snprintf(l[1], 12, "h%.1f NOTIME", ctx.heightCm / 100.0);
  snprintf(l[2], 12, "%s", lastResult);
}

bool roleRadioQuiet() {
  if (!gnss::timeValid()) return true;
  const int64_t now = gnss::utcMs();
  const core::Slot s = core::slotAt(now);
  // Stay clear of window boundaries, where a record is written and the SF is switched.
  return now - s.startMs > 100 && s.endMs - now > 100;
}

}  // namespace app

#endif  // ROLE_MOBILE

// Mobile node: listens in each scheduled window and writes exactly one record per slot.
// A lost packet is recorded only for a window that was monitored from before its transmit
// time; any other slot gets a "not listened" record, so no slot disappears silently.
#ifdef ROLE_MOBILE

#include <Arduino.h>
#include <esp_timer.h>

#include "airtime.h"
#include "app.h"
#include "board.h"
#include "config.h"
#include "context.h"
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
// Context changes wait for the open window to close, so they never relabel its packet.
static core::ContextGate gate;
static bool yellow = false;
static char lastResult[12] = "";
static uint32_t slots = 0, rxCount = 0;
static uint64_t lastFinalKey = 0;   // highest slot already recorded: never recorded twice
static int64_t airUs[2] = {0, 0};  // SF7, SF12 time on air of the tower packet
static const char* kModes[] = {"static", "sweep", "reference", "drift"};

// Slots in time order; never goes backwards, so a small step of the clock cannot reopen a slot.
static uint64_t keyOf(const core::Slot& s) { return static_cast<uint64_t>(s.cycle) * 2 + s.slot; }
static core::Slot slotOfKey(uint64_t k) {
  return core::slotAt(core::txTimeMs(static_cast<uint32_t>(k / 2), static_cast<int>(k % 2)));
}

static void logContext() {
  uint8_t rec[core::kRecordLen];
  gate.current().encode(rec);
  logRecord(rec);
}

static void sayContext(const char* prefix, const core::ContextRec& c) {
  say("%ssession %s stop %s h %.2f mode %s", prefix, c.sessionId, c.stopId, c.heightCm / 100.0, kModes[c.mode & 3]);
}

// After the closing window's record is written: a deferred context change takes effect now.
static void applyPendingContext() {
  core::ContextRec c{};
  if (!gate.take(&c)) return;
  logContext();
  sayContext("CTX ", c);
}

void roleSetup() {
  const settings::S& s = cfgRef();
  core::ContextRec c{};
  c.mode = s.mode < 4 ? s.mode : core::kModeStatic;
  c.heightCm = s.heightCm;
  core::copyText(c.stopId, s.stop[0] ? s.stop : "none", sizeof c.stopId);
  core::copyText(c.sessionId, s.session[0] ? s.session : "none", sizeof c.sessionId);
  gate = core::ContextGate(c);
  airUs[0] = llround(core::loraAirtimeMs(7, 125000, cfg::kCrDenom, cfg::kPreamble, 20, true, true, false) * 1000);
  airUs[1] = llround(core::loraAirtimeMs(12, 125000, cfg::kCrDenom, cfg::kPreamble, 20, true, true, true) * 1000);
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
  if (keyOf(w.slot) > lastFinalKey) lastFinalKey = keyOf(w.slot);
  if (w.received) ++rxCount;
  if (!w.listening) return;  // not-listened slots are logged but not shown as results

  char line[96];
  core::formatRxCsv(r, gate.current(), line, sizeof line);
  say("%s", line);
  if (w.received) {
    snprintf(lastResult, sizeof lastResult, "%u:%.1f", r.sf, w.st.rssiPkt);
    flashLed(0, 50, 0, 150);
  } else {
    snprintf(lastResult, sizeof lastResult, "%u:LOST", r.sf);
    flashLed(50, 0, 0, 150);
  }
}

// Companion record for each frame in our format: arrival time against the schedule (inter-node
// clock offset) and the packet's own cycle (a UTC disagreement shows as cycle_diff != 0).
static void logFrame(const radio::Event& e, const radio::Status& st, const core::Packet& p, bool matched) {
  core::RxFrameRec f{};
  f.sf = core::sfOf(win.slot.slot);
  f.cycle = win.slot.cycle;
  f.rxCycle = p.cycle;
  f.rxSf = p.sf;
  f.flags = (matched ? core::kFrameMatched : 0) | (e.rxDoneEdge ? core::kFrameTimingValid : 0);
  const int64_t edgeUtcUs = gnss::utcUs() - (esp_timer_get_time() - e.edgeUs);
  const int64_t schedEndUs = core::txTimeMs(win.slot.cycle, win.slot.slot) * 1000 + airUs[win.slot.slot];
  const int64_t d = edgeUtcUs - schedEndUs;
  f.arrivalUs = static_cast<int32_t>(d > INT32_MAX ? INT32_MAX : (d < INT32_MIN ? INT32_MIN : d));
  f.rssiPkt10 = clamp16(st.rssiPkt * 10);
  f.snr100 = clamp16(st.snr * 100);
  f.txNode = p.nodeId;
  f.txSeq = p.seq;
  uint8_t rec[core::kRecordLen];
  f.encode(rec);
  logRecord(rec);
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
  const bool ours = core::decodePacket(buf, n, &p);
  const bool matched = ours && core::matchesSlot(p, win.slot.cycle, core::sfOf(win.slot.slot));
  if (ours) logFrame(e, st, p, matched);
  if (matched && !win.received) {
    win.received = true;
    win.st = st;
    win.pkt = p;
  } else if (!matched) {
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
      writeRecord(win, gate.changedDuringWindow() ? core::kFlagContextChanged : 0);
      win.active = false;
    }
    applyPendingContext();
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
    if (key > lastFinalKey) openWindow(s);  // e.g. time returned inside a slot already recorded
  } else if (key > win.key) {
    serviceRadio();  // a packet may be waiting in the radio buffer: read it before closing
    const Window done = win;
    const uint8_t ctxFlag = gate.changedDuringWindow() ? core::kFlagContextChanged : 0;
    openWindow(s);  // receive the next slot first, then do the slower logging
    writeRecord(done, core::kFlagTimeValid | ctxFlag);
    applyPendingContext();
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
  core::ContextRec ctx = gate.requested();
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
  if (gate.set(ctx, win.active)) {
    logContext();
    sayContext("OK ", ctx);
  } else {
    sayContext("OK from next slot: ", ctx);
  }
  return true;
}

void roleStatus() {
  const core::ContextRec& ctx = gate.requested();
  sayContext("", ctx);
  say("slots %lu received %lu last %s", static_cast<unsigned long>(slots), static_cast<unsigned long>(rxCount),
      lastResult);
}

void roleDisplay(char l[3][12]) {
  const core::ContextRec& ctx = gate.requested();
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

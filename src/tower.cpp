// Tower node: transmits SF7 at +0 s and SF12 at +1 s of every 15 s GNSS cycle and logs each slot.
#ifdef ROLE_TOWER

#include <Arduino.h>

#include "airtime.h"
#include "app.h"
#include "board.h"
#include "config.h"
#include "gnss.h"
#include "packet.h"
#include "radio.h"
#include "record.h"
#include "schedule.h"

namespace app {

static core::DutyGuard duty;
static uint16_t seq = 0;
static uint32_t sentCount = 0;
static bool txBusy = false;
static int64_t txDeadlineMs = 0;
static core::TxRec pending{};       // record of the transmission in progress
static uint64_t lastKey = 0;       // last slot handled, as cycle*2 + slot (never goes backwards)
static uint32_t airMs[2] = {0, 0};  // SF7, SF12
static bool yellow = false;

static int32_t lat1e7(double v) { return static_cast<int32_t>(lround(v * 1e7)); }

static void fillCommon(core::TxRec& r, uint32_t cycle, int slot) {
  const core::GnssState g = gnss::state();
  r.sf = core::sfOf(slot);
  r.cycle = cycle;
  r.fix = gnss::fixCode();
  r.tempC = board::tempC();
  r.nsat = g.nsat;
  r.hdop100 = static_cast<uint16_t>(g.hdop * 100 > 65535 ? 65535 : g.hdop * 100);
  r.lat1e7 = lat1e7(g.lat);
  r.lon1e7 = lat1e7(g.lon);
}

static void logSkip(uint32_t cycle, int slot, uint8_t status) {
  core::TxRec r{};
  fillCommon(r, cycle, slot);
  r.status = status;
  uint8_t rec[core::kRecordLen];
  r.encode(rec);
  logRecord(rec);
}

void roleLogOpened() {}

void roleSetup() {
  airMs[0] = static_cast<uint32_t>(ceil(core::loraAirtimeMs(7, 125000, cfg::kCrDenom, cfg::kPreamble, 20, true, true, false)));
  airMs[1] = static_cast<uint32_t>(ceil(core::loraAirtimeMs(12, 125000, cfg::kCrDenom, cfg::kPreamble, 20, true, true, true)));
}

static void transmit(uint32_t cycle, int slot, int64_t txAt) {
  const uint8_t sf = core::sfOf(slot);
  core::Packet p{};
  p.version = core::kPacketVersion;
  p.nodeId = cfgRef().nodeId;
  p.sf = sf;
  p.cycle = cycle;
  const core::GnssState g = gnss::state();
  p.fix = gnss::fixCode();
  p.nsat = g.nsat;
  p.hdop100 = static_cast<uint16_t>(g.hdop * 100 > 65535 ? 65535 : g.hdop * 100);
  p.tempC = board::tempC();
  p.supplyMv = 0;  // not measurable on the Unit C6L
  p.fwVersion = cfg::kFwVersion;
  p.seq = seq;
  uint8_t buf[core::kPacketLen];
  core::encodePacket(p, buf);

  pending = core::TxRec{};
  fillCommon(pending, cycle, slot);
  pending.seq = seq;
  if (!radio::setSf(sf)) {
    logSkip(cycle, slot, core::kTxRadioError);
    return;
  }
  // Busy-wait to the scheduled millisecond. The offset is taken after SetTx has been issued;
  // the TCXO start and PA ramp-up inside the SX1262 (a few ms) come on top of it.
  while (gnss::utcMs() < txAt) {
  }
  if (!radio::startTx(buf, sizeof buf)) {
    logSkip(cycle, slot, core::kTxRadioError);
    return;
  }
  pending.txOffsetUs = static_cast<int32_t>(gnss::utcUs() - txAt * 1000);
  pending.status = core::kTxSent;
  duty.record(txAt, airMs[slot]);
  ++seq;
  txBusy = true;
  txDeadlineMs = txAt + airMs[slot] + 500;
  board::led(0, 0, 60);
}

void roleLoop() {
  if (txBusy) {
    const radio::Event e = radio::poll();
    const bool timedOut = gnss::timeValid() && gnss::utcMs() > txDeadlineMs;
    if (e.txDone || timedOut) {
      radio::finishTx();
      if (timedOut && !e.txDone) pending.status = core::kTxRadioError;
      uint8_t rec[core::kRecordLen];
      pending.encode(rec);
      logRecord(rec);
      if (pending.status == core::kTxSent) ++sentCount;
      txBusy = false;
      board::led(0, 0, 0);
    }
    return;
  }
  if (!gnss::timeValid()) {
    if (!yellow) board::led(30, 30, 0);  // yellow: no GNSS time, nothing is transmitted
    yellow = true;
    return;
  }
  if (yellow) {
    board::led(0, 0, 0);
    yellow = false;
  }
  const int64_t now = gnss::utcMs();
  const core::Slot s = core::slotAt(now);
  const uint64_t key = static_cast<uint64_t>(s.cycle) * 2 + s.slot;
  if (key <= lastKey) return;  // this slot is done (also after a small backward clock step)
  const int64_t txAt = core::txTimeMs(s.cycle, s.slot);
  if (now > txAt) {  // missed: the loop was busy or time just became valid
    lastKey = key;
    logSkip(s.cycle, s.slot, core::kTxLate);
    return;
  }
  // Prepare 30 ms ahead so a short stall elsewhere in the loop does not cost the slot.
  if (txAt - now > 30) return;
  lastKey = key;
  const settings::S& c = cfgRef();
  if (!core::inTxWindow(txAt / 1000, c.winStartMin, c.winEndMin, c.windowOn)) {
    logSkip(s.cycle, s.slot, core::kTxOutsideWindow);
  } else if (!duty.allow(txAt, airMs[s.slot])) {
    logSkip(s.cycle, s.slot, core::kTxDutyGuard);
  } else {
    transmit(s.cycle, s.slot, txAt);
  }
}

bool roleCommand(const core::Command& c) {
  if (c.cmd != core::Cmd::Window) return false;
  settings::S& s = cfgRef();
  s.windowOn = c.confirmed;
  s.winStartMin = static_cast<int16_t>(c.a);
  s.winEndMin = static_cast<int16_t>(c.b);
  settings::save(s);
  if (s.windowOn) say("OK window %02d:%02d-%02d:%02d UTC", c.a / 60, c.a % 60, c.b / 60, c.b % 60);
  else say("OK window off (transmit whenever time is valid)");
  return true;
}

void roleStatus() {
  const int64_t now = gnss::timeValid() ? gnss::utcMs() : 0;
  const settings::S& c = cfgRef();
  say("sent %lu seq %u duty %.1f %% window %s", static_cast<unsigned long>(sentCount), seq,
      now ? duty.usedMs(now) / 36000.0 : 0.0, c.windowOn ? "on" : "off");
}

void roleDisplay(char l[3][12]) {
  const core::GnssState g = gnss::state();
  if (gnss::timeValid()) snprintf(l[0], 12, "GPS ok %us", g.nsat);
  else snprintf(l[0], 12, "NO TIME");
  snprintf(l[1], 12, "TX %lu", static_cast<unsigned long>(sentCount));
  const settings::S& c = cfgRef();
  snprintf(l[2], 12, "WIN %s", c.windowOn ? "on" : "off");
}

bool roleRadioQuiet() {
  if (txBusy || !gnss::timeValid()) return !txBusy;
  const int64_t now = gnss::utcMs();
  const core::Slot s = core::slotAt(now);
  const int64_t txAt = core::txTimeMs(s.cycle, s.slot);
  return txAt - now > 200 || now > txAt;
}

}  // namespace app

#endif  // ROLE_TOWER

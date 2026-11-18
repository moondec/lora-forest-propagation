#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_timer.h>

#include "config.h"

namespace radio {

// Reset goes through the I/O expander (board::begin), hence RADIOLIB_NC.
static Module mod(cfg::kPinNss, cfg::kPinDio1, RADIOLIB_NC, cfg::kPinBusy, SPI);
static SX1262 sx(&mod);
static volatile bool dio1Flag = false;
static volatile int64_t dio1EdgeUs = 0;

static void IRAM_ATTR onDio1() {
  dio1EdgeUs = esp_timer_get_time();  // ISR-safe
  dio1Flag = true;
}

static int64_t edgeTime() {  // 64-bit value written by the ISR: read until two reads agree
  int64_t a, b;
  do {
    a = dio1EdgeUs;
    b = dio1EdgeUs;
  } while (a != b);
  return a;
}

bool begin() {
  int16_t st = sx.begin(cfg::kFreqMHz, cfg::kBwKHz, 7, cfg::kCrDenom, cfg::kSyncWord, cfg::kTxPowerDbm,
                        cfg::kPreamble, cfg::kTcxoV, false);
  if (st != RADIOLIB_ERR_NONE) return false;
  st = sx.setDio2AsRfSwitch(true);
  st |= sx.setCRC(2);
  st |= sx.explicitHeader();
  st |= sx.autoLDRO();  // LDRO on for SF12 (symbol >= 16.38 ms), off for SF7
  // Boosted RX gain, set explicitly: it fixes the sensitivity, and so the censoring threshold.
  st |= sx.setRxBoostedGainMode(true);
  sx.setDio1Action(onDio1);
  return st == RADIOLIB_ERR_NONE;
}

bool setSf(uint8_t sf) {
  sx.standby();
  return sx.setSpreadingFactor(sf) == RADIOLIB_ERR_NONE;
}

bool startTx(const uint8_t* d, size_t n) {
  dio1Flag = false;
  return sx.startTransmit(d, n) == RADIOLIB_ERR_NONE;
}

void finishTx() { sx.finishTransmit(); }

bool startRx() {
  dio1Flag = false;
  const RadioLibIrqFlags_t flags = (1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_CRC_ERR) |
                                   (1UL << RADIOLIB_IRQ_HEADER_VALID) | (1UL << RADIOLIB_IRQ_HEADER_ERR) |
                                   (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
  return sx.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, flags, flags, 0) == RADIOLIB_ERR_NONE;
}

void standby() { sx.standby(); }

Event poll() {
  Event e{};
  // DIO1 stays high while any routed flag is set, so also check the level: an edge
  // that arrived while flags were pending would otherwise be missed for good.
  if (!dio1Flag && digitalRead(cfg::kPinDio1) == LOW) return e;
  dio1Flag = false;
  const uint32_t irq = sx.getIrqFlags();
  e.txDone = irq & RADIOLIB_SX126X_IRQ_TX_DONE;
  e.rxDone = irq & RADIOLIB_SX126X_IRQ_RX_DONE;
  e.crcErr = irq & RADIOLIB_SX126X_IRQ_CRC_ERR;
  e.headerValid = irq & RADIOLIB_SX126X_IRQ_HEADER_VALID;
  e.headerErr = irq & RADIOLIB_SX126X_IRQ_HEADER_ERR;
  e.preamble = irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
  e.edgeUs = edgeTime();
  e.rxDoneEdge = e.rxDone && !e.headerValid && !e.preamble && !e.headerErr;
  // With RX_DONE pending, readData() checks the CRC/header bits and clears everything itself;
  // otherwise clear the informational flags now so DIO1 can rise again for the next event.
  if (!e.rxDone && !e.txDone && irq) sx.clearIrqFlags(irq);
  return e;
}

size_t read(uint8_t* buf, size_t max) {
  const size_t len = sx.getPacketLength();
  // Never pass 0: RadioLib reads the whole packet then, whatever its length. The data are read
  // even when it reports a CRC mismatch.
  sx.readData(buf, max);
  return len < max ? len : max;
}

Status packetStatus() {
  uint8_t b[3] = {0, 0, 0};
  mod.SPIreadStream(RADIOLIB_SX126X_CMD_GET_PACKET_STATUS, b, 3);
  Status s;
  s.rssiPkt = -static_cast<float>(b[0]) / 2.0f;
  s.snr = static_cast<float>(static_cast<int8_t>(b[1])) / 4.0f;
  s.signalRssi = -static_cast<float>(b[2]) / 2.0f;
  s.freqErrHz = static_cast<int32_t>(lroundf(sx.getFrequencyError()));
  return s;
}

}  // namespace radio

// SX1262 wrapper: fixed experiment settings, SF switching, interrupt-driven TX/RX, packet status.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace radio {

struct Event {
  bool rxDone, crcErr, headerValid, headerErr, preamble, txDone;
  // Local time (esp_timer, us) of the last DIO1 rising edge. It marks RX_DONE only if no other
  // routed flag was still pending (rxDoneEdge): otherwise DIO1 was already high.
  int64_t edgeUs;
  bool rxDoneEdge;
};

struct Status {
  float rssiPkt, snr, signalRssi;  // SX1262 GetPacketStatus: RssiPkt, SnrPkt, SignalRssiPkt
  int32_t freqErrHz;
};

bool begin();
bool setSf(uint8_t sf);
bool startTx(const uint8_t* d, size_t n);
void finishTx();
bool startRx();  // continuous receive, all RX interrupts routed to DIO1
void standby();
// Reads and clears pending interrupt flags (RX_DONE is left for read()).
Event poll();
// Reads the received frame and clears all flags; returns its length. Judge the CRC from the
// Event that reported rxDone: readData() can flag a good frame as bad when a later noise header
// error arrives before the frame is read.
size_t read(uint8_t* buf, size_t max);
Status packetStatus();

}  // namespace radio

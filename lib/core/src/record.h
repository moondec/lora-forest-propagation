// 32-byte binary log records stored on LittleFS; scripts/lora_logs.py decodes the same layout.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

constexpr size_t kRecordLen = 32;
enum RecordType : uint8_t { kRecRxSlot = 1, kRecTx = 2, kRecMarker = 3, kRecContext = 5, kRecHeader = 6 };

// RxSlot flag bits
constexpr uint8_t kFlagReceived = 0x01;   // our packet, matching cycle and SF
constexpr uint8_t kFlagCrcOk = 0x02;      // LoRa CRC passed
constexpr uint8_t kFlagForeign = 0x04;    // a CRC-valid frame arrived that was not ours or not for this slot
constexpr uint8_t kFlagTimeValid = 0x08;  // GNSS time valid when the window closed
constexpr uint8_t kFlagCrcErr = 0x10;     // a frame with a LoRa CRC error arrived (radio fields kept)
constexpr uint8_t kFlagNotListened = 0x20;  // window not monitored (opened late or skipped): no evidence of loss
constexpr uint8_t kFlagHoldover = 0x40;     // time from holdover (no valid RMC for > 10 s, < 600 s)

struct RxSlotRec {
  uint8_t sf;
  uint32_t cycle;
  uint8_t flags, preambleOnly;
  int16_t rssiPkt10, snr100, signalRssi10, freqErrHz;
  int32_t lat1e7, lon1e7;
  uint8_t hdop10, nsat;
  int8_t tempC;
  uint8_t txFix;
  uint16_t txSeq;
  int8_t txTempC;
  uint8_t rxFix;
  void encode(uint8_t out[kRecordLen]) const;
  static bool decode(const uint8_t* d, RxSlotRec* r);
};

// kTxNoTime is reserved: without GNSS time the cycle is unknown, so no record can be written.
enum TxStatus : uint8_t { kTxSent = 0, kTxNoTime = 1, kTxOutsideWindow = 2, kTxDutyGuard = 3, kTxRadioError = 4, kTxLate = 5 };

struct TxRec {
  uint8_t sf;
  uint32_t cycle;
  uint8_t status, fix;
  int32_t txOffsetUs;  // actual transmit start minus scheduled time
  int8_t tempC;
  uint8_t nsat;
  uint16_t hdop100, seq;
  int32_t lat1e7, lon1e7;
  void encode(uint8_t out[kRecordLen]) const;
  static bool decode(const uint8_t* d, TxRec* r);
};

struct MarkerRec {
  uint32_t cycle, utcS;
  char text[23];  // 22 chars + NUL
  void encode(uint8_t out[kRecordLen]) const;
  static bool decode(const uint8_t* d, MarkerRec* r);
};

enum Mode : uint8_t { kModeStatic = 0, kModeSweep = 1, kModeReference = 2, kModeDrift = 3 };

struct ContextRec {
  uint8_t mode;
  uint16_t heightCm;
  char stopId[15], sessionId[15];  // 14 chars + NUL
  void encode(uint8_t out[kRecordLen]) const;
  static bool decode(const uint8_t* d, ContextRec* r);
};

struct HeaderRec {
  uint8_t role, nodeId, fwVersion;  // role 1 = tower, 2 = mobile
  uint32_t freqHz;
  uint16_t bwHz100;
  uint8_t crDenom;
  int8_t txPowerDbm;
  uint16_t syncWord, preamble;
  uint8_t cycleS, sfA, sfB, payloadLen;
  uint32_t utcS;
  char fwTag[9];  // 8 chars + NUL
  void encode(uint8_t out[kRecordLen]) const;
  static bool decode(const uint8_t* d, HeaderRec* r);
};

// Copies at most `cap - 1` chars and NUL-terminates.
void copyText(char* dst, const char* src, size_t cap);

// Live line for serial/BLE: stop,h,cycle,sf,received,rssi,snr,signal_rssi (radio fields empty if lost).
size_t formatRxCsv(const RxSlotRec& r, const ContextRec& c, char* buf, size_t n);

}  // namespace core

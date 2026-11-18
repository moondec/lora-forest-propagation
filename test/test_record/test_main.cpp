#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "record.h"

using namespace core;

void setUp() {}
void tearDown() {}

// Same bytes as GOLDEN_RX_HEX in tests/test_lora_logs.py: keeps firmware and converter in step.
static const char* kGoldenRxHex = "010cf8ca26070b029bfbd3fa64fb2efb88487e1fe2e6af09090bf90201020302";

static RxSlotRec goldenRx() {
  RxSlotRec r{};
  r.sf = 12;
  r.cycle = 119982840;
  r.flags = kFlagReceived | kFlagCrcOk | kFlagTimeValid;
  r.preambleOnly = 2;
  r.rssiPkt10 = -1125;
  r.snr100 = -1325;
  r.signalRssi10 = -1180;
  r.freqErrHz = -1234;
  r.lat1e7 = 528369800;
  r.lon1e7 = 162522850;
  r.hdop10 = 9;
  r.nsat = 11;
  r.tempC = -7;
  r.txFix = 2;
  r.txSeq = 513;
  r.txTempC = 3;
  r.rxFix = 2;
  return r;
}

static void toHex(const uint8_t* b, size_t n, char* out) {
  for (size_t i = 0; i < n; ++i) snprintf(out + 2 * i, 3, "%02x", b[i]);
}

void test_rx_record_golden() {
  uint8_t b[kRecordLen];
  goldenRx().encode(b);
  char hex[2 * kRecordLen + 1];
  toHex(b, kRecordLen, hex);
  TEST_ASSERT_EQUAL_STRING(kGoldenRxHex, hex);
}

void test_all_record_roundtrips() {
  uint8_t b[kRecordLen];
  RxSlotRec rx = goldenRx(), rx2{};
  rx.encode(b);
  TEST_ASSERT_TRUE(RxSlotRec::decode(b, &rx2));
  TEST_ASSERT_EQUAL_INT16(-1180, rx2.signalRssi10);
  TEST_ASSERT_EQUAL_INT32(162522850, rx2.lon1e7);

  TxRec tx{}, tx2{};
  tx.sf = 7; tx.cycle = 5; tx.status = kTxDutyGuard; tx.fix = 2; tx.txOffsetUs = -1520;
  tx.tempC = -12; tx.nsat = 9; tx.hdop100 = 120; tx.seq = 65535; tx.lat1e7 = -1; tx.lon1e7 = 7;
  tx.encode(b);
  TEST_ASSERT_FALSE(RxSlotRec::decode(b, &rx2));
  TEST_ASSERT_TRUE(TxRec::decode(b, &tx2));
  TEST_ASSERT_EQUAL_INT32(-1520, tx2.txOffsetUs);
  TEST_ASSERT_EQUAL_UINT8(kTxDutyGuard, tx2.status);
  TEST_ASSERT_EQUAL_INT8(-12, tx2.tempC);

  MarkerRec m{}, m2{};
  m.cycle = 9; m.utcS = 1799742600;
  copyText(m.text, "button", sizeof m.text);
  m.encode(b);
  TEST_ASSERT_TRUE(MarkerRec::decode(b, &m2));
  TEST_ASSERT_EQUAL_STRING("button", m2.text);

  HeaderRec h{}, h2{};
  h.role = 2; h.nodeId = 2; h.fwVersion = 1; h.freqHz = 869525000; h.bwHz100 = 1250; h.crDenom = 5;
  h.txPowerDbm = 14; h.syncWord = 0x52; h.preamble = 8; h.cycleS = 15; h.sfA = 7; h.sfB = 12;
  h.payloadLen = 20; h.utcS = 1799742600;
  copyText(h.fwTag, "1.0.0", sizeof h.fwTag);
  h.encode(b);
  TEST_ASSERT_TRUE(HeaderRec::decode(b, &h2));
  TEST_ASSERT_EQUAL_UINT32(869525000, h2.freqHz);
  TEST_ASSERT_EQUAL_STRING("1.0.0", h2.fwTag);
}

void test_context_truncates_long_ids() {
  ContextRec c{}, c2{};
  c.mode = kModeSweep;
  c.heightCm = 130;
  copyText(c.stopId, "ME-T1-100-XXXXXXXX", sizeof c.stopId);
  copyText(c.sessionId, "W1_ME_20270112", sizeof c.sessionId);
  uint8_t b[kRecordLen];
  c.encode(b);
  TEST_ASSERT_TRUE(ContextRec::decode(b, &c2));
  TEST_ASSERT_EQUAL_STRING("ME-T1-100-XXXX", c2.stopId);  // 14 chars kept
  TEST_ASSERT_EQUAL_STRING("W1_ME_20270112", c2.sessionId);
  TEST_ASSERT_EQUAL_UINT16(130, c2.heightCm);
}

void test_live_csv_received_and_lost() {
  ContextRec c{};
  copyText(c.stopId, "ME-T1-100", sizeof c.stopId);
  c.heightCm = 130;
  char buf[96];
  RxSlotRec r = goldenRx();
  formatRxCsv(r, c, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("ME-T1-100,1.3,119982840,12,1,-112.5,-13.25,-118.0", buf);
  r.flags = kFlagTimeValid;
  formatRxCsv(r, c, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("ME-T1-100,1.3,119982840,12,0,,,", buf);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rx_record_golden);
  RUN_TEST(test_all_record_roundtrips);
  RUN_TEST(test_context_truncates_long_ids);
  RUN_TEST(test_live_csv_received_and_lost);
  return UNITY_END();
}

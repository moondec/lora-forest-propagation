#include <string.h>
#include <unity.h>

#include "airtime.h"
#include "packet.h"

using namespace core;

void setUp() {}
void tearDown() {}

static Packet sample() {
  Packet p{};
  p.version = 1;
  p.nodeId = 1;
  p.sf = 12;
  p.cycle = 119982840;
  p.fix = 2;
  p.nsat = 14;
  p.hdop100 = 87;
  p.tempC = -7;
  p.supplyMv = 0;
  p.fwVersion = 1;
  p.seq = 513;
  return p;
}

void test_crc16_check_value() {
  const char* s = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16Ccitt(reinterpret_cast<const uint8_t*>(s), 9));
}

void test_crc32_matches_zlib() {
  const uint8_t* s = reinterpret_cast<const uint8_t*>("123456789");
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32Ieee(0, s, 9));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32Ieee(crc32Ieee(0, s, 4), s + 4, 5));  // chunked
}

void test_packet_roundtrip() {
  uint8_t buf[kPacketLen];
  encodePacket(sample(), buf);
  TEST_ASSERT_EQUAL_HEX8(kMagic, buf[0]);
  Packet q{};
  TEST_ASSERT_TRUE(decodePacket(buf, kPacketLen, &q));
  TEST_ASSERT_EQUAL_UINT32(119982840, q.cycle);
  TEST_ASSERT_EQUAL_UINT8(12, q.sf);
  TEST_ASSERT_EQUAL_INT8(-7, q.tempC);
  TEST_ASSERT_EQUAL_UINT16(87, q.hdop100);
  TEST_ASSERT_EQUAL_UINT16(513, q.seq);
}

void test_packet_layout_little_endian() {
  uint8_t buf[kPacketLen];
  encodePacket(sample(), buf);
  // cycle 119982840 = 0x0726CAF8 at bytes 4..7, little-endian
  TEST_ASSERT_EQUAL_HEX8(0xF8, buf[4]);
  TEST_ASSERT_EQUAL_HEX8(0xCA, buf[5]);
  TEST_ASSERT_EQUAL_HEX8(0x26, buf[6]);
  TEST_ASSERT_EQUAL_HEX8(0x07, buf[7]);
  const uint16_t crc = crc16Ccitt(buf, 18);
  TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, buf[18]);
  TEST_ASSERT_EQUAL_HEX8(crc >> 8, buf[19]);
}

void test_decode_rejects_bad_crc() {
  uint8_t buf[kPacketLen];
  encodePacket(sample(), buf);
  buf[5] ^= 0x01;
  Packet q{};
  TEST_ASSERT_FALSE(decodePacket(buf, kPacketLen, &q));
}

void test_decode_rejects_wrong_magic_and_length() {
  uint8_t buf[kPacketLen];
  Packet p = sample();
  encodePacket(p, buf);
  Packet q{};
  TEST_ASSERT_FALSE(decodePacket(buf, kPacketLen - 1, &q));
  buf[0] = 0x00;
  const uint16_t crc = crc16Ccitt(buf, 18);
  buf[18] = crc & 0xFF;
  buf[19] = crc >> 8;
  TEST_ASSERT_FALSE(decodePacket(buf, kPacketLen, &q));
}

void test_match_rejects_wrong_cycle() {
  Packet p = sample();
  TEST_ASSERT_TRUE(matchesSlot(p, 119982840, 12));
  TEST_ASSERT_FALSE(matchesSlot(p, 119982841, 12));
  TEST_ASSERT_FALSE(matchesSlot(p, 119982840, 7));
}

void test_airtime_sf7() {
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 56.6, loraAirtimeMs(7, 125000, 5, 8, 20, true, true, false));
}

void test_airtime_sf12() {
  TEST_ASSERT_DOUBLE_WITHIN(0.5, 1318.9, loraAirtimeMs(12, 125000, 5, 8, 20, true, true, true));
}

void test_duty_guard_budget() {
  DutyGuard g;
  int64_t t = 0;
  // One hour of the real schedule: 240 cycles, 57 + 1319 ms each = 9.2 %.
  for (int i = 0; i < 240; ++i) {
    TEST_ASSERT_TRUE(g.allow(t, 57));
    g.record(t, 57);
    TEST_ASSERT_TRUE(g.allow(t + 1000, 1319));
    g.record(t + 1000, 1319);
    t += 15000;
  }
  TEST_ASSERT_TRUE(g.usedMs(t) <= 360000);
  // Doubling the airtime inside the same hour must hit the 10 % budget.
  bool refused = false;
  for (int i = 0; i < 300 && !refused; ++i) {
    if (!g.allow(t, 1400)) refused = true;
    else g.record(t, 1400);
  }
  TEST_ASSERT_TRUE(refused);
  // Entries older than one hour expire.
  TEST_ASSERT_EQUAL_UINT32(0, g.usedMs(t + 3600000 + 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_crc32_matches_zlib);
  RUN_TEST(test_packet_roundtrip);
  RUN_TEST(test_packet_layout_little_endian);
  RUN_TEST(test_decode_rejects_bad_crc);
  RUN_TEST(test_decode_rejects_wrong_magic_and_length);
  RUN_TEST(test_match_rejects_wrong_cycle);
  RUN_TEST(test_airtime_sf7);
  RUN_TEST(test_airtime_sf12);
  RUN_TEST(test_duty_guard_budget);
  return UNITY_END();
}

#include <unity.h>

#include "schedule.h"

using namespace core;

void setUp() {}
void tearDown() {}

void test_slot_sf7_window() {
  Slot s = slotAt(15000LL * 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, s.cycle);
  TEST_ASSERT_EQUAL_INT(0, s.slot);
  TEST_ASSERT_EQUAL_INT64(14999600, s.startMs);
  TEST_ASSERT_EQUAL_INT64(15000700, s.endMs);
  TEST_ASSERT_EQUAL_UINT8(7, sfOf(s.slot));
}

void test_slot_sf7_window_starts_before_cycle() {
  // 300 ms before the cycle boundary already belongs to the next cycle's SF7 window.
  Slot s = slotAt(15000LL * 1000 - 300);
  TEST_ASSERT_EQUAL_UINT32(1000, s.cycle);
  TEST_ASSERT_EQUAL_INT(0, s.slot);
}

void test_slot_sf12_window() {
  Slot s = slotAt(15000LL * 1000 + 800);
  TEST_ASSERT_EQUAL_UINT32(1000, s.cycle);
  TEST_ASSERT_EQUAL_INT(1, s.slot);
  TEST_ASSERT_EQUAL_INT64(15000700, s.startMs);
  TEST_ASSERT_EQUAL_INT64(15014600, s.endMs);
  TEST_ASSERT_EQUAL_UINT8(12, sfOf(s.slot));
}

void test_windows_tile_across_midnight() {
  // 1 Jan 2027 00:00:00 UTC = 1798761600 s; walk windows from 23:59:59 the day before.
  int64_t t = (1798761600LL - 1) * 1000;
  Slot prev = slotAt(t);
  for (int i = 0; i < 20; ++i) {
    Slot next = slotAt(prev.endMs);
    TEST_ASSERT_EQUAL_INT64(prev.endMs, next.startMs);
    if (prev.slot == 0) {
      TEST_ASSERT_EQUAL_UINT32(prev.cycle, next.cycle);
      TEST_ASSERT_EQUAL_INT(1, next.slot);
    } else {
      TEST_ASSERT_EQUAL_UINT32(prev.cycle + 1, next.cycle);
      TEST_ASSERT_EQUAL_INT(0, next.slot);
    }
    prev = next;
  }
}

void test_tx_time() {
  TEST_ASSERT_EQUAL_INT64(15000000, txTimeMs(1000, 0));
  TEST_ASSERT_EQUAL_INT64(15001000, txTimeMs(1000, 1));
}

void test_tx_inside_its_window() {
  for (int slot = 0; slot < 2; ++slot) {
    int64_t tx = txTimeMs(1000, slot);
    Slot s = slotAt(tx);
    TEST_ASSERT_EQUAL_UINT32(1000, s.cycle);
    TEST_ASSERT_EQUAL_INT(slot, s.slot);
  }
}

void test_tx_window_wraps() {
  const int64_t day = 1798761600LL;  // midnight UTC
  TEST_ASSERT_TRUE(inTxWindow(day + 23 * 3600 + 1800, 22 * 60, 2 * 60, true));
  TEST_ASSERT_TRUE(inTxWindow(day + 1 * 3600, 22 * 60, 2 * 60, true));
  TEST_ASSERT_FALSE(inTxWindow(day + 12 * 3600, 22 * 60, 2 * 60, true));
  TEST_ASSERT_TRUE(inTxWindow(day + 12 * 3600, 7 * 60, 17 * 60, true));
  TEST_ASSERT_FALSE(inTxWindow(day + 17 * 3600, 7 * 60, 17 * 60, true));  // end exclusive
  TEST_ASSERT_TRUE(inTxWindow(day + 12 * 3600, 22 * 60, 2 * 60, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_sf7_window);
  RUN_TEST(test_slot_sf7_window_starts_before_cycle);
  RUN_TEST(test_slot_sf12_window);
  RUN_TEST(test_windows_tile_across_midnight);
  RUN_TEST(test_tx_time);
  RUN_TEST(test_tx_inside_its_window);
  RUN_TEST(test_tx_window_wraps);
  return UNITY_END();
}

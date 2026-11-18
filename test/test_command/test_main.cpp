#include <unity.h>

#include "button.h"
#include "command.h"

using namespace core;

void setUp() {}
void tearDown() {}

void test_parse_stop_and_session() {
  Command c = parseCommand("stop ME-T1-100\r\n");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Stop);
  TEST_ASSERT_EQUAL_STRING("ME-T1-100", c.arg);
  c = parseCommand("  session W1_ME_20270112");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Session);
  TEST_ASSERT_EQUAL_STRING("W1_ME_20270112", c.arg);
}

void test_parse_rejects_long_or_odd_stop() {
  TEST_ASSERT_TRUE(parseCommand("stop ME-T1-100-EXTRA-LONG").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("stop ME T1").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("stop").cmd == Cmd::Error);
}

void test_parse_height_range() {
  Command c = parseCommand("h 1.3");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Height);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.3f, c.value);
  TEST_ASSERT_TRUE(parseCommand("h 12").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("h -1").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("h abc").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("h nan").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("h inf").cmd == Cmd::Error);
}

void test_parse_mode() {
  Command c = parseCommand("mode sweep");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Mode);
  TEST_ASSERT_EQUAL_INT(1, c.a);
  TEST_ASSERT_TRUE(parseCommand("mode fast").cmd == Cmd::Error);
}

void test_parse_window_and_off() {
  Command c = parseCommand("window 07:00-17:30");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Window);
  TEST_ASSERT_TRUE(c.confirmed);
  TEST_ASSERT_EQUAL_INT(420, c.a);
  TEST_ASSERT_EQUAL_INT(1050, c.b);
  c = parseCommand("window off");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Window);
  TEST_ASSERT_FALSE(c.confirmed);
  TEST_ASSERT_TRUE(parseCommand("window 25:00-17:00").cmd == Cmd::Error);
}

void test_parse_erase_needs_confirm() {
  TEST_ASSERT_TRUE(parseCommand("erase L0001.bin").cmd == Cmd::Error);
  Command c = parseCommand("erase all confirm");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Erase);
  TEST_ASSERT_EQUAL_STRING("all", c.arg);
}

void test_parse_format_needs_confirm() {
  TEST_ASSERT_TRUE(parseCommand("format").cmd == Cmd::Error);
  TEST_ASSERT_TRUE(parseCommand("format now").cmd == Cmd::Error);
  Command c = parseCommand("format confirm");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Format);
  TEST_ASSERT_TRUE(c.confirmed);
}

void test_parse_misc() {
  TEST_ASSERT_TRUE(parseCommand("status").cmd == Cmd::Status);
  TEST_ASSERT_TRUE(parseCommand("").cmd == Cmd::None);
  TEST_ASSERT_TRUE(parseCommand("frobnicate 3").cmd == Cmd::Error);
  Command c = parseCommand("latency -120");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Latency);
  TEST_ASSERT_EQUAL_INT(-120, c.a);
  TEST_ASSERT_TRUE(parseCommand("id 0").cmd == Cmd::Error);
  c = parseCommand("marker tree fell near pole and more text");
  TEST_ASSERT_TRUE(c.cmd == Cmd::Marker);
  TEST_ASSERT_EQUAL_STRING("tree fell near pole an", c.arg);  // 22 chars
}

// Helpers for the gesture tests: press for `held` ms starting at t, return the gesture on release.
static Gesture press(ButtonGestures& b, uint32_t& t, uint32_t held) {
  b.update(true, t);
  Gesture g = Gesture::None;
  for (uint32_t s = 20; s < held; s += 20) g = b.update(true, t + s);
  t += held;
  Gesture r = b.update(false, t);
  return r != Gesture::None ? r : g;
}

static Gesture idle(ButtonGestures& b, uint32_t& t, uint32_t ms) {
  Gesture g = Gesture::None;
  for (uint32_t s = 20; s <= ms; s += 20) {
    Gesture x = b.update(false, t + s);
    if (x != Gesture::None) g = x;
  }
  t += ms;
  return g;
}

void test_triple_press() {
  ButtonGestures b;
  uint32_t t = 1000;
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(press(b, t, 150) == Gesture::None);
    if (i < 2) idle(b, t, 200);
  }
  TEST_ASSERT_TRUE(idle(b, t, 800) == Gesture::Triple);
}

void test_two_or_four_presses_nothing() {
  ButtonGestures b;
  uint32_t t = 0;
  press(b, t, 150); idle(b, t, 200); press(b, t, 150);
  TEST_ASSERT_TRUE(idle(b, t, 1000) == Gesture::None);
  for (int i = 0; i < 4; ++i) { press(b, t, 150); idle(b, t, 200); }
  TEST_ASSERT_TRUE(idle(b, t, 1000) == Gesture::None);
}

void test_long_press_7s() {
  ButtonGestures b;
  uint32_t t = 0;
  TEST_ASSERT_TRUE(press(b, t, 7000) == Gesture::Long);
  TEST_ASSERT_TRUE(idle(b, t, 1000) == Gesture::None);
}

void test_hold_12s_ignored_and_3s_nothing() {
  ButtonGestures b;
  uint32_t t = 0;
  TEST_ASSERT_TRUE(press(b, t, 12000) == Gesture::None);
  TEST_ASSERT_TRUE(press(b, t, 3000) == Gesture::None);
  TEST_ASSERT_TRUE(idle(b, t, 1000) == Gesture::None);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_stop_and_session);
  RUN_TEST(test_parse_rejects_long_or_odd_stop);
  RUN_TEST(test_parse_height_range);
  RUN_TEST(test_parse_mode);
  RUN_TEST(test_parse_window_and_off);
  RUN_TEST(test_parse_erase_needs_confirm);
  RUN_TEST(test_parse_format_needs_confirm);
  RUN_TEST(test_parse_misc);
  RUN_TEST(test_triple_press);
  RUN_TEST(test_two_or_four_presses_nothing);
  RUN_TEST(test_long_press_7s);
  RUN_TEST(test_hold_12s_ignored_and_3s_nothing);
  return UNITY_END();
}

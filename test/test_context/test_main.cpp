#include <string.h>
#include <unity.h>

#include "context.h"

using namespace core;

void setUp() {}
void tearDown() {}

static ContextRec ctxAt(const char* stop, uint16_t heightCm) {
  ContextRec c{};
  copyText(c.stopId, stop, sizeof c.stopId);
  copyText(c.sessionId, "W1", sizeof c.sessionId);
  c.heightCm = heightCm;
  return c;
}

void test_change_without_window_applies_now() {
  ContextGate g(ctxAt("ME-T1-100", 30));
  TEST_ASSERT_TRUE(g.set(ctxAt("ME-T1-100", 130), false));
  TEST_ASSERT_EQUAL_UINT16(130, g.current().heightCm);
  TEST_ASSERT_FALSE(g.changedDuringWindow());
  ContextRec out{};
  TEST_ASSERT_FALSE(g.take(&out));  // nothing left pending
}

void test_change_during_window_is_deferred_and_flags_it() {
  ContextGate g(ctxAt("ME-T1-100", 30));
  TEST_ASSERT_FALSE(g.set(ctxAt("ME-T1-100", 130), true));
  // The open window keeps the context it was measured under.
  TEST_ASSERT_EQUAL_UINT16(30, g.current().heightCm);
  TEST_ASSERT_EQUAL_UINT16(130, g.requested().heightCm);
  TEST_ASSERT_TRUE(g.changedDuringWindow());
  ContextRec out{};
  TEST_ASSERT_TRUE(g.take(&out));
  TEST_ASSERT_EQUAL_UINT16(130, out.heightCm);
  TEST_ASSERT_EQUAL_UINT16(130, g.current().heightCm);
  TEST_ASSERT_FALSE(g.changedDuringWindow());
}

void test_several_changes_in_one_window_merge() {
  ContextGate g(ctxAt("ME-T1-100", 30));
  ContextRec c = g.requested();
  copyText(c.stopId, "ME-T1-150", sizeof c.stopId);
  g.set(c, true);
  c = g.requested();  // builds on the pending stop, not on the current one
  c.heightCm = 300;
  g.set(c, true);
  ContextRec out{};
  TEST_ASSERT_TRUE(g.take(&out));
  TEST_ASSERT_EQUAL_STRING("ME-T1-150", out.stopId);
  TEST_ASSERT_EQUAL_UINT16(300, out.heightCm);
  TEST_ASSERT_FALSE(g.take(&out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_change_without_window_applies_now);
  RUN_TEST(test_change_during_window_is_deferred_and_flags_it);
  RUN_TEST(test_several_changes_in_one_window_merge);
  return UNITY_END();
}

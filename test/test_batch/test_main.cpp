#include <unity.h>

#include "batch.h"
#include "record.h"

using namespace core;

void setUp() {}
void tearDown() {}

void test_nothing_written_yet() { TEST_ASSERT_EQUAL_INT(0, committedRecords(320, 320, 16)); }

void test_short_write_left_whole_records() {
  // 5 of 16 records reached the file before the write failed: resume at record 5.
  TEST_ASSERT_EQUAL_INT(5, committedRecords(320 + 5 * kRecordLen, 320, 16));
}

void test_partial_record_is_not_counted() {
  TEST_ASSERT_EQUAL_INT(5, committedRecords(320 + 5 * kRecordLen + 7, 320, 16));
}

void test_never_more_than_pending() { TEST_ASSERT_EQUAL_INT(16, committedRecords(320 + 40 * kRecordLen, 320, 16)); }

void test_file_shorter_than_batch_start() {
  // The file lost bytes (should not happen): rewrite the whole batch rather than skip records.
  TEST_ASSERT_EQUAL_INT(0, committedRecords(100, 320, 16));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_written_yet);
  RUN_TEST(test_short_write_left_whole_records);
  RUN_TEST(test_partial_record_is_not_counted);
  RUN_TEST(test_never_more_than_pending);
  RUN_TEST(test_file_shorter_than_batch_start);
  return UNITY_END();
}

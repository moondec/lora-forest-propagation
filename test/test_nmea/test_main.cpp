#include <string.h>
#include <unity.h>

#include "nmea.h"
#include "timebase.h"

using namespace core;

void setUp() {}
void tearDown() {}

static const char* kRmc = "$GNRMC,083000.000,A,5250.2188,N,01615.1371,E,0.10,45.00,120127,,,A*4A\r\n";
static const char* kGga = "$GNGGA,083000.000,5250.2188,N,01615.1371,E,1,14,0.87,85.3,M,34.1,M,,*4D\r\n";
static const char* kRmcVoid = "$GNRMC,083001.000,V,,,,,,,120127,,,N*5E\r\n";

// Feeds a string; returns how many valid RMC lines completed. The first char gets localUs.
static int feedAll(NmeaParser& p, const char* s, int64_t localUs) {
  int n = 0;
  for (const char* c = s; *c; ++c) n += p.feed(*c, localUs) ? 1 : 0;
  return n;
}

void test_unix_from_date() { TEST_ASSERT_EQUAL_INT64(1799742600LL, unixFromDate(2027, 1, 12, 8, 30, 0)); }

void test_unix_from_date_rollover() {
  TEST_ASSERT_EQUAL_INT64(unixFromDate(2026, 12, 31, 23, 59, 59) + 1, unixFromDate(2027, 1, 1, 0, 0, 0));
  TEST_ASSERT_EQUAL_INT64(unixFromDate(2028, 2, 28, 23, 59, 59) + 1, unixFromDate(2028, 2, 29, 0, 0, 0));
}

void test_nmea_rmc_valid() {
  NmeaParser p;
  TEST_ASSERT_EQUAL_INT(1, feedAll(p, kRmc, 5000000));
  const GnssState& s = p.state();
  TEST_ASSERT_TRUE(s.rmcValid);
  TEST_ASSERT_EQUAL_INT64(1799742600LL, s.utcS);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 52.83698, s.lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 16.252285, s.lon);
  TEST_ASSERT_EQUAL_INT64(5000000, p.rmcMarkUs());
}

void test_nmea_gga_fields() {
  NmeaParser p;
  TEST_ASSERT_EQUAL_INT(0, feedAll(p, kGga, 0));
  TEST_ASSERT_EQUAL_UINT8(1, p.state().quality);
  TEST_ASSERT_EQUAL_UINT8(14, p.state().nsat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.87f, p.state().hdop);
}

void test_nmea_rejects_bad_checksum() {
  NmeaParser p;
  char bad[128];
  strcpy(bad, kRmc);
  bad[20] = '1';  // corrupt a digit, keep the checksum
  TEST_ASSERT_EQUAL_INT(0, feedAll(p, bad, 0));
  TEST_ASSERT_FALSE(p.state().rmcValid);
}

void test_nmea_rmc_void_no_time() {
  NmeaParser p;
  feedAll(p, kRmc, 0);
  TEST_ASSERT_EQUAL_INT(0, feedAll(p, kRmcVoid, 1000000));
  TEST_ASSERT_FALSE(p.state().rmcValid);
  TEST_ASSERT_EQUAL_INT64(0, p.rmcMarkUs());  // mark of the last valid RMC is kept
}

void test_nmea_truncated_line_ignored() {
  NmeaParser p;
  TEST_ASSERT_EQUAL_INT(0, feedAll(p, "$GNRMC,083000.000,A,5250.21", 0));
  TEST_ASSERT_EQUAL_INT(1, feedAll(p, kRmc, 100000));  // a new '$' resets the line
  TEST_ASSERT_EQUAL_INT64(100000, p.rmcMarkUs());
}

void test_nmea_mark_is_time_of_dollar() {
  NmeaParser p;
  int64_t t = 100;
  int n = 0;
  for (const char* c = kRmc; *c; ++c) n += p.feed(*c, t++) ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT64(100, p.rmcMarkUs());
}

void test_nmea_mark_is_start_of_burst() {
  // GGA then RMC back to back: the RMC is stamped with the burst start, not its own '$',
  // so the mark does not move with the number of sentences before RMC.
  NmeaParser p;
  int64_t t = 1000000;
  for (const char* c = kGga; *c; ++c) p.feed(*c, t += 87);
  const int64_t burst = 1000000 + 87;
  int n = 0;
  for (const char* c = kRmc; *c; ++c) n += p.feed(*c, t += 87) ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT64(burst, p.rmcMarkUs());
  // After a pause of 900 ms the next burst starts fresh.
  t += 900000;
  const int64_t burst2 = t + 87;
  for (const char* c = kRmc; *c; ++c) p.feed(*c, t += 87);
  TEST_ASSERT_EQUAL_INT64(burst2, p.rmcMarkUs());
}

void test_timebase_utc_ms() {
  TimeBase tb;
  TEST_ASSERT_FALSE(tb.valid(0));
  tb.discipline(1000, 5000000, 0);
  TEST_ASSERT_TRUE(tb.valid(5250000));
  TEST_ASSERT_EQUAL_INT64(1000250, tb.utcMs(5250000));
  tb.discipline(1000, 5000000, 120);  // NMEA latency: the second began 120 ms before the '$'
  TEST_ASSERT_EQUAL_INT64(1000370, tb.utcMs(5250000));
  TEST_ASSERT_EQUAL_INT64(1000370123LL, tb.utcUs(5250123));
}

void test_timebase_fresh_10s_holdover_600s() {
  TimeBase tb;
  tb.discipline(1000, 5000000, 0);
  TEST_ASSERT_TRUE(tb.fresh(15000000));
  TEST_ASSERT_FALSE(tb.fresh(15000001));
  TEST_ASSERT_TRUE(tb.valid(605000000));
  TEST_ASSERT_FALSE(tb.valid(605000001));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unix_from_date);
  RUN_TEST(test_unix_from_date_rollover);
  RUN_TEST(test_nmea_rmc_valid);
  RUN_TEST(test_nmea_gga_fields);
  RUN_TEST(test_nmea_rejects_bad_checksum);
  RUN_TEST(test_nmea_rmc_void_no_time);
  RUN_TEST(test_nmea_truncated_line_ignored);
  RUN_TEST(test_nmea_mark_is_time_of_dollar);
  RUN_TEST(test_nmea_mark_is_start_of_burst);
  RUN_TEST(test_timebase_utc_ms);
  RUN_TEST(test_timebase_fresh_10s_holdover_600s);
  return UNITY_END();
}

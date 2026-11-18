#include "gnss.h"

#include <Arduino.h>
#include <esp_timer.h>

#include "config.h"
#include "timebase.h"

namespace gnss {

static core::NmeaParser parser;
static core::TimeBase tb;
static core::GnssState snap;
static int32_t latency = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
// One UART character (10 bits) at 115200 baud.
constexpr int64_t kCharUs = 10 * 1000000LL / cfg::kGnssBaud;

// Runs in the UART event task. Bytes are timestamped backwards from the moment the callback
// runs, assuming the last byte has just arrived; the bench-calibrated latency absorbs the rest.
static void onUart() {
  const int64_t now = esp_timer_get_time();
  int avail = Serial1.available();
  while (avail > 0) {
    const int c = Serial1.read();
    if (c < 0) break;
    --avail;
    const int64_t t = now - static_cast<int64_t>(avail) * kCharUs;
    if (parser.feed(static_cast<char>(c), t)) {
      portENTER_CRITICAL(&mux);
      tb.discipline(parser.state().utcS, parser.rmcMarkUs(), latency);
      snap = parser.state();
      portEXIT_CRITICAL(&mux);
    } else if (c == '\n') {
      portENTER_CRITICAL(&mux);
      snap = parser.state();  // GGA updates quality, satellites, HDOP
      portEXIT_CRITICAL(&mux);
    }
  }
}

void begin(int32_t latencyMs) {
  latency = latencyMs;
  Serial1.setRxBufferSize(1024);
  Serial1.begin(cfg::kGnssBaud, SERIAL_8N1, cfg::kPinGnssRx, cfg::kPinGnssTx);
  Serial1.setRxTimeout(2);
  Serial1.onReceive(onUart, false);
}

void setLatencyMs(int32_t latencyMs) {
  portENTER_CRITICAL(&mux);
  latency = latencyMs;
  portEXIT_CRITICAL(&mux);
}

core::GnssState state() {
  portENTER_CRITICAL(&mux);
  core::GnssState s = snap;
  portEXIT_CRITICAL(&mux);
  return s;
}

bool timeValid() {
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  const bool v = tb.valid(now);
  portEXIT_CRITICAL(&mux);
  return v;
}

bool timeFresh() {
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  const bool v = tb.fresh(now) && snap.rmcValid;  // a void RMC (fix lost) ends "fresh" at once
  portEXIT_CRITICAL(&mux);
  return v;
}

int64_t utcMs() {
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  const int64_t t = tb.utcMs(now);
  portEXIT_CRITICAL(&mux);
  return t;
}

int64_t utcUs() {
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  const int64_t t = tb.utcUs(now);
  portEXIT_CRITICAL(&mux);
  return t;
}

uint8_t fixCode() {
  if (!timeFresh()) return 0;  // holdover: time is kept but there is no current fix
  return state().quality > 0 ? 2 : 1;
}

}  // namespace gnss

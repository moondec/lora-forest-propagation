// Button gestures: three quick presses (marker) and a 5-10 s hold (Wi-Fi action).
#pragma once

#include <stdint.h>

namespace core {

enum class Gesture { None, Triple, Long };

class ButtonGestures {
 public:
  // Call every ~20 ms with the debounced button state.
  Gesture update(bool pressed, uint32_t nowMs);
  // Duration of the current hold (0 if released), for on-screen feedback.
  uint32_t heldMs(uint32_t nowMs) const;

  static constexpr uint32_t kMaxPressMs = 600, kMaxGapMs = 600, kTripleSettleMs = 700;
  static constexpr uint32_t kLongMinMs = 5000, kLongMaxMs = 10000;

 private:
  bool down_ = false;
  uint32_t downAt_ = 0, upAt_ = 0;
  int shortCount_ = 0;
};

}  // namespace core

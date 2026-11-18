#include "button.h"

namespace core {

Gesture ButtonGestures::update(bool pressed, uint32_t nowMs) {
  if (pressed && !down_) {
    down_ = true;
    if (shortCount_ > 0 && nowMs - upAt_ > kMaxGapMs) shortCount_ = 0;  // too slow: new sequence
    downAt_ = nowMs;
    return Gesture::None;
  }
  if (!pressed && down_) {
    down_ = false;
    upAt_ = nowMs;
    const uint32_t held = nowMs - downAt_;
    if (held >= kLongMinMs) {
      shortCount_ = 0;
      return held <= kLongMaxMs ? Gesture::Long : Gesture::None;  // > 10 s: ignored
    }
    shortCount_ = held <= kMaxPressMs ? shortCount_ + 1 : 0;
    return Gesture::None;
  }
  // Released and idle: decide a triple once no further press follows.
  if (!down_ && shortCount_ > 0 && nowMs - upAt_ >= kTripleSettleMs) {
    const bool triple = shortCount_ == 3;
    shortCount_ = 0;
    return triple ? Gesture::Triple : Gesture::None;
  }
  return Gesture::None;
}

uint32_t ButtonGestures::heldMs(uint32_t nowMs) const { return down_ ? nowMs - downAt_ : 0; }

}  // namespace core

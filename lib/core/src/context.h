// Operator context (session, stop, height, mode) and when a change takes effect.
// A change that arrives while a receive window is open would relabel a packet already received
// at the old position (the record is written when the window closes), so it is deferred.
#pragma once

#include "record.h"

namespace core {

class ContextGate {
 public:
  explicit ContextGate(const ContextRec& initial = ContextRec{}) : current_(initial), next_(initial) {}
  // Returns true if the change took effect now (no window open); otherwise it waits for take().
  bool set(const ContextRec& c, bool windowOpen);
  // True while a change is waiting for the open window to close: that window gets flagged.
  bool changedDuringWindow() const { return pending_; }
  // After the window's record is written: applies a pending change; false if there was none.
  bool take(ContextRec* out);
  const ContextRec& current() const { return current_; }
  // The context that commands should build on (pending change included).
  const ContextRec& requested() const { return pending_ ? next_ : current_; }

 private:
  ContextRec current_, next_;
  bool pending_ = false;
};

}  // namespace core

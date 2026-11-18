#include "context.h"

namespace core {

bool ContextGate::set(const ContextRec& c, bool windowOpen) {
  next_ = c;
  if (windowOpen) {
    pending_ = true;
    return false;
  }
  current_ = c;
  pending_ = false;
  return true;
}

bool ContextGate::take(ContextRec* out) {
  if (!pending_) return false;
  current_ = next_;
  pending_ = false;
  *out = current_;
  return true;
}

}  // namespace core

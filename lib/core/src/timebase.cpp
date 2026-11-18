#include "timebase.h"

namespace core {

void TimeBase::discipline(int64_t utcS, int64_t markUs, int32_t latencyMs) {
  utcS_ = utcS;
  markUs_ = markUs;
  latencyMs_ = latencyMs;
  set_ = true;
}

bool TimeBase::fresh(int64_t localUs) const { return set_ && localUs - markUs_ <= kFreshUs; }
bool TimeBase::valid(int64_t localUs) const { return set_ && localUs - markUs_ <= kHoldoverUs; }

int64_t TimeBase::utcMs(int64_t localUs) const {
  return utcS_ * 1000 + (localUs - markUs_) / 1000 + latencyMs_;
}

int64_t TimeBase::utcUs(int64_t localUs) const {
  return utcS_ * 1000000 + (localUs - markUs_) + static_cast<int64_t>(latencyMs_) * 1000;
}

}  // namespace core

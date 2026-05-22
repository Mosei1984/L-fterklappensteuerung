#include "InputDebouncer.h"

namespace luefterklappe {
namespace {

bool inputsEqual(const DigitalInputs lhs, const DigitalInputs rhs) {
  return (lhs.minSwitchActive == rhs.minSwitchActive) &&
         (lhs.maxSwitchActive == rhs.maxSwitchActive) &&
         (lhs.stallGuardActive == rhs.stallGuardActive);
}

bool elapsed(const std::uint32_t nowMs, const std::uint32_t sinceMs,
             const std::uint32_t durationMs) {
  return static_cast<std::uint32_t>(nowMs - sinceMs) >= durationMs;
}

}  // namespace

InputDebouncer::InputDebouncer(const std::uint32_t debounceMs)
    : stable_{false, false, false},
      candidate_{false, false, false},
      candidateSinceMs_(0U),
      debounceMs_(debounceMs),
      initialized_(false) {}

DigitalInputs InputDebouncer::tick(const DigitalInputs raw,
                                   const std::uint32_t nowMs) {
  if (!initialized_) {
    stable_ = raw;
    candidate_ = raw;
    candidateSinceMs_ = nowMs;
    initialized_ = true;
    return stable_;
  }

  if (!inputsEqual(raw, candidate_)) {
    candidate_ = raw;
    candidateSinceMs_ = nowMs;
  }

  if (!inputsEqual(stable_, candidate_) &&
      elapsed(nowMs, candidateSinceMs_, debounceMs_)) {
    stable_ = candidate_;
  }

  return stable_;
}

}  // namespace luefterklappe

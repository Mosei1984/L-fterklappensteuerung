#ifndef LUEFTERKLAPPE_FAULT_REASON_H
#define LUEFTERKLAPPE_FAULT_REASON_H

#include <cstdint>

namespace luefterklappe {

enum class FaultReason : std::uint16_t {
  None = 0U,
  HomingRangeInvalid = 1U,
  HomingMinTravelExceeded = 2U,
  HomingMaxTravelExceeded = 3U,
  UnexpectedMinSwitch = 4U,
  UnexpectedMaxSwitch = 5U,
  StallGuardDuringMove = 6U,
  ValveBlockedDuringFreeCheck = 7U,
  MotionNoProgress = 8U,
  TmcCommunicationLost = 9U,
  SettingsWriteFailed = 10U,
  BothEndstopsActiveAtBoot = 11U,
  WatchdogRestart = 12U
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_FAULT_REASON_H

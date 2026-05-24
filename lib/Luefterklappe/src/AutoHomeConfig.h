#ifndef LUEFTERKLAPPE_AUTO_HOME_CONFIG_H
#define LUEFTERKLAPPE_AUTO_HOME_CONFIG_H

#include <cstdint>

namespace luefterklappe {

constexpr std::uint16_t kDefaultAutoHomeIntervalMinutes = 0U;
constexpr std::uint16_t kMaxAutoHomeIntervalMinutes = 10080U;

inline bool autoHomeIntervalMinutesIsValid(const std::uint16_t minutes) {
  return minutes <= kMaxAutoHomeIntervalMinutes;
}

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_AUTO_HOME_CONFIG_H

#ifndef LUEFTERKLAPPE_HOMING_CONFIG_H
#define LUEFTERKLAPPE_HOMING_CONFIG_H

#include <cstdint>

namespace luefterklappe {

enum class HomingSwitch : std::uint8_t {
  MinInput = 0U,
  MaxInput = 1U
};

enum class HomingDirection : std::uint8_t {
  Negative = 0U,
  Positive = 1U
};

struct HomingConfig {
  HomingSwitch minSwitch{HomingSwitch::MinInput};
  HomingSwitch maxSwitch{HomingSwitch::MaxInput};
  HomingDirection minDirection{HomingDirection::Negative};
  HomingDirection maxDirection{HomingDirection::Positive};
  bool stepperDirectionInverted{false};
};

constexpr HomingConfig kDefaultHomingConfig{};

inline bool homingConfigValuesAreValid(const HomingConfig& config) {
  return (config.minSwitch != config.maxSwitch) &&
         (config.minDirection != config.maxDirection);
}

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_HOMING_CONFIG_H

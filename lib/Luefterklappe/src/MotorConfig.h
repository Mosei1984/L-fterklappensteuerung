#ifndef LUEFTERKLAPPE_MOTOR_CONFIG_H
#define LUEFTERKLAPPE_MOTOR_CONFIG_H

#include <cstdint>

namespace luefterklappe {

struct MotorConfig {
  std::uint16_t normalMaxSpeedStepsPerSecond;
  std::uint16_t homingMaxSpeedStepsPerSecond;
  std::uint16_t runCurrentMilliamps;
};

constexpr std::uint16_t kMinMotorSpeedStepsPerSecond = 20U;
constexpr std::uint16_t kMaxMotorSpeedStepsPerSecond = 5000U;
constexpr std::uint16_t kMinRunCurrentMilliamps = 100U;
constexpr std::uint16_t kMaxRunCurrentMilliamps = 1000U;

constexpr MotorConfig kDefaultMotorConfig{400U, 200U, 650U};

inline bool motorConfigValuesAreValid(const MotorConfig config) {
  return (config.normalMaxSpeedStepsPerSecond >=
          kMinMotorSpeedStepsPerSecond) &&
         (config.normalMaxSpeedStepsPerSecond <=
          kMaxMotorSpeedStepsPerSecond) &&
         (config.homingMaxSpeedStepsPerSecond >=
          kMinMotorSpeedStepsPerSecond) &&
         (config.homingMaxSpeedStepsPerSecond <=
          kMaxMotorSpeedStepsPerSecond) &&
         (config.runCurrentMilliamps >= kMinRunCurrentMilliamps) &&
         (config.runCurrentMilliamps <= kMaxRunCurrentMilliamps);
}

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_MOTOR_CONFIG_H

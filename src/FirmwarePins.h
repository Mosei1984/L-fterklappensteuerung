#ifndef LUEFTERKLAPPE_FIRMWARE_PINS_H
#define LUEFTERKLAPPE_FIRMWARE_PINS_H

#include <cstdint>

namespace luefterklappe {
namespace firmware {

constexpr std::uint8_t kStepPin = 2U;
constexpr std::uint8_t kDirPin = 3U;
constexpr std::uint8_t kEnablePin = 7U;
constexpr std::uint8_t kMinSwitchPin = 5U;
constexpr std::uint8_t kMaxSwitchPin = 6U;
constexpr std::uint8_t kTmcUartTxPin = 8U;
constexpr std::uint8_t kTmcUartRxPin = 9U;

}  // namespace firmware
}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_FIRMWARE_PINS_H

#ifndef LUEFTERKLAPPE_TMC2209_DRIVER_H
#define LUEFTERKLAPPE_TMC2209_DRIVER_H

#include <cstddef>
#include <cstdint>

#include "FanFlapController.h"
#include "IoPorts.h"

namespace luefterklappe {

struct Tmc2209Config {
  std::uint8_t syncByte;
  std::uint8_t slaveAddress;
  std::uint8_t gconfRegister;
  std::uint8_t globalStatusRegister;
  std::uint8_t holdRunCurrentRegister;
  std::uint8_t powerDownRegister;
  std::uint8_t coolThresholdRegister;
  std::uint8_t chopConfigRegister;
  std::uint8_t pwmConfigRegister;
  std::uint8_t stallGuardThresholdRegister;
  std::uint8_t stallGuardResultRegister;
  std::uint8_t driverStatusRegister;
  std::uint32_t gconfValue;
  std::uint32_t globalStatusClearValue;
  std::uint32_t holdRunCurrentValue;
  std::uint32_t powerDownValue;
  std::uint32_t coolThresholdValue;
  std::uint32_t chopConfigValue;
  std::uint32_t pwmConfigValue;
  std::uint8_t stallGuardThreshold;
  std::uint8_t driverStatusPollInterval;
  std::uint32_t resetDelayMs;
  std::uint32_t responseDelayMs;
};

constexpr Tmc2209Config kDefaultTmc2209Config{
    0x05U,
    0x00U,
    0x00U,
    0x01U,
    0x10U,
    0x11U,
    0x14U,
    0x6CU,
    0x70U,
    0x40U,
    0x41U,
    0x6FU,
    0x000001C0UL,
    0x00000007UL,
    0x0008140AUL,
    0x00000014UL,
    0x000FFFFFUL,
    0x14030053UL,
    0xC80D0E24UL,
    100U,
    8U,
    10UL,
    2UL};

enum class Tmc2209PollResult : std::uint8_t {
  NotStalled,
  Stalled,
  DriverError,
  CommunicationError
};

class Tmc2209Driver {
 public:
  Tmc2209Driver(UartPort& uart, DelayPort& delay, EventSink& events,
                const Tmc2209Config& config = kDefaultTmc2209Config);

  void initialize();
  void sendCommand(std::uint8_t address, std::uint8_t value);
  void setStallGuardThreshold(std::uint8_t threshold);
  std::uint8_t stallGuardThreshold() const;
  void setRunCurrentMilliamps(std::uint16_t milliamps);
  std::uint16_t runCurrentMilliamps() const;
  bool readRegister(std::uint8_t registerAddress, std::uint32_t& value);
  Tmc2209PollResult pollStallGuardStatus();
  bool pollStallGuard();
  bool verifyCommunication();

 private:
  struct RegisterWrite {
    std::uint8_t address;
    std::uint32_t value;
  };

  static constexpr std::size_t kReadDatagramLength = 4U;
  static constexpr std::size_t kWriteDatagramLength = 8U;
  static constexpr std::size_t kMaxReadBytes = 24U;
  static constexpr std::size_t kMaxStaleReadBytes = 64U;
  static constexpr std::uint8_t kPollReadAttempts = 3U;
  static constexpr std::uint8_t kWriteAccessMask = 0x80U;
  static constexpr std::uint8_t kMasterReplyAddress = 0xFFU;

  static std::uint8_t calculateCrc(const std::uint8_t* data,
                                   std::size_t lengthWithoutCrc);
  static bool isResponseForRegister(const std::uint8_t* data,
                                    const Tmc2209Config& config,
                                    std::uint8_t registerAddress);
  static std::uint8_t currentScaleFromMilliamps(std::uint16_t milliamps);
  static std::uint32_t holdRunCurrentValueFromMilliamps(
      std::uint16_t milliamps);
  void drainReceiveBuffer();
  void writeRegister(RegisterWrite write);
  void writeDatagram(const std::uint8_t* data, std::size_t length);

  UartPort& uart_;
  DelayPort& delay_;
  EventSink& events_;
  Tmc2209Config config_;
  std::uint16_t runCurrentMilliamps_;
  std::uint8_t pollCounter_{0U};
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_TMC2209_DRIVER_H

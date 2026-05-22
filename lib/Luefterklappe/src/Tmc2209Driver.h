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
  std::uint8_t stallGuardThresholdRegister;
  std::uint8_t stallGuardResultRegister;
  std::uint32_t gconfValue;
  std::uint8_t stallGuardThreshold;
  std::uint32_t resetDelayMs;
  std::uint32_t responseDelayMs;
};

constexpr Tmc2209Config kDefaultTmc2209Config{
    0x05U,
    0x00U,
    0x00U,
    0x40U,
    0x41U,
    0x00000040UL,
    100U,
    10UL,
    2UL};

class Tmc2209Driver {
 public:
  Tmc2209Driver(UartPort& uart, DelayPort& delay, EventSink& events,
                const Tmc2209Config& config = kDefaultTmc2209Config);

  void initialize();
  void sendCommand(std::uint8_t address, std::uint8_t value);
  bool readRegister(std::uint8_t registerAddress, std::uint32_t& value);
  bool pollStallGuard();

 private:
  struct RegisterWrite {
    std::uint8_t address;
    std::uint32_t value;
  };

  static constexpr std::size_t kReadDatagramLength = 4U;
  static constexpr std::size_t kWriteDatagramLength = 8U;
  static constexpr std::size_t kMaxReadBytes = 24U;
  static constexpr std::uint8_t kWriteAccessMask = 0x80U;
  static constexpr std::uint8_t kMasterReplyAddress = 0xFFU;

  static std::uint8_t calculateCrc(const std::uint8_t* data,
                                   std::size_t lengthWithoutCrc);
  static bool isResponseForRegister(const std::uint8_t* data,
                                    const Tmc2209Config& config,
                                    std::uint8_t registerAddress);
  void writeRegister(RegisterWrite write);
  void writeDatagram(const std::uint8_t* data, std::size_t length);

  UartPort& uart_;
  DelayPort& delay_;
  EventSink& events_;
  Tmc2209Config config_;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_TMC2209_DRIVER_H

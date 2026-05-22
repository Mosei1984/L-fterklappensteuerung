#include "Tmc2209Driver.h"

#include <array>

namespace luefterklappe {

Tmc2209Driver::Tmc2209Driver(UartPort& uart, DelayPort& delay,
                             EventSink& events, const Tmc2209Config& config)
    : uart_(uart), delay_(delay), events_(events), config_(config) {}

void Tmc2209Driver::initialize() {
  const Event initEvent{EventId::TmcInitializationStarted, 0, 0, false};
  events_.onEvent(initEvent);

  writeRegister(RegisterWrite{config_.gconfRegister, config_.gconfValue});
  delay_.delayMilliseconds(config_.resetDelayMs);
  writeRegister(RegisterWrite{config_.stallGuardThresholdRegister,
                              config_.stallGuardThreshold});

  const Event configuredEvent{EventId::TmcConfigured, 0, 0, false};
  events_.onEvent(configuredEvent);
}

void Tmc2209Driver::sendCommand(const std::uint8_t address,
                                const std::uint8_t value) {
  writeRegister(RegisterWrite{address, value});
}

void Tmc2209Driver::writeRegister(const RegisterWrite write) {
  std::array<std::uint8_t, kWriteDatagramLength> datagram{{
      config_.syncByte,
      config_.slaveAddress,
      static_cast<std::uint8_t>(write.address | kWriteAccessMask),
      static_cast<std::uint8_t>((write.value >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((write.value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((write.value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(write.value & 0xFFU),
      0U}};

  datagram[kWriteDatagramLength - 1U] =
      calculateCrc(datagram.data(), kWriteDatagramLength - 1U);
  writeDatagram(datagram.data(), datagram.size());
}

bool Tmc2209Driver::readRegister(const std::uint8_t registerAddress,
                                 std::uint32_t& value) {
  std::array<std::uint8_t, kReadDatagramLength> request{{
      config_.syncByte,
      config_.slaveAddress,
      static_cast<std::uint8_t>(registerAddress & ~kWriteAccessMask),
      0U}};
  // cppcheck-suppress misra-c2012-12.3
  std::array<std::uint8_t, kWriteDatagramLength> window{};
  std::size_t windowLength = 0U;
  std::size_t bytesRead = 0U;

  request[kReadDatagramLength - 1U] =
      calculateCrc(request.data(), kReadDatagramLength - 1U);
  writeDatagram(request.data(), request.size());
  delay_.delayMilliseconds(config_.responseDelayMs);

  while ((uart_.available() > 0U) && (bytesRead < kMaxReadBytes)) {
    std::uint8_t nextByte = 0U;

    if (!uart_.readByte(nextByte)) {
      return false;
    }

    ++bytesRead;
    if (windowLength < kWriteDatagramLength) {
      window[windowLength] = nextByte;
      ++windowLength;
    } else {
      for (std::size_t index = 1U; index < kWriteDatagramLength; ++index) {
        window[index - 1U] = window[index];
      }
      window[kWriteDatagramLength - 1U] = nextByte;
    }

    if ((windowLength == kWriteDatagramLength) &&
        isResponseForRegister(window.data(), config_, registerAddress)) {
      value = (static_cast<std::uint32_t>(window[3]) << 24U) |
              (static_cast<std::uint32_t>(window[4]) << 16U) |
              (static_cast<std::uint32_t>(window[5]) << 8U) |
              static_cast<std::uint32_t>(window[6]);
      return true;
    }
  }

  return false;
}

Tmc2209PollResult Tmc2209Driver::pollStallGuardStatus() {
  std::uint32_t stallGuardResult = 0U;

  if (!readRegister(config_.stallGuardResultRegister, stallGuardResult)) {
    return Tmc2209PollResult::CommunicationError;
  }

  return ((stallGuardResult & 0x3FFUL) <= config_.stallGuardThreshold)
             ? Tmc2209PollResult::Stalled
             : Tmc2209PollResult::NotStalled;
}

bool Tmc2209Driver::pollStallGuard() {
  return pollStallGuardStatus() == Tmc2209PollResult::Stalled;
}

bool Tmc2209Driver::verifyCommunication() {
  std::uint32_t gconfValue = 0U;

  if (!readRegister(config_.gconfRegister, gconfValue)) {
    return false;
  }

  return (gconfValue & config_.gconfValue) == config_.gconfValue;
}

std::uint8_t Tmc2209Driver::calculateCrc(const std::uint8_t* const data,
                                         const std::size_t lengthWithoutCrc) {
  std::uint8_t crc = 0U;

  for (std::size_t index = 0U; index < lengthWithoutCrc; ++index) {
    std::uint8_t currentByte = data[index];

    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
      const bool xorBit =
          (((crc >> 7U) ^ (currentByte & 0x01U)) & 0x01U) != 0U;

      crc = static_cast<std::uint8_t>(crc << 1U);
      if (xorBit) {
        crc = static_cast<std::uint8_t>(crc ^ 0x07U);
      }

      currentByte = static_cast<std::uint8_t>(currentByte >> 1U);
    }
  }

  return crc;
}

bool Tmc2209Driver::isResponseForRegister(
    const std::uint8_t* const data, const Tmc2209Config& config,
    const std::uint8_t registerAddress) {
  const std::uint8_t expectedCrc =
      calculateCrc(data, kWriteDatagramLength - 1U);

  const bool addressedToMaster =
      (data[1] == kMasterReplyAddress) || (data[1] == config.slaveAddress);

  return (data[0] == config.syncByte) && addressedToMaster &&
         ((data[2] & ~kWriteAccessMask) ==
          (registerAddress & ~kWriteAccessMask)) &&
         (data[kWriteDatagramLength - 1U] == expectedCrc);
}

void Tmc2209Driver::writeDatagram(const std::uint8_t* const data,
                                  const std::size_t length) {
  for (std::size_t index = 0U; index < length; ++index) {
    uart_.writeByte(data[index]);
  }

  uart_.flush();
}

}  // namespace luefterklappe

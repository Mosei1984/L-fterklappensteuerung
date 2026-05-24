#ifndef LUEFTERKLAPPE_MODBUS_RTU_SERVER_H
#define LUEFTERKLAPPE_MODBUS_RTU_SERVER_H

#include <cstddef>
#include <cstdint>

#include "FanFlapController.h"
#include "IoPorts.h"

namespace luefterklappe {

enum class BootReason : std::uint16_t {
  Unknown = 0U,
  PowerOn = 1U,
  Watchdog = 2U,
  SoftwareReset = 3U
};

enum class ModbusRegister : std::uint16_t {
  Command = 0U,
  TargetPositionHigh = 1U,
  TargetPositionLow = 2U,
  SoftMinHigh = 3U,
  SoftMinLow = 4U,
  SoftMaxHigh = 5U,
  SoftMaxLow = 6U,
  DeviceId = 7U,
  State = 8U,
  Flags = 9U,
  CurrentPositionHigh = 10U,
  CurrentPositionLow = 11U,
  MaxPositionHigh = 12U,
  MaxPositionLow = 13U,
  TargetPermille = 14U,
  CurrentPermille = 15U,
  SafePositionPermille = 16U,
  LastFaultReason = 17U,
  FaultCount = 18U,
  SettingsStatus = 19U,
  TmcHealth = 20U,
  BootReason = 21U,
  FirmwareProtocolVersion = 22U,
  TargetDegree = 23U,
  CurrentDegree = 24U,
  SoftMinDegree = 25U,
  SoftMaxDegree = 26U,
  StallGuardThreshold = 27U,
  HomeMinSwitch = 28U,
  HomeMaxSwitch = 29U,
  HomeMinDirection = 30U,
  HomeMaxDirection = 31U,
  StepperDirectionInverted = 32U,
  NormalMaxSpeed = 33U,
  HomingMaxSpeed = 34U,
  RunCurrentMilliamps = 35U
};

enum class ModbusCommand : std::uint16_t {
  None = 0U,
  Home = 1U,
  Reset = 2U,
  SoftEndstopsOn = 3U,
  SoftEndstopsOff = 4U,
  RefreshMachine = 5U
};

class ModbusRtuServer {
 public:
  ModbusRtuServer(FanFlapController& controller, UartPort& uart);

  void handleByte(std::uint8_t value);
  void reset();
  bool isReceiving() const;
  void setBootReason(BootReason bootReason);

  static std::uint16_t crc16(const std::uint8_t* data,
                             std::size_t lengthWithoutCrc);

 private:
  struct RegisterWrite {
    std::uint16_t address;
    std::uint16_t value;
  };

  struct ExceptionResponse {
    std::uint8_t functionCode;
    std::uint8_t exceptionCode;
  };

  static constexpr std::size_t kMaxFrameSize = 96U;
  static constexpr std::size_t kFixedRequestSize = 8U;
  static constexpr std::uint8_t kReadHoldingRegisters = 0x03U;
  static constexpr std::uint8_t kWriteSingleRegister = 0x06U;
  static constexpr std::uint8_t kWriteMultipleRegisters = 0x10U;
  static constexpr std::uint8_t kExceptionOffset = 0x80U;
  static constexpr std::uint8_t kIllegalFunction = 0x01U;
  static constexpr std::uint8_t kIllegalDataAddress = 0x02U;
  static constexpr std::uint8_t kIllegalDataValue = 0x03U;
  static constexpr std::uint8_t kNoException = 0x00U;

  void updateExpectedLength();
  void processFrame();
  void handleReadHoldingRegisters();
  void handleWriteSingleRegister();
  void handleWriteMultipleRegisters();
  void sendException(ExceptionResponse response);
  void sendFrame(const std::uint8_t* data, std::size_t lengthWithoutCrc);
  bool validCrc() const;
  bool isForThisDevice() const;
  std::uint8_t validateRegisterWrites(const RegisterWrite* writes,
                                      std::size_t count) const;
  std::uint8_t validateRegisterWrite(RegisterWrite write) const;
  bool softEndstopRangeCanBeApplied(SoftEndstopRange range) const;
  bool degreeRangeCanBeApplied(std::uint16_t minDegree,
                               std::uint16_t maxDegree) const;
  bool applyDegreeSoftEndstops(std::uint16_t minDegree,
                               std::uint16_t maxDegree);
  bool applyRegisterWrites(const RegisterWrite* writes, std::size_t count);
  bool writeRegister(RegisterWrite write);
  bool readRegister(std::uint16_t address, std::uint16_t& value) const;
  void executeCommand(std::uint16_t value);

  static std::uint16_t readUint16(const std::uint8_t* data,
                                  std::size_t offset);
  static void writeUint16(std::uint8_t* data, std::size_t offset,
                          std::uint16_t value);
  static std::uint16_t highWord(std::int32_t value);
  static std::uint16_t lowWord(std::int32_t value);
  static std::int32_t combineWords(std::uint16_t high, std::uint16_t low);
  std::int32_t positionFromPermille(std::uint16_t permille) const;
  std::uint16_t permilleFromPosition(std::int32_t position) const;
  std::int32_t positionFromDegree(std::uint16_t degree) const;
  std::uint16_t degreeFromPosition(std::int32_t position) const;

  FanFlapController& controller_;
  UartPort& uart_;
  std::uint8_t buffer_[kMaxFrameSize];
  std::size_t length_;
  std::size_t expectedLength_;
  BootReason bootReason_;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_MODBUS_RTU_SERVER_H

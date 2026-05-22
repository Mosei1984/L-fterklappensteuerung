#include "ModbusRtuServer.h"

#include <array>

namespace luefterklappe {
namespace {

constexpr std::uint16_t kRegisterCount = 17U;
constexpr std::uint16_t kMaxReadRegisters = kRegisterCount;
constexpr std::size_t kMaxWriteRegisters = kRegisterCount;
constexpr std::uint16_t kReadyFlag = 0x0001U;
constexpr std::uint16_t kFaultFlag = 0x0002U;
constexpr std::uint16_t kSoftEndstopsFlag = 0x0004U;
constexpr std::uint16_t kMaxPermille = 1000U;

bool isFaultState(const ControllerState state) {
  return (state == ControllerState::ErrorDetected) ||
         (state == ControllerState::WaitReset) ||
         (state == ControllerState::AutoRehome);
}

}  // namespace

ModbusRtuServer::ModbusRtuServer(FanFlapController& controller, UartPort& uart)
    : controller_(controller),
      uart_(uart),
      buffer_{},
      length_(0U),
      expectedLength_(0U) {}

void ModbusRtuServer::handleByte(const std::uint8_t value) {
  if (length_ >= kMaxFrameSize) {
    reset();
  }

  buffer_[length_] = value;
  ++length_;
  updateExpectedLength();

  if ((expectedLength_ > 0U) && (length_ >= expectedLength_)) {
    processFrame();
    reset();
  }
}

void ModbusRtuServer::reset() {
  length_ = 0U;
  expectedLength_ = 0U;
}

bool ModbusRtuServer::isReceiving() const { return length_ > 0U; }

std::uint16_t ModbusRtuServer::crc16(const std::uint8_t* const data,
                                     const std::size_t lengthWithoutCrc) {
  std::uint16_t crc = 0xFFFFU;

  for (std::size_t index = 0U; index < lengthWithoutCrc; ++index) {
    crc = static_cast<std::uint16_t>(crc ^ data[index]);

    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
      const bool lsbSet = (crc & 0x0001U) != 0U;
      crc = static_cast<std::uint16_t>(crc >> 1U);
      if (lsbSet) {
        crc = static_cast<std::uint16_t>(crc ^ 0xA001U);
      }
    }
  }

  return crc;
}

void ModbusRtuServer::updateExpectedLength() {
  if (length_ < 2U) {
    return;
  }

  const std::uint8_t functionCode = buffer_[1];

  switch (functionCode) {
    case kReadHoldingRegisters:
    case kWriteSingleRegister:
      expectedLength_ = kFixedRequestSize;
      break;
    case kWriteMultipleRegisters:
      if (length_ >= 7U) {
        expectedLength_ = static_cast<std::size_t>(9U + buffer_[6]);
        if (expectedLength_ > kMaxFrameSize) {
          reset();
        }
      }
      break;
    default:
      if (length_ >= kFixedRequestSize) {
        expectedLength_ = kFixedRequestSize;
      }
      break;
  }
}

void ModbusRtuServer::processFrame() {
  if (!validCrc()) {
    return;
  }

  if (!isForThisDevice()) {
    return;
  }

  switch (buffer_[1]) {
    case kReadHoldingRegisters:
      handleReadHoldingRegisters();
      break;
    case kWriteSingleRegister:
      handleWriteSingleRegister();
      break;
    case kWriteMultipleRegisters:
      handleWriteMultipleRegisters();
      break;
    default:
      sendException(ExceptionResponse{buffer_[1], kIllegalFunction});
      break;
  }
}

void ModbusRtuServer::handleReadHoldingRegisters() {
  const std::uint16_t startAddress = readUint16(buffer_, 2U);
  const std::uint16_t quantity = readUint16(buffer_, 4U);
  // cppcheck-suppress misra-c2012-12.3
  std::array<std::uint8_t, kMaxFrameSize> response{
      {buffer_[0], buffer_[1], 0U}};

  if (buffer_[0] == 0U) {
    return;
  }

  if ((quantity == 0U) || (quantity > kMaxReadRegisters) ||
      ((startAddress + quantity) > kRegisterCount)) {
    sendException(ExceptionResponse{buffer_[1], kIllegalDataAddress});
    return;
  }

  response[2] = static_cast<std::uint8_t>(quantity * 2U);
  for (std::uint16_t index = 0U; index < quantity; ++index) {
    std::uint16_t registerValue = 0U;
    static_cast<void>(readRegister(startAddress + index, registerValue));
    writeUint16(response.data(), static_cast<std::size_t>(3U + (index * 2U)),
                registerValue);
  }

  sendFrame(response.data(), static_cast<std::size_t>(3U + response[2]));
}

void ModbusRtuServer::handleWriteSingleRegister() {
  const std::uint16_t address = readUint16(buffer_, 2U);
  const std::uint16_t value = readUint16(buffer_, 4U);
  const RegisterWrite write{address, value};
  const std::uint8_t exceptionCode =
      validateRegisterWrites(&write, 1U);

  if (exceptionCode != kNoException) {
    sendException(ExceptionResponse{buffer_[1], exceptionCode});
    return;
  }

  if (!applyRegisterWrites(&write, 1U)) {
    sendException(ExceptionResponse{buffer_[1], kIllegalDataValue});
    return;
  }

  if (buffer_[0] != 0U) {
    sendFrame(buffer_, 6U);
  }
}

void ModbusRtuServer::handleWriteMultipleRegisters() {
  const std::uint16_t startAddress = readUint16(buffer_, 2U);
  const std::uint16_t quantity = readUint16(buffer_, 4U);
  const std::uint8_t byteCount = buffer_[6];
  // cppcheck-suppress misra-c2012-12.3
  std::array<RegisterWrite, kMaxWriteRegisters> writes{};

  if ((quantity == 0U) || ((quantity * 2U) != byteCount) ||
      ((startAddress + quantity) > kRegisterCount)) {
    sendException(ExceptionResponse{
        buffer_[1], ((startAddress + quantity) > kRegisterCount)
                       ? kIllegalDataAddress
                       : kIllegalDataValue});
    return;
  }

  for (std::uint16_t index = 0U; index < quantity; ++index) {
    const std::uint16_t value =
        readUint16(buffer_, static_cast<std::size_t>(7U + (index * 2U)));
    const auto address = static_cast<std::uint16_t>(startAddress + index);
    writes[index] = RegisterWrite{address, value};
  }

  const std::uint8_t exceptionCode =
      validateRegisterWrites(writes.data(), quantity);
  if (exceptionCode != kNoException) {
    sendException(ExceptionResponse{buffer_[1], exceptionCode});
    return;
  }

  if (!applyRegisterWrites(writes.data(), quantity)) {
    sendException(ExceptionResponse{buffer_[1], kIllegalDataValue});
    return;
  }

  if (buffer_[0] != 0U) {
    const std::array<std::uint8_t, 6U> response{
        {buffer_[0], buffer_[1], buffer_[2], buffer_[3], buffer_[4],
         buffer_[5]}};
    sendFrame(response.data(), response.size());
  }
}

void ModbusRtuServer::sendException(const ExceptionResponse response) {
  if (buffer_[0] == 0U) {
    return;
  }

  const std::array<std::uint8_t, 3U> frame{
      {buffer_[0],
       static_cast<std::uint8_t>(response.functionCode | kExceptionOffset),
       response.exceptionCode}};
  sendFrame(frame.data(), frame.size());
}

void ModbusRtuServer::sendFrame(const std::uint8_t* const data,
                                const std::size_t lengthWithoutCrc) {
  const std::uint16_t crc = crc16(data, lengthWithoutCrc);

  for (std::size_t index = 0U; index < lengthWithoutCrc; ++index) {
    uart_.writeByte(data[index]);
  }

  uart_.writeByte(static_cast<std::uint8_t>(crc & 0x00FFU));
  uart_.writeByte(static_cast<std::uint8_t>((crc >> 8U) & 0x00FFU));
  uart_.flush();
}

bool ModbusRtuServer::validCrc() const {
  if (length_ < 4U) {
    return false;
  }

  const std::uint16_t expected = crc16(buffer_, length_ - 2U);
  const auto received = readUint16(buffer_, length_ - 2U);
  const auto receivedLittleEndian =
      static_cast<std::uint16_t>((received >> 8U) | (received << 8U));

  return expected == receivedLittleEndian;
}

bool ModbusRtuServer::isForThisDevice() const {
  return buffer_[0] == controller_.deviceId();
}

std::uint8_t ModbusRtuServer::validateRegisterWrites(
    const RegisterWrite* const writes, const std::size_t count) const {
  bool hasSingleWriteOnlyRegister = false;
  bool hasTargetPosition = false;
  bool hasTargetPermille = false;
  bool hasSoftEndstop = false;
  std::int32_t proposedSoftMin = controller_.softMinPosition();
  std::int32_t proposedSoftMax = controller_.softMaxPosition();

  for (std::size_t index = 0U; index < count; ++index) {
    const RegisterWrite write = writes[index];
    const std::uint8_t exceptionCode = validateRegisterWrite(write);

    if (exceptionCode != kNoException) {
      return exceptionCode;
    }

    switch (static_cast<ModbusRegister>(write.address)) {
      case ModbusRegister::Command:
      case ModbusRegister::DeviceId:
        hasSingleWriteOnlyRegister = true;
        break;
      case ModbusRegister::TargetPositionHigh:
      case ModbusRegister::TargetPositionLow:
        hasTargetPosition = true;
        break;
      case ModbusRegister::TargetPermille:
        hasTargetPermille = true;
        break;
      case ModbusRegister::SafePositionPermille:
        break;
      case ModbusRegister::SoftMinHigh:
        hasSoftEndstop = true;
        proposedSoftMin =
            combineWords(write.value, lowWord(proposedSoftMin));
        break;
      case ModbusRegister::SoftMinLow:
        hasSoftEndstop = true;
        proposedSoftMin =
            combineWords(highWord(proposedSoftMin), write.value);
        break;
      case ModbusRegister::SoftMaxHigh:
        hasSoftEndstop = true;
        proposedSoftMax =
            combineWords(write.value, lowWord(proposedSoftMax));
        break;
      case ModbusRegister::SoftMaxLow:
        hasSoftEndstop = true;
        proposedSoftMax =
            combineWords(highWord(proposedSoftMax), write.value);
        break;
      case ModbusRegister::State:
      case ModbusRegister::Flags:
      case ModbusRegister::CurrentPositionHigh:
      case ModbusRegister::CurrentPositionLow:
      case ModbusRegister::MaxPositionHigh:
      case ModbusRegister::MaxPositionLow:
      case ModbusRegister::CurrentPermille:
        break;
    }
  }

  if ((count > 1U) && hasSingleWriteOnlyRegister) {
    return kIllegalDataValue;
  }

  if (hasTargetPosition && hasTargetPermille) {
    return kIllegalDataValue;
  }

  if (hasSoftEndstop &&
      !softEndstopRangeCanBeApplied(
          SoftEndstopRange{proposedSoftMin, proposedSoftMax})) {
    return kIllegalDataValue;
  }

  return kNoException;
}

std::uint8_t ModbusRtuServer::validateRegisterWrite(
    const RegisterWrite write) const {
  switch (static_cast<ModbusRegister>(write.address)) {
    case ModbusRegister::Command:
      switch (static_cast<ModbusCommand>(write.value)) {
        case ModbusCommand::None:
        case ModbusCommand::Home:
        case ModbusCommand::Reset:
        case ModbusCommand::SoftEndstopsOn:
        case ModbusCommand::SoftEndstopsOff:
        case ModbusCommand::RefreshMachine:
          return kNoException;
      }
      return kIllegalDataValue;
    case ModbusRegister::TargetPositionHigh:
    case ModbusRegister::TargetPositionLow:
    case ModbusRegister::TargetPermille:
      if (controller_.state() != ControllerState::Ready) {
        return kIllegalDataValue;
      }
      if ((static_cast<ModbusRegister>(write.address) ==
           ModbusRegister::TargetPermille) &&
          (write.value > kMaxPermille)) {
        return kIllegalDataValue;
      }
      return kNoException;
    case ModbusRegister::SafePositionPermille:
      if (write.value > kMaxPermille) {
        return kIllegalDataValue;
      }
      return kNoException;
    case ModbusRegister::SoftMinHigh:
    case ModbusRegister::SoftMinLow:
    case ModbusRegister::SoftMaxHigh:
    case ModbusRegister::SoftMaxLow:
      if (controller_.state() != ControllerState::Ready) {
        return kIllegalDataValue;
      }
      return kNoException;
    case ModbusRegister::DeviceId:
      if ((write.value == 0U) || (write.value > 247U)) {
        return kIllegalDataValue;
      }
      return kNoException;
    case ModbusRegister::State:
    case ModbusRegister::Flags:
    case ModbusRegister::CurrentPositionHigh:
    case ModbusRegister::CurrentPositionLow:
    case ModbusRegister::MaxPositionHigh:
    case ModbusRegister::MaxPositionLow:
    case ModbusRegister::CurrentPermille:
      return kIllegalDataAddress;
  }

  return kIllegalDataAddress;
}

bool ModbusRtuServer::softEndstopRangeCanBeApplied(
    const SoftEndstopRange range) const {
  std::int32_t boundedMin = range.minPosition;
  std::int32_t boundedMax = range.maxPosition;

  if (boundedMin < 0) {
    boundedMin = 0;
  }

  if (boundedMax > controller_.maxPosition()) {
    boundedMax = controller_.maxPosition();
  }

  if (boundedMax < 0) {
    boundedMax = 0;
  }

  return boundedMin < boundedMax;
}

bool ModbusRtuServer::applyRegisterWrites(const RegisterWrite* const writes,
                                          const std::size_t count) {
  bool hasSoftEndstop = false;
  bool hasTargetPosition = false;
  bool hasTargetPermille = false;
  std::uint16_t targetPermille = 0U;
  std::int32_t proposedSoftMin = controller_.softMinPosition();
  std::int32_t proposedSoftMax = controller_.softMaxPosition();
  std::int32_t proposedTarget = controller_.targetPosition();

  for (std::size_t index = 0U; index < count; ++index) {
    const RegisterWrite write = writes[index];

    switch (static_cast<ModbusRegister>(write.address)) {
      case ModbusRegister::Command:
      case ModbusRegister::DeviceId:
        return writeRegister(write);
      case ModbusRegister::TargetPositionHigh:
        hasTargetPosition = true;
        proposedTarget = combineWords(write.value, lowWord(proposedTarget));
        break;
      case ModbusRegister::TargetPositionLow:
        hasTargetPosition = true;
        proposedTarget = combineWords(highWord(proposedTarget), write.value);
        break;
      case ModbusRegister::SoftMinHigh:
        hasSoftEndstop = true;
        proposedSoftMin =
            combineWords(write.value, lowWord(proposedSoftMin));
        break;
      case ModbusRegister::SoftMinLow:
        hasSoftEndstop = true;
        proposedSoftMin =
            combineWords(highWord(proposedSoftMin), write.value);
        break;
      case ModbusRegister::SoftMaxHigh:
        hasSoftEndstop = true;
        proposedSoftMax =
            combineWords(write.value, lowWord(proposedSoftMax));
        break;
      case ModbusRegister::SoftMaxLow:
        hasSoftEndstop = true;
        proposedSoftMax =
            combineWords(highWord(proposedSoftMax), write.value);
        break;
      case ModbusRegister::TargetPermille:
        hasTargetPermille = true;
        targetPermille = write.value;
        break;
      case ModbusRegister::SafePositionPermille:
        if (!controller_.setSafePositionPermille(write.value)) {
          return false;
        }
        break;
      case ModbusRegister::State:
      case ModbusRegister::Flags:
      case ModbusRegister::CurrentPositionHigh:
      case ModbusRegister::CurrentPositionLow:
      case ModbusRegister::MaxPositionHigh:
      case ModbusRegister::MaxPositionLow:
      case ModbusRegister::CurrentPermille:
        return false;
    }
  }

  if (hasSoftEndstop &&
      !controller_.setSoftEndstops(
          SoftEndstopRange{proposedSoftMin, proposedSoftMax})) {
    return false;
  }

  if (hasTargetPosition) {
    std::int32_t acceptedPosition = 0;
    if (!controller_.requestMoveTo(proposedTarget, acceptedPosition)) {
      return false;
    }
  }

  if (hasTargetPermille) {
    std::int32_t acceptedPosition = 0;
    if (!controller_.requestMoveTo(positionFromPermille(targetPermille),
                                   acceptedPosition)) {
      return false;
    }
  }

  return true;
}

bool ModbusRtuServer::writeRegister(const RegisterWrite write) {
  switch (static_cast<ModbusRegister>(write.address)) {
    case ModbusRegister::Command:
      executeCommand(write.value);
      return true;
    case ModbusRegister::TargetPositionHigh:
    {
      std::int32_t acceptedPosition = 0;
      return controller_.requestMoveTo(
          combineWords(write.value, lowWord(controller_.targetPosition())),
          acceptedPosition);
    }
    case ModbusRegister::TargetPositionLow:
    {
      std::int32_t acceptedPosition = 0;
      return controller_.requestMoveTo(
          combineWords(highWord(controller_.targetPosition()), write.value),
          acceptedPosition);
    }
    case ModbusRegister::SoftMinHigh:
      return controller_.setSoftEndstops(SoftEndstopRange{
          combineWords(write.value, lowWord(controller_.softMinPosition())),
          controller_.softMaxPosition()});
    case ModbusRegister::SoftMinLow:
      return controller_.setSoftEndstops(SoftEndstopRange{
          combineWords(highWord(controller_.softMinPosition()), write.value),
          controller_.softMaxPosition()});
    case ModbusRegister::SoftMaxHigh:
      return controller_.setSoftEndstops(SoftEndstopRange{
          controller_.softMinPosition(),
          combineWords(write.value, lowWord(controller_.softMaxPosition()))});
    case ModbusRegister::SoftMaxLow:
      return controller_.setSoftEndstops(SoftEndstopRange{
          controller_.softMinPosition(),
          combineWords(highWord(controller_.softMaxPosition()), write.value)});
    case ModbusRegister::DeviceId:
      if ((write.value == 0U) || (write.value > 247U)) {
        return false;
      }
      return controller_.setDeviceId(static_cast<std::uint8_t>(write.value));
    case ModbusRegister::TargetPermille:
    {
      std::int32_t acceptedPosition = 0;
      if (write.value > kMaxPermille) {
        return false;
      }
      return controller_.requestMoveTo(positionFromPermille(write.value),
                                       acceptedPosition);
    }
    case ModbusRegister::SafePositionPermille:
      return controller_.setSafePositionPermille(write.value);
    case ModbusRegister::State:
    case ModbusRegister::Flags:
    case ModbusRegister::CurrentPositionHigh:
    case ModbusRegister::CurrentPositionLow:
    case ModbusRegister::MaxPositionHigh:
    case ModbusRegister::MaxPositionLow:
    case ModbusRegister::CurrentPermille:
      return false;
  }

  return false;
}

bool ModbusRtuServer::readRegister(const std::uint16_t address,
                                   std::uint16_t& value) const {
  const ControllerState state = controller_.state();

  switch (static_cast<ModbusRegister>(address)) {
    case ModbusRegister::Command:
      value = static_cast<std::uint16_t>(ModbusCommand::None);
      return true;
    case ModbusRegister::TargetPositionHigh:
      value = highWord(controller_.targetPosition());
      return true;
    case ModbusRegister::TargetPositionLow:
      value = lowWord(controller_.targetPosition());
      return true;
    case ModbusRegister::SoftMinHigh:
      value = highWord(controller_.softMinPosition());
      return true;
    case ModbusRegister::SoftMinLow:
      value = lowWord(controller_.softMinPosition());
      return true;
    case ModbusRegister::SoftMaxHigh:
      value = highWord(controller_.softMaxPosition());
      return true;
    case ModbusRegister::SoftMaxLow:
      value = lowWord(controller_.softMaxPosition());
      return true;
    case ModbusRegister::DeviceId:
      value = controller_.deviceId();
      return true;
    case ModbusRegister::State:
      value = static_cast<std::uint16_t>(state);
      return true;
    case ModbusRegister::Flags:
    {
      std::uint16_t flags = 0U;
      if (state == ControllerState::Ready) {
        flags = kReadyFlag;
      }
      if (isFaultState(state)) {
        flags = static_cast<std::uint16_t>(flags | kFaultFlag);
      }
      if (controller_.softEndstopsEnabled()) {
        flags = static_cast<std::uint16_t>(flags | kSoftEndstopsFlag);
      }
      value = flags;
      return true;
    }
    case ModbusRegister::CurrentPositionHigh:
      value = highWord(controller_.currentPosition());
      return true;
    case ModbusRegister::CurrentPositionLow:
      value = lowWord(controller_.currentPosition());
      return true;
    case ModbusRegister::MaxPositionHigh:
      value = highWord(controller_.maxPosition());
      return true;
    case ModbusRegister::MaxPositionLow:
      value = lowWord(controller_.maxPosition());
      return true;
    case ModbusRegister::TargetPermille:
      value = permilleFromPosition(controller_.targetPosition());
      return true;
    case ModbusRegister::CurrentPermille:
      value = permilleFromPosition(controller_.currentPosition());
      return true;
    case ModbusRegister::SafePositionPermille:
      value = controller_.safePositionPermille();
      return true;
  }

  return false;
}

void ModbusRtuServer::executeCommand(const std::uint16_t value) {
  switch (static_cast<ModbusCommand>(value)) {
    case ModbusCommand::None:
      break;
    case ModbusCommand::Home:
      controller_.handleCommand("HOME");
      break;
    case ModbusCommand::Reset:
      controller_.handleCommand("RESET");
      break;
    case ModbusCommand::SoftEndstopsOn:
      static_cast<void>(controller_.setSoftEndstopsEnabled(true));
      break;
    case ModbusCommand::SoftEndstopsOff:
      static_cast<void>(controller_.setSoftEndstopsEnabled(false));
      break;
    case ModbusCommand::RefreshMachine:
      controller_.handleCommand("REFRESH");
      break;
  }
}

std::uint16_t ModbusRtuServer::readUint16(const std::uint8_t* const data,
                                          const std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(data[offset]) << 8U) |
      static_cast<std::uint16_t>(data[offset + 1U]));
}

void ModbusRtuServer::writeUint16(std::uint8_t* const data,
                                  const std::size_t offset,
                                  const std::uint16_t value) {
  data[offset] = static_cast<std::uint8_t>((value >> 8U) & 0x00FFU);
  data[offset + 1U] = static_cast<std::uint8_t>(value & 0x00FFU);
}

std::uint16_t ModbusRtuServer::highWord(const std::int32_t value) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint32_t>(value) >> 16U) & 0x0000FFFFUL);
}

std::uint16_t ModbusRtuServer::lowWord(const std::int32_t value) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(value) & 0x0000FFFFUL);
}

std::int32_t ModbusRtuServer::combineWords(const std::uint16_t high,
                                           const std::uint16_t low) {
  const std::uint32_t combined =
      (static_cast<std::uint32_t>(high) << 16U) | low;
  return static_cast<std::int32_t>(combined);
}

std::int32_t ModbusRtuServer::positionFromPermille(
    const std::uint16_t permille) const {
  if (controller_.maxPosition() <= 0) {
    return 0;
  }

  return static_cast<std::int32_t>(
      (static_cast<std::int64_t>(controller_.maxPosition()) * permille) /
      kMaxPermille);
}

std::uint16_t ModbusRtuServer::permilleFromPosition(
    const std::int32_t position) const {
  if (controller_.maxPosition() <= 0) {
    return 0U;
  }

  if (position <= 0) {
    return 0U;
  }

  if (position >= controller_.maxPosition()) {
    return kMaxPermille;
  }

  return static_cast<std::uint16_t>(
      (static_cast<std::int64_t>(position) * kMaxPermille) /
      controller_.maxPosition());
}

}  // namespace luefterklappe

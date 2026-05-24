#include "FanFlapController.h"

#include <limits>

namespace luefterklappe {
namespace {

constexpr std::uint32_t kTimerNotStarted =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint16_t kMaxPermille = 1000U;
constexpr std::uint16_t kMaxDegree = 90U;
constexpr std::uint16_t kDefaultStallGuardThreshold = 100U;
constexpr std::uint16_t kMaxStallGuardThreshold = 255U;
constexpr std::int32_t kMaxMotorTestSteps = 3200;

bool isSpace(const char value) {
  return (value == ' ') || (value == '\t') || (value == '\n') ||
         (value == '\r') || (value == '\v') || (value == '\f');
}

bool isDigit(const char value) {
  return (value >= '0') && (value <= '9');
}

}  // namespace

FanFlapController::FanFlapController(StepperPort& motor, EventSink& events,
                                     const ControllerConfig& config)
    : motor_(motor),
      events_(events),
      config_(config),
      state_(ControllerState::Init),
      stateTimestampMs_(0U),
      errorTimestampMs_(0U),
      deviceId_(1U),
      maxPosition_(0),
      targetPosition_(0),
      softMinPosition_(0),
      softMaxPosition_(0),
      homingStartPosition_(0),
      freeCheckStartPosition_(0),
      lastProgressPosition_(0),
      lastProgressMs_(kTimerNotStarted),
      freeCheckStartMs_(kTimerNotStarted),
      safePositionPermille_(config.safePositionPermille <= 1000U
                                ? config.safePositionPermille
                                : 1000U),
      stallGuardThreshold_(kDefaultStallGuardThreshold),
      lastFaultReason_(FaultReason::None),
      faultCount_(0U),
      softEndstopsEnabled_(true),
      valveFreeCheckActive_(false),
      motionSupervisionActive_(false),
      moveConfirmationPending_(false) {}

void FanFlapController::begin() {
  state_ = ControllerState::Init;
  stateTimestampMs_ = 0U;
  errorTimestampMs_ = 0U;
  maxPosition_ = 0;
  targetPosition_ = 0;
  softMinPosition_ = 0;
  softMaxPosition_ = 0;
  homingStartPosition_ = 0;
  softEndstopsEnabled_ = true;
  valveFreeCheckActive_ = false;
  motionSupervisionActive_ = false;
  moveConfirmationPending_ = false;
  freeCheckStartPosition_ = 0;
  lastProgressPosition_ = 0;
  lastProgressMs_ = kTimerNotStarted;
  freeCheckStartMs_ = kTimerNotStarted;
  lastFaultReason_ = FaultReason::None;

  motor_.setDriverEnabled(true);
  motor_.setMaxSpeed(config_.normalMaxSpeed);
  motor_.setAcceleration(config_.acceleration);
  emit(EventId::SystemStarted);
}

void FanFlapController::tick(const DigitalInputs& inputs,
                             const std::uint32_t nowMs) {
  switch (state_) {
    case ControllerState::Init:
      handleInitState(inputs);
      break;
    case ControllerState::HomingMin:
      handleHomingMinState(inputs, nowMs);
      break;
    case ControllerState::HomingMinSettling:
      handleHomingMinSettlingState(nowMs);
      break;
    case ControllerState::HomingMax:
      handleHomingMaxState(inputs, nowMs);
      break;
    case ControllerState::HomingMaxSettling:
      handleHomingMaxSettlingState(nowMs);
      break;
    case ControllerState::Ready:
      handleReadyState(inputs, nowMs);
      break;
    case ControllerState::ErrorDetected:
      handleErrorDetectedState(nowMs);
      break;
    case ControllerState::WaitReset:
      handleWaitResetState(nowMs);
      break;
    case ControllerState::AutoRehome:
      handleAutoRehomeState();
      break;
    case ControllerState::ServiceMotorTest:
      handleServiceMotorTestState(nowMs);
      break;
  }
}

void FanFlapController::handleCommand(const char* const command) {
  TextView text = trim(command);

  if (!applyAddressFilter(text)) {
    return;
  }

  handleCommandText(text);
}

ControllerState FanFlapController::state() const { return state_; }

std::uint8_t FanFlapController::deviceId() const { return deviceId_; }

bool FanFlapController::setDeviceId(const std::uint8_t deviceId) {
  if ((deviceId == 0U) || (deviceId > 247U)) {
    emit(EventId::InvalidDeviceId, deviceId);
    return false;
  }

  if (deviceId_ == deviceId) {
    return true;
  }

  deviceId_ = deviceId;
  emit(EventId::DeviceIdChanged, deviceId_);
  return true;
}

std::int32_t FanFlapController::currentPosition() const {
  return motor_.currentPosition();
}

std::int32_t FanFlapController::maxPosition() const { return maxPosition_; }

std::int32_t FanFlapController::targetPosition() const {
  return targetPosition_;
}

std::int32_t FanFlapController::softMinPosition() const {
  return softMinPosition_;
}

std::int32_t FanFlapController::softMaxPosition() const {
  return softMaxPosition_;
}

bool FanFlapController::softEndstopsEnabled() const {
  return softEndstopsEnabled_;
}

std::uint16_t FanFlapController::safePositionPermille() const {
  return safePositionPermille_;
}

std::uint8_t FanFlapController::stallGuardThreshold() const {
  return stallGuardThreshold_;
}

FaultReason FanFlapController::lastFaultReason() const {
  return lastFaultReason_;
}

std::uint16_t FanFlapController::faultCount() const { return faultCount_; }

bool FanFlapController::requestMoveTo(const std::int32_t position,
                                      std::int32_t& acceptedPosition) {
  if (!requireReady()) {
    return false;
  }

  acceptedPosition = moveTo(position);
  return true;
}

bool FanFlapController::setSoftEndstopsEnabled(const bool enabled) {
  softEndstopsEnabled_ = enabled;
  emit(enabled ? EventId::SoftEndstopsEnabled : EventId::SoftEndstopsDisabled);
  return true;
}

bool FanFlapController::setSoftEndstops(const SoftEndstopRange range) {
  std::int32_t boundedMin = range.minPosition;
  std::int32_t boundedMax = range.maxPosition;

  if (boundedMin < 0) {
    boundedMin = 0;
  }

  if (boundedMax > maxPosition_) {
    boundedMax = maxPosition_;
  }

  if (boundedMax < 0) {
    boundedMax = 0;
  }

  if (boundedMin >= boundedMax) {
    emit(EventId::SoftEndstopRangeInvalid, boundedMin, boundedMax);
    return false;
  }

  softMinPosition_ = boundedMin;
  softMaxPosition_ = boundedMax;
  emit(EventId::SoftEndstopsSet, softMinPosition_, softMaxPosition_);
  return true;
}

bool FanFlapController::setSafePositionPermille(const std::uint16_t permille) {
  if (permille > 1000U) {
    emit(EventId::SafePositionInvalid, permille);
    return false;
  }

  if (safePositionPermille_ == permille) {
    return true;
  }

  safePositionPermille_ = permille;
  emit(EventId::SafePositionChanged, safePositionPermille_);
  return true;
}

bool FanFlapController::setStallGuardThreshold(const std::uint16_t threshold) {
  if (threshold > kMaxStallGuardThreshold) {
    emit(EventId::StallGuardThresholdInvalid, threshold);
    return false;
  }

  const auto acceptedThreshold = static_cast<std::uint8_t>(threshold);
  if (stallGuardThreshold_ == acceptedThreshold) {
    return true;
  }

  stallGuardThreshold_ = acceptedThreshold;
  emit(EventId::StallGuardThresholdChanged, stallGuardThreshold_);
  return true;
}

std::int32_t FanFlapController::moveTo(const std::int32_t position) {
  std::int32_t acceptedPosition = position;

  if (!canMoveWithoutClamp(position)) {
    if (position < softMinPosition_) {
      acceptedPosition = softMinPosition_;
      emit(EventId::SoftEndstopMinClamped, acceptedPosition);
    } else {
      acceptedPosition = softMaxPosition_;
      emit(EventId::SoftEndstopMaxClamped, acceptedPosition);
    }
  }

  motor_.moveTo(acceptedPosition);
  targetPosition_ = acceptedPosition;
  beginMotionSupervision();
  beginValveFreeCheck();
  return acceptedPosition;
}

void FanFlapController::reportExternalFault(const FaultReason reason) {
  enterError(reason);
}

void FanFlapController::emit(const EventId eventId, const std::int32_t first,
                             const std::int32_t second, const bool flag,
                             const std::int32_t third,
                             const std::int32_t fourth,
                             const std::int32_t fifth) {
  const Event event{eventId, first, second, flag, third, fourth, fifth};
  events_.onEvent(event);
}

void FanFlapController::handleCommandText(const TextView& text) {
  TextView argument{nullptr, 0U, 0U};

  if (readArgument(text, "GOTO_DEG", argument)) {
    handleGotoDegreeCommand(argument);
  } else if (readArgument(text, "GOTO", argument)) {
    handleGotoCommand(argument);
  } else if (equals(text, "ID?")) {
    emit(EventId::DeviceIdReported, deviceId_);
  } else if (readArgument(text, "ID", argument) ||
             readArgument(text, "SETID", argument)) {
    handleDeviceIdCommand(argument);
  } else if (equals(text, "POS?")) {
    emit(EventId::PositionReported, currentPosition());
  } else if (equals(text, "DEG?")) {
    emit(EventId::DegreePositionReported,
         static_cast<std::int32_t>(degreeFromPosition(currentPosition())));
  } else if (equals(text, "RESET")) {
    handleResetCommand();
  } else if (equals(text, "REFRESH") || equals(text, "REFRESH MACHINE")) {
    refreshMachine();
  } else if (equals(text, "HOME")) {
    emit(EventId::ManualHomingStarted);
    resetMotor();
  } else if (readArgument(text, "SOFTMIN_DEG", argument)) {
    handleSoftMinDegreeCommand(argument);
  } else if (readArgument(text, "SOFTMIN", argument)) {
    handleSoftMinCommand(argument);
  } else if (readArgument(text, "SOFTMAX_DEG", argument)) {
    handleSoftMaxDegreeCommand(argument);
  } else if (readArgument(text, "SOFTMAX", argument)) {
    handleSoftMaxCommand(argument);
  } else if (equals(text, "SOFTENDSTOPS?")) {
    emit(EventId::SoftEndstopsStatus, softMinPosition_, softMaxPosition_,
         softEndstopsEnabled_);
  } else if (equals(text, "DEGLIMITS?")) {
    emit(EventId::DegreeLimitsReported,
         static_cast<std::int32_t>(degreeFromPosition(softMaxPosition_)),
         static_cast<std::int32_t>(degreeFromPosition(softMinPosition_)));
  } else if (readArgument(text, "SOFTENDSTOPS", argument)) {
    handleSoftEndstopsCommand(argument);
  } else if (equals(text, "SAFE?")) {
    emit(EventId::SafePositionReported, safePositionPermille_);
  } else if (readArgument(text, "SAFE", argument)) {
    handleSafePositionCommand(argument);
  } else if (equals(text, "STALLGUARD?")) {
    emit(EventId::StallGuardThresholdReported, stallGuardThreshold_);
  } else if (readArgument(text, "STALLGUARD", argument)) {
    handleStallGuardThresholdCommand(argument);
  } else if (readArgument(text, "MOTORTEST", argument)) {
    handleMotorTestCommand(argument);
  } else if (equals(text, "FAULT?")) {
    emit(EventId::FaultReported,
         static_cast<std::int32_t>(lastFaultReason_), faultCount_);
  } else if (equals(text, "DIAG?")) {
    emit(EventId::DiagnosticsReported,
         static_cast<std::int32_t>(lastFaultReason_), faultCount_);
  } else if (equals(text, "SELFTEST?")) {
    emit(EventId::SelfTestReported, static_cast<std::int32_t>(state_),
         static_cast<std::int32_t>(lastFaultReason_), softEndstopsEnabled_,
         faultCount_, deviceId_, safePositionPermille_);
  } else {
    emit(EventId::UnknownCommand);
  }
}

void FanFlapController::handleResetCommand() {
  if (state_ == ControllerState::WaitReset) {
    emit(EventId::ResetDuringWait);
    resetMotor();
  } else {
    emit(EventId::ResetIgnored);
  }
}

void FanFlapController::handleGotoDegreeCommand(const TextView& argument) {
  std::int32_t value = 0;
  std::int32_t acceptedTarget = 0;

  if (!parseInt32(argument, value) || (value < 0) ||
      (value > static_cast<std::int32_t>(kMaxDegree))) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if (!requestMoveTo(positionFromDegree(static_cast<std::uint16_t>(value)),
                     acceptedTarget)) {
    return;
  }

  emit(EventId::MoveAccepted, acceptedTarget);
}

void FanFlapController::handleMotorTestCommand(const TextView& argument) {
  std::int32_t steps = 0;

  if (!parseInt32(argument, steps) || (steps == 0) ||
      (steps < -kMaxMotorTestSteps) || (steps > kMaxMotorTestSteps)) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if ((state_ != ControllerState::WaitReset) &&
      (state_ != ControllerState::ErrorDetected)) {
    emit(EventId::MotorNotReady);
    return;
  }

  motor_.stop();
  motor_.setDriverEnabled(true);
  motor_.setMaxSpeed(config_.homingMaxSpeed);
  motor_.setCurrentPosition(0);
  motor_.moveTo(steps);
  targetPosition_ = steps;
  valveFreeCheckActive_ = false;
  motionSupervisionActive_ = false;
  moveConfirmationPending_ = false;
  state_ = ControllerState::ServiceMotorTest;
  emit(EventId::MotorTestStarted, steps);
}

void FanFlapController::handleInitState(const DigitalInputs& inputs) {
  if (inputs.minSwitchActive && inputs.maxSwitchActive) {
    enterError(FaultReason::BothEndstopsActiveAtBoot);
    return;
  }

  startHomingMin();
}

void FanFlapController::handleHomingMinState(const DigitalInputs& inputs,
                                             const std::uint32_t nowMs) {
  motor_.run();

  if (!inputs.minSwitchActive) {
    if (inputs.stallGuardActive && stallGuardTravelArmed()) {
      motor_.stop();
      motor_.setCurrentPosition(0);
      stateTimestampMs_ = nowMs;
      state_ = ControllerState::HomingMinSettling;
      emit(EventId::SensorlessMinPositionReached);
      return;
    }

    if (homingMinTravelExceeded()) {
      emit(EventId::HomingRangeInvalid, currentPosition());
      enterError(FaultReason::HomingMinTravelExceeded);
    }
    return;
  }

  motor_.stop();
  motor_.setCurrentPosition(0);
  stateTimestampMs_ = nowMs;
  state_ = ControllerState::HomingMinSettling;
  emit(EventId::MinPositionReached);
}

void FanFlapController::handleHomingMinSettlingState(
    const std::uint32_t nowMs) {
  if (hasElapsed(nowMs, stateTimestampMs_, config_.settleDelayMs)) {
    startHomingMax();
  }
}

void FanFlapController::handleHomingMaxState(const DigitalInputs& inputs,
                                             const std::uint32_t nowMs) {
  motor_.run();

  if (!inputs.maxSwitchActive) {
    if (inputs.stallGuardActive && stallGuardTravelArmed()) {
      motor_.stop();
      maxPosition_ = motor_.currentPosition();
      emit(EventId::SensorlessMaxPositionReached, maxPosition_);

      if (maxPosition_ <= 0) {
        emit(EventId::HomingRangeInvalid, maxPosition_);
        enterError(FaultReason::HomingRangeInvalid);
        return;
      }

      softMinPosition_ = 0;
      softMaxPosition_ = maxPosition_;
      emit(EventId::SoftEndstopsSet, softMinPosition_, softMaxPosition_);
      stateTimestampMs_ = nowMs;
      state_ = ControllerState::HomingMaxSettling;
      return;
    }

    if (homingMaxTravelExceeded()) {
      emit(EventId::HomingRangeInvalid, currentPosition());
      enterError(FaultReason::HomingMaxTravelExceeded);
    }
    return;
  }

  motor_.stop();
  maxPosition_ = motor_.currentPosition();
  emit(EventId::MaxPositionReached, maxPosition_);

  if (maxPosition_ <= 0) {
    emit(EventId::HomingRangeInvalid, maxPosition_);
    enterError(FaultReason::HomingRangeInvalid);
    return;
  }

  softMinPosition_ = 0;
  softMaxPosition_ = maxPosition_;
  emit(EventId::SoftEndstopsSet, softMinPosition_, softMaxPosition_);
  stateTimestampMs_ = nowMs;
  state_ = ControllerState::HomingMaxSettling;
}

void FanFlapController::handleHomingMaxSettlingState(
    const std::uint32_t nowMs) {
  if (hasElapsed(nowMs, stateTimestampMs_, config_.settleDelayMs)) {
    const std::int32_t safePosition =
        positionFromPermille(safePositionPermille_);
    static_cast<void>(moveTo(safePosition));
    emit(EventId::SafePositionApplied, safePosition,
         static_cast<std::int32_t>(safePositionPermille_));
    state_ = ControllerState::Ready;
  }
}

void FanFlapController::handleReadyState(const DigitalInputs& inputs,
                                         const std::uint32_t nowMs) {
  motor_.run();

  const FaultReason unexpectedReason = unexpectedSwitchReason(inputs);
  if (unexpectedReason != FaultReason::None) {
    enterError(unexpectedReason);
    return;
  }

  updateValveFreeCheck(nowMs);
  if (state_ != ControllerState::Ready) {
    return;
  }

  if (inputs.stallGuardActive && !valveFreeCheckActive_) {
    emit(EventId::StallDetected);
    enterError(FaultReason::StallGuardDuringMove);
  } else {
    updateMotionSupervision(nowMs);
  }
}

void FanFlapController::handleErrorDetectedState(const std::uint32_t nowMs) {
  motor_.stop();
  motor_.setDriverEnabled(false);
  errorTimestampMs_ = nowMs;
  state_ = ControllerState::WaitReset;
  emit(EventId::ErrorMotorStopped);
}

void FanFlapController::handleWaitResetState(const std::uint32_t nowMs) {
  if (hasElapsed(nowMs, errorTimestampMs_, config_.resetTimeoutMs)) {
    emit(EventId::AutoRehomeTimeout);
    resetMotor();
  }
}

void FanFlapController::handleAutoRehomeState() {
  motor_.setDriverEnabled(true);
  startHomingMin();
}

void FanFlapController::handleServiceMotorTestState(const std::uint32_t nowMs) {
  motor_.run();

  if (currentPosition() == targetPosition_) {
    motor_.stop();
    motor_.setCurrentPosition(currentPosition());
    motor_.setDriverEnabled(false);
    errorTimestampMs_ = nowMs;
    state_ = ControllerState::WaitReset;
    emit(EventId::MotorTestFinished, targetPosition_);
  }
}

void FanFlapController::startHomingMin() {
  valveFreeCheckActive_ = false;
  motionSupervisionActive_ = false;
  moveConfirmationPending_ = false;
  homingStartPosition_ = currentPosition();
  motor_.setMaxSpeed(config_.homingMaxSpeed);
  motor_.moveTo(-homingTravelSteps());
  state_ = ControllerState::HomingMin;
  emit(EventId::HomingStarted);
}

void FanFlapController::startHomingMax() {
  valveFreeCheckActive_ = false;
  motionSupervisionActive_ = false;
  moveConfirmationPending_ = false;
  homingStartPosition_ = currentPosition();
  motor_.setMaxSpeed(config_.homingMaxSpeed);
  motor_.moveTo(homingTravelSteps());
  state_ = ControllerState::HomingMax;
}

void FanFlapController::resetMotor() { state_ = ControllerState::AutoRehome; }

void FanFlapController::refreshMachine() {
  motor_.stop();
  valveFreeCheckActive_ = false;
  motionSupervisionActive_ = false;
  moveConfirmationPending_ = false;
  emit(EventId::MachineRefreshStarted);
  resetMotor();
}

void FanFlapController::enterError(const FaultReason reason) {
  if ((state_ == ControllerState::ErrorDetected) ||
      (state_ == ControllerState::WaitReset) ||
      (state_ == ControllerState::ServiceMotorTest)) {
    return;
  }

  motor_.stop();
  motor_.setCurrentPosition(motor_.currentPosition());
  motor_.setDriverEnabled(false);
  valveFreeCheckActive_ = false;
  motionSupervisionActive_ = false;
  moveConfirmationPending_ = false;
  lastFaultReason_ = reason;
  if (faultCount_ < 0xFFFFU) {
    ++faultCount_;
  }
  state_ = ControllerState::ErrorDetected;
}

bool FanFlapController::requireReady() {
  if (state_ == ControllerState::Ready) {
    return true;
  }

  emit(EventId::MotorNotReady);
  return false;
}

bool FanFlapController::homingMinTravelExceeded() const {
  return currentPosition() <= -homingTravelSteps();
}

bool FanFlapController::homingMaxTravelExceeded() const {
  return currentPosition() >= homingTravelSteps();
}

bool FanFlapController::stallGuardTravelArmed() const {
  return absoluteDistance(currentPosition(), homingStartPosition_) >=
         stallGuardActivationTravelSteps();
}

std::int32_t FanFlapController::stallGuardActivationTravelSteps() const {
  if (config_.freeCheckSteps > 0) {
    return config_.freeCheckSteps;
  }

  return motionProgressMinSteps();
}

std::int32_t FanFlapController::positionFromPermille(
    const std::uint16_t permille) const {
  if (maxPosition_ <= 0) {
    return 0;
  }

  return static_cast<std::int32_t>(
      (static_cast<std::int64_t>(maxPosition_) * permille) / 1000LL);
}

std::uint16_t FanFlapController::permilleFromPosition(
    const std::int32_t position) const {
  if (maxPosition_ <= 0) {
    return 0U;
  }

  if (position <= 0) {
    return 0U;
  }

  if (position >= maxPosition_) {
    return kMaxPermille;
  }

  return static_cast<std::uint16_t>(
      (static_cast<std::int64_t>(position) * kMaxPermille) / maxPosition_);
}

void FanFlapController::beginMotionSupervision() {
  lastProgressPosition_ = currentPosition();
  lastProgressMs_ = kTimerNotStarted;
  motionSupervisionActive_ = targetPosition_ != currentPosition();
  moveConfirmationPending_ = true;
}

void FanFlapController::updateMotionSupervision(const std::uint32_t nowMs) {
  const std::int32_t current = currentPosition();
  if (moveConfirmationPending_ && (current == targetPosition_)) {
    moveConfirmationPending_ = false;
    motionSupervisionActive_ = false;
    emit(EventId::MoveReached, targetPosition_,
         static_cast<std::int32_t>(permilleFromPosition(current)), false,
         static_cast<std::int32_t>(degreeFromPosition(current)));
    return;
  }

  if (!motionSupervisionActive_) {
    return;
  }

  if (lastProgressMs_ == kTimerNotStarted) {
    lastProgressPosition_ = current;
    lastProgressMs_ = nowMs;
    return;
  }

  if (absoluteDistance(current, lastProgressPosition_) >=
      motionProgressMinSteps()) {
    lastProgressPosition_ = current;
    lastProgressMs_ = nowMs;
    return;
  }

  if (hasElapsed(nowMs, lastProgressMs_, config_.motionNoProgressTimeoutMs)) {
    enterError(FaultReason::MotionNoProgress);
  }
}

void FanFlapController::beginValveFreeCheck() {
  if ((config_.freeCheckSteps <= 0) || (targetPosition_ == currentPosition())) {
    valveFreeCheckActive_ = false;
    freeCheckStartMs_ = kTimerNotStarted;
    return;
  }

  freeCheckStartPosition_ = currentPosition();
  freeCheckStartMs_ = kTimerNotStarted;
  valveFreeCheckActive_ = true;
  emit(EventId::ValveFreeCheckStarted, freeCheckStartPosition_,
       targetPosition_);
}

void FanFlapController::updateValveFreeCheck(const std::uint32_t nowMs) {
  if (!valveFreeCheckActive_) {
    return;
  }

  if (freeCheckStartMs_ == kTimerNotStarted) {
    freeCheckStartMs_ = nowMs;
  }

  if (absoluteDistance(currentPosition(), freeCheckStartPosition_) >=
      config_.freeCheckSteps) {
    valveFreeCheckActive_ = false;
    emit(EventId::ValveFreeCheckPassed, currentPosition(), targetPosition_);
    return;
  }

  if (hasElapsed(nowMs, freeCheckStartMs_, config_.freeCheckTimeoutMs)) {
    enterError(FaultReason::ValveBlockedDuringFreeCheck);
  }
}

bool FanFlapController::applyAddressFilter(TextView& text) {
  std::uint32_t parsedAddress = 0U;
  std::size_t index = 2U;

  if ((text.length < 3U) || (charAt(text, 0U) != 'I') ||
      (charAt(text, 1U) != 'D') || !isDigit(charAt(text, 2U))) {
    return true;
  }

  while ((index < text.length) && isDigit(charAt(text, index))) {
    const auto digit = static_cast<std::uint32_t>(charAt(text, index) - '0');

    if (parsedAddress > ((247UL - digit) / 10UL)) {
      emit(EventId::InvalidDeviceId);
      return false;
    }

    parsedAddress = (parsedAddress * 10UL) + digit;
    ++index;
  }

  if ((parsedAddress == 0U) || (parsedAddress > 247U)) {
    emit(EventId::InvalidDeviceId, static_cast<std::int32_t>(parsedAddress));
    return false;
  }

  if ((index < text.length) && !isSpace(charAt(text, index))) {
    emit(EventId::InvalidDeviceId, static_cast<std::int32_t>(parsedAddress));
    return false;
  }

  if (parsedAddress != deviceId_) {
    return false;
  }

  text = trim(subview(text, index));
  return true;
}

void FanFlapController::handleGotoCommand(const TextView& argument) {
  std::int32_t value = 0;
  std::int32_t acceptedTarget = 0;

  if (!parseInt32(argument, value)) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if (!requestMoveTo(value, acceptedTarget)) {
    return;
  }

  emit(EventId::MoveAccepted, acceptedTarget);
}

void FanFlapController::handleSoftMinCommand(const TextView& argument) {
  std::int32_t value = 0;

  if (!parseInt32(argument, value)) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if (requireReady()) {
    static_cast<void>(
        setSoftEndstops(SoftEndstopRange{value, softMaxPosition_}));
  }
}

void FanFlapController::handleSoftMaxCommand(const TextView& argument) {
  std::int32_t value = 0;

  if (!parseInt32(argument, value)) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if (requireReady()) {
    static_cast<void>(
        setSoftEndstops(SoftEndstopRange{softMinPosition_, value}));
  }
}

void FanFlapController::handleSoftMinDegreeCommand(const TextView& argument) {
  std::int32_t value = 0;

  if (!parseInt32(argument, value) || (value < 0) ||
      (value > static_cast<std::int32_t>(kMaxDegree))) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if (requireReady()) {
    static_cast<void>(setSoftEndstopsDegrees(
        static_cast<std::uint16_t>(value),
        degreeFromPosition(softMinPosition_)));
  }
}

void FanFlapController::handleSoftMaxDegreeCommand(const TextView& argument) {
  std::int32_t value = 0;

  if (!parseInt32(argument, value) || (value < 0) ||
      (value > static_cast<std::int32_t>(kMaxDegree))) {
    emit(EventId::InvalidCommandArgument);
    return;
  }

  if (requireReady()) {
    static_cast<void>(setSoftEndstopsDegrees(
        degreeFromPosition(softMaxPosition_),
        static_cast<std::uint16_t>(value)));
  }
}

void FanFlapController::handleSoftEndstopsCommand(const TextView& argument) {
  if (equals(argument, "ON")) {
    static_cast<void>(setSoftEndstopsEnabled(true));
  } else if (equals(argument, "OFF")) {
    static_cast<void>(setSoftEndstopsEnabled(false));
  } else {
    emit(EventId::InvalidCommandArgument);
  }
}

void FanFlapController::handleDeviceIdCommand(const TextView& argument) {
  std::uint8_t value = 0U;

  if (!parseAddress(argument, value)) {
    emit(EventId::InvalidDeviceId);
    return;
  }

  static_cast<void>(setDeviceId(value));
}

void FanFlapController::handleSafePositionCommand(const TextView& argument) {
  std::int32_t value = 0;

  if (!parseInt32(argument, value) || (value < 0) || (value > 1000)) {
    emit(EventId::SafePositionInvalid, value);
    return;
  }

  static_cast<void>(
      setSafePositionPermille(static_cast<std::uint16_t>(value)));
}

void FanFlapController::handleStallGuardThresholdCommand(
    const TextView& argument) {
  std::int32_t value = 0;

  if (!parseInt32(argument, value) || (value < 0) ||
      (value > static_cast<std::int32_t>(kMaxStallGuardThreshold))) {
    emit(EventId::StallGuardThresholdInvalid, value);
    return;
  }

  static_cast<void>(
      setStallGuardThreshold(static_cast<std::uint16_t>(value)));
}

bool FanFlapController::setSoftEndstopsDegrees(
    const std::uint16_t minDegree, const std::uint16_t maxDegree) {
  if ((minDegree > kMaxDegree) || (maxDegree > kMaxDegree) ||
      (minDegree > maxDegree)) {
    emit(EventId::SoftEndstopRangeInvalid, minDegree, maxDegree);
    return false;
  }

  return setSoftEndstops(SoftEndstopRange{positionFromDegree(maxDegree),
                                          positionFromDegree(minDegree)});
}

FaultReason FanFlapController::unexpectedSwitchReason(
    const DigitalInputs& inputs) const {
  const float currentSpeed = motor_.speed();

  if (inputs.minSwitchActive && (currentSpeed > 0.0F)) {
    return FaultReason::UnexpectedMinSwitch;
  }

  if (inputs.maxSwitchActive && (currentSpeed < 0.0F)) {
    return FaultReason::UnexpectedMaxSwitch;
  }

  return FaultReason::None;
}

bool FanFlapController::canMoveWithoutClamp(
    const std::int32_t position) const {
  if (!softEndstopsEnabled_) {
    return true;
  }

  return (position >= softMinPosition_) && (position <= softMaxPosition_);
}

std::int32_t FanFlapController::homingTravelSteps() const {
  if (config_.homingTravelSteps <= 0) {
    return 1;
  }

  return config_.homingTravelSteps;
}

std::int32_t FanFlapController::motionProgressMinSteps() const {
  if (config_.motionProgressMinSteps <= 0) {
    return 1;
  }

  return config_.motionProgressMinSteps;
}

std::int32_t FanFlapController::positionFromDegree(
    const std::uint16_t degree) const {
  if (maxPosition_ <= 0) {
    return 0;
  }

  if (degree >= kMaxDegree) {
    return 0;
  }

  return static_cast<std::int32_t>(
      (static_cast<std::int64_t>(maxPosition_) *
       (static_cast<std::int64_t>(kMaxDegree) - degree)) /
      kMaxDegree);
}

std::uint16_t FanFlapController::degreeFromPosition(
    const std::int32_t position) const {
  if (maxPosition_ <= 0) {
    return 0U;
  }

  if (position <= 0) {
    return kMaxDegree;
  }

  if (position >= maxPosition_) {
    return 0U;
  }

  return static_cast<std::uint16_t>(
      (static_cast<std::int64_t>(maxPosition_ - position) * kMaxDegree) /
      maxPosition_);
}

FanFlapController::TextView FanFlapController::trim(const char* const text) {
  return trim(TextView{text, 0U, stringLength(text)});
}

FanFlapController::TextView FanFlapController::trim(const TextView& text) {
  std::size_t beginOffset = 0U;
  std::size_t endOffset = text.length;

  if (text.data == nullptr) {
    return TextView{nullptr, 0U, 0U};
  }

  while ((beginOffset < text.length) && isSpace(charAt(text, beginOffset))) {
    ++beginOffset;
  }

  while ((endOffset > beginOffset) && isSpace(charAt(text, endOffset - 1U))) {
    --endOffset;
  }

  return TextView{text.data, text.offset + beginOffset,
                  endOffset - beginOffset};
}

bool FanFlapController::equals(const TextView& text, const char* expected) {
  const std::size_t expectedLength = stringLength(expected);

  if (text.length != expectedLength) {
    return false;
  }

  for (std::size_t index = 0U; index < expectedLength; ++index) {
    if (charAt(text, index) != expected[index]) {
      return false;
    }
  }

  return true;
}

bool FanFlapController::readArgument(const TextView& text, const char* command,
                                     TextView& argument) {
  const std::size_t commandLength = stringLength(command);

  argument = TextView{nullptr, 0U, 0U};

  if (text.length < commandLength) {
    return false;
  }

  for (std::size_t index = 0U; index < commandLength; ++index) {
    if (charAt(text, index) != command[index]) {
      return false;
    }
  }

  if (text.length == commandLength) {
    return true;
  }

  if (!isSpace(charAt(text, commandLength))) {
    return false;
  }

  argument = trim(subview(text, commandLength + 1U));
  return true;
}

bool FanFlapController::parseInt32(const TextView& text, std::int32_t& value) {
  const TextView trimmed = trim(text);
  std::size_t index = 0U;
  bool negative = false;
  std::uint32_t magnitude = 0U;
  auto limit =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());

  value = 0;

  if (trimmed.length == 0U) {
    return false;
  }

  if ((charAt(trimmed, index) == '-') || (charAt(trimmed, index) == '+')) {
    negative = charAt(trimmed, index) == '-';
    ++index;
  }

  if (index >= trimmed.length) {
    return false;
  }

  if (negative) {
    limit =
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) +
        1UL;
  }

  for (; index < trimmed.length; ++index) {
    if (!isDigit(charAt(trimmed, index))) {
      return false;
    }

    const auto digit = static_cast<std::uint32_t>(charAt(trimmed, index) - '0');

    if (magnitude > ((limit - digit) / 10UL)) {
      return false;
    }

    magnitude = (magnitude * 10UL) + digit;
  }

  if (negative) {
    const auto signedValue = -static_cast<std::int64_t>(magnitude);
    value = static_cast<std::int32_t>(signedValue);
  } else {
    value = static_cast<std::int32_t>(magnitude);
  }

  return true;
}

bool FanFlapController::parseUint8(const TextView& text, std::uint8_t& value) {
  std::int32_t parsedValue = 0;

  value = 0U;

  if (!parseInt32(text, parsedValue)) {
    return false;
  }

  if ((parsedValue < 0) || (parsedValue > 255)) {
    return false;
  }

  value = static_cast<std::uint8_t>(parsedValue);
  return true;
}

bool FanFlapController::parseAddress(const TextView& text,
                                     std::uint8_t& value) {
  if (!parseUint8(text, value)) {
    return false;
  }

  return (value >= 1U) && (value <= 247U);
}

char FanFlapController::charAt(const TextView& text, const std::size_t index) {
  return text.data[text.offset + index];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

FanFlapController::TextView FanFlapController::subview(
    const TextView& text, const std::size_t offset) {
  if (offset >= text.length) {
    return TextView{text.data, text.offset + text.length, 0U};
  }

  return TextView{text.data, text.offset + offset, text.length - offset};
}

std::size_t FanFlapController::stringLength(const char* const text) {
  std::size_t length = 0U;

  if (text == nullptr) {
    return 0U;
  }

  while (text[length] != '\0') {
    ++length;
  }

  return length;
}

bool FanFlapController::hasElapsed(const std::uint32_t nowMs,
                                   const std::uint32_t sinceMs,
                                   const std::uint32_t durationMs) {
  return static_cast<std::uint32_t>(nowMs - sinceMs) >= durationMs;
}

std::int32_t FanFlapController::absoluteDistance(const std::int32_t first,
                                                 const std::int32_t second) {
  const std::int64_t distance =
      static_cast<std::int64_t>(first) - static_cast<std::int64_t>(second);
  const std::int64_t absolute = distance < 0 ? -distance : distance;

  if (absolute > std::numeric_limits<std::int32_t>::max()) {
    return std::numeric_limits<std::int32_t>::max();
  }

  return static_cast<std::int32_t>(absolute);
}

}  // namespace luefterklappe

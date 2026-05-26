#include "StepDirStepper.h"

#include <cmath>

namespace luefterklappe {

StepDirStepper::StepDirStepper(StepDirIo& stepIo) : io_(stepIo) {}

void StepDirStepper::begin() {
  io_.beginStepDirOutputs();
  const std::uint32_t nowUs = io_.micros();
  lastRunUs_ = nowUs;
  lastStepUs_ = nowUs;
}

void StepDirStepper::setMinPulseWidth(const unsigned int pulseWidthUs) {
  minPulseWidthUs_ = (pulseWidthUs > 0U) ? pulseWidthUs : 1U;
}

void StepDirStepper::setMaxSpeed(const float speed) {
  maxSpeed_ = (speed > 0.0F) ? speed : 1.0F;
  const float currentSpeed = absolute(speed_);
  if (currentSpeed > maxSpeed_) {
    speed_ = (speed_ < 0.0F) ? -maxSpeed_ : maxSpeed_;
  }
}

void StepDirStepper::setAcceleration(const float acceleration) {
  acceleration_ = (acceleration > 0.0F) ? acceleration : 1.0F;
}

void StepDirStepper::moveTo(const std::int32_t position) {
  targetPosition_ = position;
  lastRunUs_ = io_.micros();
}

void StepDirStepper::run() {
  const std::int64_t distance =
      static_cast<std::int64_t>(targetPosition_) -
      static_cast<std::int64_t>(currentPosition_);
  if (distance == 0) {
    speed_ = 0.0F;
    lastRunUs_ = io_.micros();
    return;
  }

  const std::uint32_t nowUs = io_.micros();
  const std::int8_t direction = (distance > 0) ? 1 : -1;
  updateSpeed(SpeedUpdate{nowUs, direction, absoluteDistance(distance)});

  const float currentSpeed = absolute(speed_);
  if (currentSpeed <= 0.0F) {
    return;
  }

  if (elapsedMicros(lastStepUs_, nowUs) < stepIntervalUs(currentSpeed)) {
    return;
  }

  stepOnce(direction);
  currentPosition_ += direction;
  if (currentPosition_ == targetPosition_) {
    speed_ = 0.0F;
  }
}

void StepDirStepper::stop() {
  targetPosition_ = currentPosition_;
  speed_ = 0.0F;
  const std::uint32_t nowUs = io_.micros();
  lastRunUs_ = nowUs;
  lastStepUs_ = nowUs;
}

void StepDirStepper::idle() {
  speed_ = 0.0F;
  lastRunUs_ = io_.micros();
}

void StepDirStepper::setCurrentPosition(const std::int32_t position) {
  currentPosition_ = position;
  targetPosition_ = position;
  speed_ = 0.0F;
  const std::uint32_t nowUs = io_.micros();
  lastRunUs_ = nowUs;
  lastStepUs_ = nowUs;
}

std::int32_t StepDirStepper::currentPosition() const {
  return currentPosition_;
}

float StepDirStepper::speed() const { return speed_; }

float StepDirStepper::absolute(const float value) {
  return (value < 0.0F) ? -value : value;
}

std::int64_t StepDirStepper::absoluteDistance(const std::int64_t value) {
  return (value < 0) ? -value : value;
}

std::uint32_t StepDirStepper::elapsedMicros(const std::uint32_t sinceUs,
                                            const std::uint32_t nowUs) {
  return static_cast<std::uint32_t>(nowUs - sinceUs);
}

float StepDirStepper::startingSpeed() const {
  const auto intervalSeconds =
      0.676F * static_cast<float>(std::sqrt(2.0F / acceleration_));
  const float startSpeed =
      (intervalSeconds > 0.0F) ? (1.0F / intervalSeconds) : maxSpeed_;
  return (startSpeed < maxSpeed_) ? startSpeed : maxSpeed_;
}

float StepDirStepper::distanceLimitedSpeed(
    const std::int64_t distance) const {
  const auto distanceAsFloat = static_cast<float>(distance);
  const auto stopLimitedSpeed =
      static_cast<float>(std::sqrt(2.0F * acceleration_ * distanceAsFloat));
  float limitedSpeed =
      (stopLimitedSpeed < maxSpeed_) ? stopLimitedSpeed : maxSpeed_;
  const float minimumSpeed = startingSpeed();
  if (limitedSpeed < minimumSpeed) {
    limitedSpeed = minimumSpeed;
  }

  return limitedSpeed;
}

void StepDirStepper::updateSpeed(const SpeedUpdate& update) {
  const std::uint32_t elapsedUs = elapsedMicros(lastRunUs_, update.nowUs);
  lastRunUs_ = update.nowUs;

  float currentSpeed = absolute(speed_);
  if (((speed_ < 0.0F) && (update.direction > 0)) ||
      ((speed_ > 0.0F) && (update.direction < 0))) {
    currentSpeed = 0.0F;
  }

  const float targetSpeed = distanceLimitedSpeed(update.distance);
  const float accelerationStep =
      acceleration_ * (static_cast<float>(elapsedUs) / 1000000.0F);

  if (currentSpeed < startingSpeed()) {
    currentSpeed = startingSpeed();
  } else if (currentSpeed < targetSpeed) {
    currentSpeed += accelerationStep;
    if (currentSpeed > targetSpeed) {
      currentSpeed = targetSpeed;
    }
  } else if (currentSpeed > targetSpeed) {
    currentSpeed -= accelerationStep;
    if (currentSpeed < targetSpeed) {
      currentSpeed = targetSpeed;
    }
  }

  speed_ = (update.direction > 0) ? currentSpeed : -currentSpeed;
}

std::uint32_t StepDirStepper::stepIntervalUs(const float speed) {
  const float interval = 1000000.0F / speed;
  if (interval < 1.0F) {
    return 1U;
  }

  return static_cast<std::uint32_t>(interval);
}

void StepDirStepper::stepOnce(const std::int8_t direction) {
  io_.setDirPin(direction > 0);
  io_.delayMicroseconds(1U);
  io_.setStepPin(true);
  io_.delayMicroseconds(minPulseWidthUs_);
  io_.setStepPin(false);
  lastStepUs_ = io_.micros();
}

}  // namespace luefterklappe

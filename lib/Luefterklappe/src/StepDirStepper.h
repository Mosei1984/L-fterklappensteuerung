#ifndef LUEFTERKLAPPE_STEP_DIR_STEPPER_H
#define LUEFTERKLAPPE_STEP_DIR_STEPPER_H

#include <cstdint>

namespace luefterklappe {

class StepDirIo {
 public:
  virtual ~StepDirIo() = default;
  virtual void beginStepDirOutputs() = 0;
  virtual void setStepPin(bool high) = 0;
  virtual void setDirPin(bool high) = 0;
  virtual void delayMicroseconds(std::uint32_t durationUs) = 0;
  virtual std::uint32_t micros() const = 0;
};

class StepDirStepper {
 public:
  explicit StepDirStepper(StepDirIo& stepIo);

  void begin();
  void setMinPulseWidth(unsigned int pulseWidthUs);
  void setMaxSpeed(float speed);
  void setAcceleration(float acceleration);
  void moveTo(std::int32_t position);
  void run();
  void stop();
  void idle();
  void setCurrentPosition(std::int32_t position);
  std::int32_t currentPosition() const;
  float speed() const;

 private:
  struct SpeedUpdate {
    std::uint32_t nowUs;
    std::int8_t direction;
    std::int64_t distance;
  };

  static float absolute(float value);
  static std::int64_t absoluteDistance(std::int64_t value);
  static std::uint32_t elapsedMicros(std::uint32_t sinceUs,
                                     std::uint32_t nowUs);
  static std::uint32_t stepIntervalUs(float speed);

  float startingSpeed() const;
  float distanceLimitedSpeed(std::int64_t distance) const;
  void updateSpeed(const SpeedUpdate& update);
  void stepOnce(std::int8_t direction);

  StepDirIo& io_;
  unsigned int minPulseWidthUs_{20U};
  float maxSpeed_{400.0F};
  float acceleration_{1000.0F};
  float speed_{0.0F};
  std::int32_t currentPosition_{0};
  std::int32_t targetPosition_{0};
  std::uint32_t lastRunUs_{0U};
  std::uint32_t lastStepUs_{0U};
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_STEP_DIR_STEPPER_H

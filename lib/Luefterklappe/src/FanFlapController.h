#ifndef LUEFTERKLAPPE_FAN_FLAP_CONTROLLER_H
#define LUEFTERKLAPPE_FAN_FLAP_CONTROLLER_H

#include <cstddef>
#include <cstdint>

namespace luefterklappe {

enum class ControllerState : std::uint8_t {
  Init,
  HomingMin,
  HomingMinSettling,
  HomingMax,
  HomingMaxSettling,
  Ready,
  ErrorDetected,
  WaitReset,
  AutoRehome
};

enum class EventId : std::uint8_t {
  SystemStarted,
  HomingStarted,
  MinPositionReached,
  MaxPositionReached,
  HomingRangeInvalid,
  SoftEndstopsSet,
  SoftEndstopMinClamped,
  SoftEndstopMaxClamped,
  SoftEndstopRangeInvalid,
  MoveAccepted,
  MotorNotReady,
  PositionReported,
  ResetDuringWait,
  ResetIgnored,
  ManualHomingStarted,
  SoftEndstopsEnabled,
  SoftEndstopsDisabled,
  SoftEndstopsStatus,
  DeviceIdReported,
  DeviceIdChanged,
  InvalidDeviceId,
  SafePositionReported,
  SafePositionChanged,
  SafePositionInvalid,
  SafePositionApplied,
  SensorlessMinPositionReached,
  SensorlessMaxPositionReached,
  ValveFreeCheckStarted,
  ValveFreeCheckPassed,
  UnknownCommand,
  InvalidCommandArgument,
  ErrorMotorStopped,
  AutoRehomeTimeout,
  StallDetected,
  TmcInitializationStarted,
  TmcConfigured
};

struct Event {
  EventId id;
  std::int32_t first;
  std::int32_t second;
  bool flag;
};

class EventSink {
 public:
  virtual ~EventSink() = default;
  virtual void onEvent(const Event& event) = 0;
};

class StepperPort {
 public:
  virtual ~StepperPort() = default;
  virtual void setDriverEnabled(bool enabled) = 0;
  virtual void setMaxSpeed(float speed) = 0;
  virtual void setAcceleration(float acceleration) = 0;
  virtual void moveTo(std::int32_t position) = 0;
  virtual void run() = 0;
  virtual void stop() = 0;
  virtual void setCurrentPosition(std::int32_t position) = 0;
  virtual std::int32_t currentPosition() const = 0;
  virtual float speed() const = 0;
};

struct DigitalInputs {
  bool minSwitchActive;
  bool maxSwitchActive;
  bool stallGuardActive;
};

struct ControllerConfig {
  std::int32_t homingTravelSteps;
  std::uint32_t settleDelayMs;
  std::uint32_t resetTimeoutMs;
  float normalMaxSpeed;
  float homingMaxSpeed;
  float acceleration;
  std::uint16_t safePositionPermille;
  std::int32_t freeCheckSteps;
};

struct SoftEndstopRange {
  std::int32_t minPosition;
  std::int32_t maxPosition;
};

constexpr ControllerConfig kDefaultControllerConfig{
    100000L,
    500UL,
    10UL * 60UL * 1000UL,
    400.0F,
    200.0F,
    1000.0F,
    1000U,
    20L};

class FanFlapController {
 public:
  FanFlapController(StepperPort& motor, EventSink& events,
                    const ControllerConfig& config = kDefaultControllerConfig);

  void begin();
  void tick(const DigitalInputs& inputs, std::uint32_t nowMs);
  void handleCommand(const char* command);

  ControllerState state() const;
  std::uint8_t deviceId() const;
  bool setDeviceId(std::uint8_t deviceId);
  std::int32_t currentPosition() const;
  std::int32_t maxPosition() const;
  std::int32_t targetPosition() const;
  std::int32_t softMinPosition() const;
  std::int32_t softMaxPosition() const;
  bool softEndstopsEnabled() const;
  std::uint16_t safePositionPermille() const;

  bool requestMoveTo(std::int32_t position, std::int32_t& acceptedPosition);
  bool setSoftEndstopsEnabled(bool enabled);
  bool setSoftEndstops(SoftEndstopRange range);
  bool setSafePositionPermille(std::uint16_t permille);
  std::int32_t moveTo(std::int32_t position);

 private:
  struct TextView {
    const char* data;
    std::size_t offset;
    std::size_t length;
  };

  void emit(EventId eventId, std::int32_t first = 0, std::int32_t second = 0,
            bool flag = false);
  void handleInitState();
  void handleHomingMinState(const DigitalInputs& inputs, std::uint32_t nowMs);
  void handleHomingMinSettlingState(std::uint32_t nowMs);
  void handleHomingMaxState(const DigitalInputs& inputs, std::uint32_t nowMs);
  void handleHomingMaxSettlingState(std::uint32_t nowMs);
  void handleReadyState(const DigitalInputs& inputs);
  void handleErrorDetectedState(std::uint32_t nowMs);
  void handleWaitResetState(std::uint32_t nowMs);
  void handleAutoRehomeState();
  void handleCommandText(const TextView& text);
  void startHomingMin();
  void startHomingMax();
  void resetMotor();
  void enterError();
  bool requireReady();
  bool homingMinTravelExceeded() const;
  bool homingMaxTravelExceeded() const;
  bool applyAddressFilter(TextView& text);
  void handleGotoCommand(const TextView& argument);
  void handleSoftMinCommand(const TextView& argument);
  void handleSoftMaxCommand(const TextView& argument);
  void handleSoftEndstopsCommand(const TextView& argument);
  void handleDeviceIdCommand(const TextView& argument);
  void handleSafePositionCommand(const TextView& argument);
  bool hasUnexpectedSwitch(const DigitalInputs& inputs) const;
  bool canMoveWithoutClamp(std::int32_t position) const;
  std::int32_t homingTravelSteps() const;
  std::int32_t positionFromPermille(std::uint16_t permille) const;
  void beginValveFreeCheck();
  void updateValveFreeCheck();

  static TextView trim(const char* text);
  static TextView trim(const TextView& text);
  static bool equals(const TextView& text, const char* expected);
  static bool readArgument(const TextView& text, const char* command,
                           TextView& argument);
  static bool parseInt32(const TextView& text, std::int32_t& value);
  static bool parseUint8(const TextView& text, std::uint8_t& value);
  static bool parseAddress(const TextView& text, std::uint8_t& value);
  static char charAt(const TextView& text, std::size_t index);
  static TextView subview(const TextView& text, std::size_t offset);
  static std::size_t stringLength(const char* text);
  static bool hasElapsed(std::uint32_t nowMs, std::uint32_t sinceMs,
                         std::uint32_t durationMs);
  static std::int32_t absoluteDistance(std::int32_t first,
                                       std::int32_t second);

  StepperPort& motor_;
  EventSink& events_;
  ControllerConfig config_;
  ControllerState state_;
  std::uint32_t stateTimestampMs_;
  std::uint32_t errorTimestampMs_;
  std::uint8_t deviceId_;
  std::int32_t maxPosition_;
  std::int32_t targetPosition_;
  std::int32_t softMinPosition_;
  std::int32_t softMaxPosition_;
  std::int32_t freeCheckStartPosition_;
  std::uint16_t safePositionPermille_;
  bool softEndstopsEnabled_;
  bool valveFreeCheckActive_;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_FAN_FLAP_CONTROLLER_H

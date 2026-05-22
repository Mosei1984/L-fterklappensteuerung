#include <unity.h>

#include <cstddef>
#include <cstdint>

#include <FanFlapController.h>
#include <ModbusRtuServer.h>
#include <PersistentSettings.h>
#include <Tmc2209Driver.h>

namespace {

using luefterklappe::ControllerConfig;
using luefterklappe::ControllerState;
using luefterklappe::DelayPort;
using luefterklappe::DigitalInputs;
using luefterklappe::Event;
using luefterklappe::EventId;
using luefterklappe::EventSink;
using luefterklappe::FanFlapController;
using luefterklappe::ModbusRegister;
using luefterklappe::ModbusRtuServer;
using luefterklappe::PersistentSettings;
using luefterklappe::PersistentSettingsStore;
using luefterklappe::SettingsStoragePort;
using luefterklappe::StepperPort;
using luefterklappe::Tmc2209Driver;
using luefterklappe::UartPort;

constexpr ControllerConfig kFastTestConfig{1000L, 5UL, 100UL, 400.0F, 200.0F,
                                           1000.0F, 500U, 20L};

constexpr DigitalInputs kNoInputs{false, false, false};

class FakeStepper final : public StepperPort {
 public:
  void setDriverEnabled(const bool enabled) override { enabled_ = enabled; }
  void setMaxSpeed(const float speed) override { maxSpeed_ = speed; }
  void setAcceleration(const float acceleration) override {
    acceleration_ = acceleration;
  }
  void moveTo(const std::int32_t position) override {
    target_ = position;
    if (position > position_) {
      speed_ = 1.0F;
    } else if (position < position_) {
      speed_ = -1.0F;
    } else {
      speed_ = 0.0F;
    }
  }
  void run() override { ++runCount_; }
  void stop() override {
    speed_ = 0.0F;
    ++stopCount_;
  }
  void setCurrentPosition(const std::int32_t position) override {
    position_ = position;
  }
  std::int32_t currentPosition() const override { return position_; }
  float speed() const override { return speed_; }

  bool enabled_{false};
  float maxSpeed_{0.0F};
  float acceleration_{0.0F};
  std::int32_t target_{0};
  std::int32_t position_{0};
  float speed_{0.0F};
  std::uint32_t runCount_{0U};
  std::uint32_t stopCount_{0U};
};

class CapturingEvents final : public EventSink {
 public:
  void onEvent(const Event& event) override {
    if (count_ < kCapacity) {
      events_[count_] = event;
      ++count_;
    }
  }

  void clear() { count_ = 0U; }

  bool contains(const EventId id) const {
    for (std::size_t index = 0U; index < count_; ++index) {
      if (events_[index].id == id) {
        return true;
      }
    }

    return false;
  }

  Event last() const {
    if (count_ == 0U) {
      return Event{EventId::UnknownCommand, 0, 0, false};
    }

    return events_[count_ - 1U];
  }

  static constexpr std::size_t kCapacity = 96U;
  Event events_[kCapacity]{};
  std::size_t count_{0U};
};

class FakeUart final : public UartPort {
 public:
  void writeByte(const std::uint8_t value) override {
    if (writtenCount_ < kCapacity) {
      written_[writtenCount_] = value;
      ++writtenCount_;
    }
  }

  void flush() override { ++flushCount_; }

  std::size_t available() const override { return readCount_ - readIndex_; }

  bool readByte(std::uint8_t& value) override {
    if (readIndex_ >= readCount_) {
      return false;
    }

    value = read_[readIndex_];
    ++readIndex_;
    return true;
  }

  void queueRead(const std::uint8_t value) {
    if (readCount_ < kCapacity) {
      read_[readCount_] = value;
      ++readCount_;
    }
  }

  void clearWritten() { writtenCount_ = 0U; }

  static constexpr std::size_t kCapacity = 64U;
  std::uint8_t written_[kCapacity]{};
  std::uint8_t read_[kCapacity]{};
  std::size_t writtenCount_{0U};
  std::size_t readCount_{0U};
  std::size_t readIndex_{0U};
  std::uint32_t flushCount_{0U};
};

class FakeDelay final : public DelayPort {
 public:
  void delayMilliseconds(const std::uint32_t milliseconds) override {
    lastDelayMs_ = milliseconds;
    ++callCount_;
  }

  std::uint32_t lastDelayMs_{0U};
  std::uint32_t callCount_{0U};
};

class FakeSettingsStorage final : public SettingsStoragePort {
 public:
  FakeSettingsStorage() {
    for (std::uint8_t& byte : bytes_) {
      byte = 0xFFU;
    }
  }

  bool read(std::uint8_t* const data, const std::size_t size) override {
    if ((!readOk_) || (size > kCapacity)) {
      return false;
    }

    for (std::size_t index = 0U; index < size; ++index) {
      data[index] = bytes_[index];
    }

    return true;
  }

  bool write(const std::uint8_t* const data, const std::size_t size) override {
    if ((!writeOk_) || (size > kCapacity)) {
      return false;
    }

    for (std::size_t index = 0U; index < size; ++index) {
      bytes_[index] = data[index];
    }
    ++writeCount_;
    return true;
  }

  void corruptByte(const std::size_t index) {
    if (index < kCapacity) {
      bytes_[index] = static_cast<std::uint8_t>(bytes_[index] ^ 0x5AU);
    }
  }

  static constexpr std::size_t kCapacity = 32U;
  std::uint8_t bytes_[kCapacity]{};
  bool readOk_{true};
  bool writeOk_{true};
  std::uint32_t writeCount_{0U};
};

std::uint8_t tmcCrc(const std::uint8_t* const data,
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

void queueTmcResponse(FakeUart& uart, const std::uint8_t registerAddress,
                      const std::uint32_t value,
                      const std::uint8_t sourceAddress = 0x00U) {
  std::uint8_t response[8]{
      0x05U,
      sourceAddress,
      registerAddress,
      static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(value & 0xFFU),
      0U};

  response[7] = tmcCrc(response, 7U);

  for (const std::uint8_t valueByte : response) {
    uart.queueRead(valueByte);
  }
}

void queueTmcReadEcho(FakeUart& uart, const std::uint8_t registerAddress) {
  std::uint8_t request[4]{0x05U, 0x00U, registerAddress, 0U};
  request[3] = tmcCrc(request, 3U);

  for (const std::uint8_t valueByte : request) {
    uart.queueRead(valueByte);
  }
}

void writeModbusUint16(std::uint8_t* const data, const std::size_t offset,
                       const std::uint16_t value) {
  data[offset] = static_cast<std::uint8_t>((value >> 8U) & 0x00FFU);
  data[offset + 1U] = static_cast<std::uint8_t>(value & 0x00FFU);
}

void appendModbusCrc(std::uint8_t* const frame,
                     const std::size_t lengthWithoutCrc) {
  const std::uint16_t crc = ModbusRtuServer::crc16(frame, lengthWithoutCrc);
  frame[lengthWithoutCrc] = static_cast<std::uint8_t>(crc & 0x00FFU);
  frame[lengthWithoutCrc + 1U] =
      static_cast<std::uint8_t>((crc >> 8U) & 0x00FFU);
}

void feedModbusFrame(ModbusRtuServer& server, std::uint8_t* const frame,
                     const std::size_t lengthWithoutCrc) {
  appendModbusCrc(frame, lengthWithoutCrc);

  for (std::size_t index = 0U; index < (lengthWithoutCrc + 2U); ++index) {
    server.handleByte(frame[index]);
  }
}

std::uint16_t responseUint16(const FakeUart& uart, const std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(uart.written_[offset]) << 8U) |
      uart.written_[offset + 1U]);
}

void bringControllerToReady(FanFlapController& controller, FakeStepper& stepper,
                            CapturingEvents& events) {
  controller.begin();
  controller.tick(kNoInputs, 0U);
  controller.tick(DigitalInputs{true, false, false}, 1U);
  controller.tick(kNoInputs, 6U);
  stepper.position_ = 800;
  controller.tick(DigitalInputs{false, true, false}, 7U);
  controller.tick(kNoInputs, 12U);
  events.clear();
}

void test_persistent_settings_load_defaults_from_blank_storage() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  const PersistentSettings settings = store.load();

  TEST_ASSERT_EQUAL_UINT8(1U, settings.deviceId);
  TEST_ASSERT_EQUAL_UINT16(1000U, settings.safePositionPermille);
}

void test_persistent_settings_save_and_reload_valid_settings() {
  FakeSettingsStorage storage;
  PersistentSettingsStore writer(storage);
  const PersistentSettings saved{42U, 250U};

  TEST_ASSERT_TRUE(writer.save(saved));
  TEST_ASSERT_EQUAL_UINT32(1U, storage.writeCount_);

  PersistentSettingsStore reader(storage);
  const PersistentSettings loaded = reader.load();

  TEST_ASSERT_EQUAL_UINT8(saved.deviceId, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(saved.safePositionPermille,
                           loaded.safePositionPermille);
}

void test_persistent_settings_reject_corrupt_or_out_of_range_data() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U}));
  storage.corruptByte(4U);

  PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(1U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(1000U, loaded.safePositionPermille);

  TEST_ASSERT_FALSE(store.save(PersistentSettings{248U, 1001U}));
  loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(1U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(1000U, loaded.safePositionPermille);
}

void test_homing_sequence_sets_range_and_midpoint() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  TEST_ASSERT_TRUE(stepper.enabled_);
  TEST_ASSERT_EQUAL_FLOAT(400.0F, stepper.maxSpeed_);
  TEST_ASSERT_EQUAL_FLOAT(1000.0F, stepper.acceleration_);
  TEST_ASSERT_TRUE(events.contains(EventId::SystemStarted));

  controller.tick(kNoInputs, 0U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());
  TEST_ASSERT_EQUAL_INT32(-1000, stepper.target_);
  TEST_ASSERT_EQUAL_FLOAT(200.0F, stepper.maxSpeed_);

  controller.tick(DigitalInputs{true, false, false}, 1U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMinSettling, controller.state());
  TEST_ASSERT_EQUAL_INT32(0, stepper.position_);
  TEST_ASSERT_TRUE(events.contains(EventId::MinPositionReached));

  controller.tick(kNoInputs, 5U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMinSettling, controller.state());

  controller.tick(kNoInputs, 6U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMax, controller.state());
  TEST_ASSERT_EQUAL_INT32(1000, stepper.target_);

  stepper.position_ = 800;
  controller.tick(DigitalInputs{false, true, false}, 7U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMaxSettling, controller.state());
  TEST_ASSERT_EQUAL_INT32(800, controller.maxPosition());
  TEST_ASSERT_EQUAL_INT32(0, controller.softMinPosition());
  TEST_ASSERT_EQUAL_INT32(800, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsSet));

  controller.tick(kNoInputs, 12U);
  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(400, stepper.target_);
}

void test_homing_moves_to_configured_safe_position() {
  FakeStepper stepper;
  CapturingEvents events;
  ControllerConfig config = kFastTestConfig;
  config.safePositionPermille = 250U;
  FanFlapController controller(stepper, events, config);

  bringControllerToReady(controller, stepper, events);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_INT32(200, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(200, stepper.target_);
}

void test_safe_position_commands_validate_and_report_permille() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  TEST_ASSERT_EQUAL_UINT16(500U, controller.safePositionPermille());

  controller.handleCommand("SAFE 250");
  TEST_ASSERT_EQUAL_UINT16(250U, controller.safePositionPermille());
  TEST_ASSERT_TRUE(events.contains(EventId::SafePositionChanged));

  controller.handleCommand("SAFE?");
  TEST_ASSERT_EQUAL(EventId::SafePositionReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(250, events.last().first);

  controller.handleCommand("SAFE 1001");
  TEST_ASSERT_EQUAL_UINT16(250U, controller.safePositionPermille());
  TEST_ASSERT_TRUE(events.contains(EventId::SafePositionInvalid));
}

void test_homing_uses_stallguard_as_min_endstop_redundancy() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  stepper.position_ = -300;
  controller.tick(DigitalInputs{false, false, true}, 1U);

  TEST_ASSERT_EQUAL(ControllerState::HomingMinSettling, controller.state());
  TEST_ASSERT_EQUAL_INT32(0, controller.currentPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SensorlessMinPositionReached));
}

void test_homing_uses_stallguard_as_max_endstop_redundancy() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  controller.tick(DigitalInputs{true, false, false}, 1U);
  controller.tick(kNoInputs, 6U);
  stepper.position_ = 800;
  controller.tick(DigitalInputs{false, false, true}, 7U);
  controller.tick(kNoInputs, 12U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_INT32(800, controller.maxPosition());
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SensorlessMaxPositionReached));
}

void test_invalid_homing_range_enters_fault_state() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  controller.tick(DigitalInputs{true, false, false}, 1U);
  controller.tick(kNoInputs, 6U);
  stepper.position_ = 0;
  controller.tick(DigitalInputs{false, true, false}, 7U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::HomingRangeInvalid));

  controller.tick(kNoInputs, 8U);
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_FALSE(stepper.enabled_);
  TEST_ASSERT_TRUE(events.contains(EventId::ErrorMotorStopped));
}

void test_homing_min_fails_when_switch_is_not_reached_within_travel() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  events.clear();

  stepper.position_ = -1000;
  controller.tick(kNoInputs, 1U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::HomingRangeInvalid));
  TEST_ASSERT_EQUAL_INT32(-1000, events.last().first);

  controller.tick(kNoInputs, 2U);
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_FALSE(stepper.enabled_);
}

void test_homing_max_fails_when_switch_is_not_reached_within_travel() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  controller.tick(DigitalInputs{true, false, false}, 1U);
  controller.tick(kNoInputs, 6U);
  events.clear();

  stepper.position_ = 1000;
  controller.tick(kNoInputs, 7U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::HomingRangeInvalid));
  TEST_ASSERT_EQUAL_INT32(1000, events.last().first);
}

void test_reset_timeout_is_exact_and_wraparound_safe() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  constexpr std::uint32_t kNearWrap = 0xFFFFFFF0UL;

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;

  controller.tick(DigitalInputs{true, false, false}, 20U);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());

  controller.tick(kNoInputs, kNearWrap);
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  events.clear();

  controller.tick(kNoInputs, static_cast<std::uint32_t>(kNearWrap + 99UL));
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::AutoRehomeTimeout));

  controller.tick(kNoInputs, static_cast<std::uint32_t>(kNearWrap + 100UL));
  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::AutoRehomeTimeout));
}

void test_serial_goto_clamps_edges_and_rejects_bad_numbers() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("  GOTO -25 \r");
  TEST_ASSERT_EQUAL_INT32(0, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(0, stepper.target_);
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopMinClamped));
  TEST_ASSERT_EQUAL(EventId::MoveAccepted, events.last().id);
  TEST_ASSERT_EQUAL_INT32(0, events.last().first);

  events.clear();
  controller.handleCommand("GOTO 1200");
  TEST_ASSERT_EQUAL_INT32(800, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(800, stepper.target_);
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopMaxClamped));
  TEST_ASSERT_EQUAL(EventId::MoveAccepted, events.last().id);
  TEST_ASSERT_EQUAL_INT32(800, events.last().first);

  events.clear();
  controller.handleCommand("GOTO 999999999999");
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidCommandArgument));
  TEST_ASSERT_EQUAL_INT32(800, controller.targetPosition());

  events.clear();
  controller.handleCommand("GOTO 500 extra");
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidCommandArgument));
  TEST_ASSERT_EQUAL_INT32(800, controller.targetPosition());
}

void test_soft_endstop_configuration_handles_clamps_and_invalid_ranges() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("SOFTMIN -10");
  TEST_ASSERT_EQUAL_INT32(0, controller.softMinPosition());
  TEST_ASSERT_EQUAL_INT32(800, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsSet));

  events.clear();
  controller.handleCommand("SOFTMIN 700");
  TEST_ASSERT_EQUAL_INT32(700, controller.softMinPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsSet));

  events.clear();
  controller.handleCommand("SOFTMAX 2000");
  TEST_ASSERT_EQUAL_INT32(800, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsSet));

  events.clear();
  controller.handleCommand("SOFTMAX 700");
  TEST_ASSERT_EQUAL_INT32(800, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopRangeInvalid));
}

void test_commands_are_safe_before_ready_and_status_is_always_available() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  events.clear();

  controller.handleCommand("GOTO 10");
  TEST_ASSERT_TRUE(events.contains(EventId::MotorNotReady));
  TEST_ASSERT_EQUAL_INT32(0, controller.targetPosition());

  events.clear();
  controller.handleCommand("SOFTMIN 1");
  TEST_ASSERT_TRUE(events.contains(EventId::MotorNotReady));

  events.clear();
  controller.handleCommand("POS?");
  TEST_ASSERT_EQUAL(EventId::PositionReported, events.last().id);

  events.clear();
  controller.handleCommand("RESET");
  TEST_ASSERT_TRUE(events.contains(EventId::ResetIgnored));

  events.clear();
  controller.handleCommand("SOFTENDSTOPS OFF");
  TEST_ASSERT_FALSE(controller.softEndstopsEnabled());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsDisabled));

  events.clear();
  controller.handleCommand("SOFTENDSTOPS?");
  TEST_ASSERT_EQUAL(EventId::SoftEndstopsStatus, events.last().id);
  TEST_ASSERT_FALSE(events.last().flag);
}

void test_ready_faults_on_unexpected_switch_and_stall() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());

  FakeStepper stalledStepper;
  CapturingEvents stalledEvents;
  FanFlapController stalledController(stalledStepper, stalledEvents,
                                      kFastTestConfig);
  bringControllerToReady(stalledController, stalledStepper, stalledEvents);

  stalledController.tick(DigitalInputs{false, false, true}, 20U);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, stalledController.state());
  TEST_ASSERT_TRUE(stalledEvents.contains(EventId::StallDetected));
}

void test_move_starts_and_passes_valve_free_check() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("GOTO 600");
  TEST_ASSERT_TRUE(events.contains(EventId::ValveFreeCheckStarted));

  events.clear();
  stepper.position_ = 780;
  controller.tick(kNoInputs, 20U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::ValveFreeCheckPassed));
}

void test_move_free_check_faults_when_valve_is_blocked() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("GOTO 600");
  events.clear();
  controller.tick(DigitalInputs{false, false, true}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::StallDetected));
}

void test_reset_command_in_wait_reset_starts_rehome() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  controller.tick(kNoInputs, 21U);
  events.clear();

  controller.handleCommand("RESET");
  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::ResetDuringWait));

  controller.tick(kNoInputs, 22U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());
  TEST_ASSERT_TRUE(stepper.enabled_);
}

void test_refresh_machine_command_rehomes_from_fault_without_mcu_reset() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  events.clear();

  controller.handleCommand("REFRESH");
  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::MachineRefreshStarted));
  TEST_ASSERT_FALSE(events.contains(EventId::SystemStarted));

  controller.tick(kNoInputs, 21U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());
  TEST_ASSERT_TRUE(stepper.enabled_);
}

void test_refresh_machine_command_rehomes_when_addressed() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  controller.handleCommand("ID 7");
  events.clear();

  controller.handleCommand("ID1 REFRESH");
  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);

  controller.handleCommand("ID7 REFRESH");
  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::MachineRefreshStarted));
}

void test_soft_endstops_can_be_disabled_for_service_moves() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("SOFTENDSTOPS OFF");
  events.clear();
  controller.handleCommand("GOTO -500");

  TEST_ASSERT_EQUAL_INT32(-500, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(-500, stepper.target_);
  TEST_ASSERT_FALSE(events.contains(EventId::SoftEndstopMinClamped));
  TEST_ASSERT_EQUAL(EventId::MoveAccepted, events.last().id);
  TEST_ASSERT_EQUAL_INT32(-500, events.last().first);
}

void test_device_id_can_be_reported_and_changed() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  TEST_ASSERT_EQUAL_UINT8(1U, controller.deviceId());
  events.clear();

  controller.handleCommand("ID?");
  TEST_ASSERT_EQUAL(EventId::DeviceIdReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(1, events.last().first);

  events.clear();
  controller.handleCommand("ID 7");
  TEST_ASSERT_EQUAL_UINT8(7U, controller.deviceId());
  TEST_ASSERT_TRUE(events.contains(EventId::DeviceIdChanged));
  TEST_ASSERT_EQUAL_INT32(7, events.last().first);

  events.clear();
  controller.handleCommand("SETID 247");
  TEST_ASSERT_EQUAL_UINT8(247U, controller.deviceId());
  TEST_ASSERT_EQUAL(EventId::DeviceIdChanged, events.last().id);
  TEST_ASSERT_EQUAL_INT32(247, events.last().first);
}

void test_device_id_rejects_invalid_values() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  events.clear();

  controller.handleCommand("ID 0");
  TEST_ASSERT_EQUAL_UINT8(1U, controller.deviceId());
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidDeviceId));

  events.clear();
  controller.handleCommand("SETID 248");
  TEST_ASSERT_EQUAL_UINT8(1U, controller.deviceId());
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidDeviceId));

  events.clear();
  controller.handleCommand("ID abc");
  TEST_ASSERT_EQUAL_UINT8(1U, controller.deviceId());
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidDeviceId));
}

void test_addressed_commands_only_execute_for_matching_id() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("ID2 GOTO 700");
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());

  controller.handleCommand("ID1 GOTO 700");
  TEST_ASSERT_EQUAL_INT32(700, controller.targetPosition());
  TEST_ASSERT_EQUAL(EventId::MoveAccepted, events.last().id);
  TEST_ASSERT_EQUAL_INT32(700, events.last().first);

  events.clear();
  controller.handleCommand("ID 7");
  TEST_ASSERT_EQUAL_UINT8(7U, controller.deviceId());
  events.clear();

  controller.handleCommand("ID1 GOTO 100");
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
  TEST_ASSERT_EQUAL_INT32(700, controller.targetPosition());

  controller.handleCommand("ID7 GOTO 100");
  TEST_ASSERT_EQUAL_INT32(100, controller.targetPosition());
  TEST_ASSERT_EQUAL(EventId::MoveAccepted, events.last().id);
}

void test_addressed_command_rejects_malformed_id_prefix() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("ID0 GOTO 200");
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidDeviceId));
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());

  events.clear();
  controller.handleCommand("ID248 GOTO 200");
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidDeviceId));
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());

  events.clear();
  controller.handleCommand("ID1GOTO 200");
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidDeviceId));
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
}

void test_modbus_read_ignores_wrong_address_and_bad_crc() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);

  std::uint8_t wrongAddressFrame[8]{2U, 0x03U, 0U, 8U, 0U, 2U, 0U, 0U};
  feedModbusFrame(server, wrongAddressFrame, 6U);
  TEST_ASSERT_EQUAL_UINT32(0U, uart.writtenCount_);

  std::uint8_t badCrcFrame[8]{1U, 0x03U, 0U, 8U, 0U, 2U, 0U, 0U};
  appendModbusCrc(badCrcFrame, 6U);
  badCrcFrame[6] = static_cast<std::uint8_t>(badCrcFrame[6] ^ 0x01U);
  for (const std::uint8_t byte : badCrcFrame) {
    server.handleByte(byte);
  }
  TEST_ASSERT_EQUAL_UINT32(0U, uart.writtenCount_);
}

void test_modbus_read_ignores_broadcast_and_rejects_invalid_ranges() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);

  std::uint8_t broadcastRead[8]{0U, 0x03U, 0U, 8U, 0U, 2U, 0U, 0U};
  feedModbusFrame(server, broadcastRead, 6U);
  TEST_ASSERT_EQUAL_UINT32(0U, uart.writtenCount_);

  std::uint8_t zeroQuantity[8]{1U, 0x03U, 0U, 8U, 0U, 0U, 0U, 0U};
  feedModbusFrame(server, zeroQuantity, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x83U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);

  uart.clearWritten();
  std::uint8_t tooManyRegisters[8]{1U, 0x03U, 0U, 0U, 0U, 18U, 0U, 0U};
  feedModbusFrame(server, tooManyRegisters, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x83U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);

  uart.clearWritten();
  std::uint8_t endOverflow[8]{1U, 0x03U, 0U, 16U, 0U, 2U, 0U, 0U};
  feedModbusFrame(server, endOverflow, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x83U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_modbus_reads_status_and_permille_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);

  std::uint8_t request[8]{1U, 0x03U, 0U, 8U, 0U, 8U, 0U, 0U};
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_UINT32(21U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(16U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT16(static_cast<std::uint16_t>(ControllerState::Ready),
                           responseUint16(uart, 3U));
  TEST_ASSERT_EQUAL_UINT16(0x0005U, responseUint16(uart, 5U));
  TEST_ASSERT_EQUAL_UINT16(1000U, responseUint16(uart, 17U));
}

void test_modbus_reads_complete_loxone_register_block() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);

  std::uint8_t request[8]{1U, 0x03U, 0U, 0U, 0U, 17U, 0U, 0U};
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_UINT32(39U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(34U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT16(1U, responseUint16(uart, 17U));
  TEST_ASSERT_EQUAL_UINT16(static_cast<std::uint16_t>(ControllerState::Ready),
                           responseUint16(uart, 19U));
  TEST_ASSERT_EQUAL_UINT16(controller.safePositionPermille(),
                           responseUint16(uart, 35U));
}

void test_modbus_write_target_permille_moves_when_ready() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  events.clear();

  std::uint8_t request[8]{1U, 0x06U, 0U, 0U, 0U, 500U >> 8U, 0U, 0U};
  writeModbusUint16(request, 2U,
                    static_cast<std::uint16_t>(ModbusRegister::TargetPermille));
  writeModbusUint16(request, 4U, 500U);
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(400, stepper.target_);
  TEST_ASSERT_TRUE(events.contains(EventId::ValveFreeCheckStarted) ||
                   events.contains(EventId::SoftEndstopMaxClamped));
  TEST_ASSERT_EQUAL_UINT32(8U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x06U, uart.written_[1]);
}

void test_modbus_broadcast_write_is_ignored_for_home_safety() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  events.clear();

  std::uint8_t request[8]{0U, 0x06U, 0U, 14U, 0U, 1000U >> 8U, 0U, 0U};
  writeModbusUint16(request, 4U, 1000U);
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(400, stepper.target_);
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
  TEST_ASSERT_EQUAL_UINT32(0U, uart.writtenCount_);
}

void test_modbus_safe_position_register_validates_and_updates() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t request[8]{1U, 0x06U, 0U, 0U, 0U, 250U, 0U, 0U};
  writeModbusUint16(
      request, 2U,
      static_cast<std::uint16_t>(ModbusRegister::SafePositionPermille));
  writeModbusUint16(request, 4U, 250U);
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_UINT16(250U, controller.safePositionPermille());
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x06U, uart.written_[1]);

  uart.clearWritten();
  writeModbusUint16(request, 4U, 1001U);
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_UINT16(250U, controller.safePositionPermille());
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
}

void test_modbus_rejects_invalid_register_and_device_id() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);

  std::uint8_t invalidRegister[8]{1U, 0x06U, 0U, 99U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, invalidRegister, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);

  uart.clearWritten();
  std::uint8_t invalidId[8]{1U, 0x06U, 0U, 7U, 0U, 248U, 0U, 0U};
  feedModbusFrame(server, invalidId, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, controller.deviceId());
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
}

void test_modbus_rejects_invalid_command_values() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  events.clear();

  std::uint8_t invalidCommand[8]{1U, 0x06U, 0U, 0U, 0U, 99U, 0U, 0U};
  feedModbusFrame(server, invalidCommand, 6U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
}

void test_modbus_refresh_machine_command_rehomes_from_fault() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  events.clear();

  std::uint8_t request[8]{1U, 0x06U, 0U, 0U, 0U, 5U, 0U, 0U};
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::MachineRefreshStarted));
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x06U, uart.written_[1]);
}

void test_modbus_write_multiple_registers_is_atomic_on_late_error() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  events.clear();

  std::uint8_t request[13]{1U, 0x10U, 0U, 14U, 0U, 2U, 4U,
                           0x03U, 0xE8U, 0U, 0U, 0U, 0U};
  feedModbusFrame(server, request, 11U);

  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(400, stepper.target_);
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x90U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_modbus_write_multiple_target_words_uses_final_32bit_value() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  events.clear();

  std::uint8_t request[13]{1U, 0x10U, 0U, 1U, 0U, 2U, 4U,
                           0U, 1U, 0U, 0U, 0U, 0U};
  feedModbusFrame(server, request, 11U);

  TEST_ASSERT_EQUAL_INT32(800, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(800, stepper.target_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x10U, uart.written_[1]);
}

void test_modbus_write_multiple_target_words_preserves_signed_32bit_value() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  TEST_ASSERT_TRUE(controller.setSoftEndstopsEnabled(false));
  events.clear();

  std::uint8_t request[13]{1U, 0x10U, 0U, 1U, 0U, 2U, 4U,
                           0xFFU, 0xFFU, 0xFFU, 0x9CU, 0U, 0U};
  feedModbusFrame(server, request, 11U);

  TEST_ASSERT_EQUAL_INT32(-100, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(-100, stepper.target_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x10U, uart.written_[1]);
}

void test_modbus_write_multiple_rejects_command_mixed_with_target() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  events.clear();

  std::uint8_t request[13]{1U, 0x10U, 0U, 0U, 0U, 2U, 4U,
                           0U, 1U, 0U, 0U, 0U, 0U};
  feedModbusFrame(server, request, 11U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x90U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
}

void test_tmc_driver_initializes_and_polls_stall_status() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  driver.initialize();

  TEST_ASSERT_TRUE(events.contains(EventId::TmcInitializationStarted));
  TEST_ASSERT_TRUE(events.contains(EventId::TmcConfigured));
  TEST_ASSERT_EQUAL_UINT32(1U, delay.callCount_);
  TEST_ASSERT_EQUAL_UINT32(10U, delay.lastDelayMs_);
  TEST_ASSERT_EQUAL_UINT32(2U, uart.flushCount_);
  TEST_ASSERT_EQUAL_UINT32(16U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(0x05U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x80U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[3]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[4]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[5]);
  TEST_ASSERT_EQUAL_UINT8(0x40U, uart.written_[6]);
  TEST_ASSERT_EQUAL_UINT8(tmcCrc(uart.written_, 7U), uart.written_[7]);
  TEST_ASSERT_EQUAL_UINT8(0x05U, uart.written_[8]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[9]);
  TEST_ASSERT_EQUAL_UINT8(0xC0U, uart.written_[10]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[11]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[12]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[13]);
  TEST_ASSERT_EQUAL_UINT8(100U, uart.written_[14]);
  TEST_ASSERT_EQUAL_UINT8(tmcCrc(&uart.written_[8], 7U), uart.written_[15]);

  uart.clearWritten();
  TEST_ASSERT_FALSE(driver.pollStallGuard());
  TEST_ASSERT_EQUAL_UINT32(4U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(0x05U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x41U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT8(tmcCrc(uart.written_, 3U), uart.written_[3]);

  queueTmcResponse(uart, 0x41U, 200U);
  TEST_ASSERT_FALSE(driver.pollStallGuard());

  queueTmcReadEcho(uart, 0x41U);
  queueTmcResponse(uart, 0x41U, 10U);
  TEST_ASSERT_TRUE(driver.pollStallGuard());
}

void test_tmc_driver_accepts_datasheet_read_reply_master_address() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  queueTmcReadEcho(uart, 0x41U);
  queueTmcResponse(uart, 0x41U, 10U, 0xFFU);

  TEST_ASSERT_TRUE(driver.pollStallGuard());
}

}  // namespace

void setUp() {}

void tearDown() {}

int main(int argc, char** argv) {
  static_cast<void>(argc);
  static_cast<void>(argv);

  UNITY_BEGIN();
  RUN_TEST(test_persistent_settings_load_defaults_from_blank_storage);
  RUN_TEST(test_persistent_settings_save_and_reload_valid_settings);
  RUN_TEST(test_persistent_settings_reject_corrupt_or_out_of_range_data);
  RUN_TEST(test_homing_sequence_sets_range_and_midpoint);
  RUN_TEST(test_homing_moves_to_configured_safe_position);
  RUN_TEST(test_safe_position_commands_validate_and_report_permille);
  RUN_TEST(test_homing_uses_stallguard_as_min_endstop_redundancy);
  RUN_TEST(test_homing_uses_stallguard_as_max_endstop_redundancy);
  RUN_TEST(test_invalid_homing_range_enters_fault_state);
  RUN_TEST(test_homing_min_fails_when_switch_is_not_reached_within_travel);
  RUN_TEST(test_homing_max_fails_when_switch_is_not_reached_within_travel);
  RUN_TEST(test_reset_timeout_is_exact_and_wraparound_safe);
  RUN_TEST(test_serial_goto_clamps_edges_and_rejects_bad_numbers);
  RUN_TEST(test_soft_endstop_configuration_handles_clamps_and_invalid_ranges);
  RUN_TEST(test_commands_are_safe_before_ready_and_status_is_always_available);
  RUN_TEST(test_ready_faults_on_unexpected_switch_and_stall);
  RUN_TEST(test_move_starts_and_passes_valve_free_check);
  RUN_TEST(test_move_free_check_faults_when_valve_is_blocked);
  RUN_TEST(test_reset_command_in_wait_reset_starts_rehome);
  RUN_TEST(test_refresh_machine_command_rehomes_from_fault_without_mcu_reset);
  RUN_TEST(test_refresh_machine_command_rehomes_when_addressed);
  RUN_TEST(test_soft_endstops_can_be_disabled_for_service_moves);
  RUN_TEST(test_device_id_can_be_reported_and_changed);
  RUN_TEST(test_device_id_rejects_invalid_values);
  RUN_TEST(test_addressed_commands_only_execute_for_matching_id);
  RUN_TEST(test_addressed_command_rejects_malformed_id_prefix);
  RUN_TEST(test_modbus_read_ignores_wrong_address_and_bad_crc);
  RUN_TEST(test_modbus_read_ignores_broadcast_and_rejects_invalid_ranges);
  RUN_TEST(test_modbus_reads_status_and_permille_registers);
  RUN_TEST(test_modbus_reads_complete_loxone_register_block);
  RUN_TEST(test_modbus_write_target_permille_moves_when_ready);
  RUN_TEST(test_modbus_broadcast_write_is_ignored_for_home_safety);
  RUN_TEST(test_modbus_safe_position_register_validates_and_updates);
  RUN_TEST(test_modbus_rejects_invalid_register_and_device_id);
  RUN_TEST(test_modbus_rejects_invalid_command_values);
  RUN_TEST(test_modbus_refresh_machine_command_rehomes_from_fault);
  RUN_TEST(test_modbus_write_multiple_registers_is_atomic_on_late_error);
  RUN_TEST(test_modbus_write_multiple_target_words_uses_final_32bit_value);
  RUN_TEST(test_modbus_write_multiple_target_words_preserves_signed_32bit_value);
  RUN_TEST(test_modbus_write_multiple_rejects_command_mixed_with_target);
  RUN_TEST(test_tmc_driver_initializes_and_polls_stall_status);
  RUN_TEST(test_tmc_driver_accepts_datasheet_read_reply_master_address);
  return UNITY_END();
}

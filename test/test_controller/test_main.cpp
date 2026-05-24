#include <unity.h>

#include <cstddef>
#include <cstdint>

#include <FanFlapController.h>
#include <InputDebouncer.h>
#include <ModbusRtuServer.h>
#include <MotorConfig.h>
#include <PersistentSettings.h>
#include <Tmc2209Driver.h>

#include "../../src/FirmwarePins.h"

namespace {

using luefterklappe::BootReason;
using luefterklappe::ControllerConfig;
using luefterklappe::ControllerState;
using luefterklappe::DelayPort;
using luefterklappe::DigitalInputs;
using luefterklappe::Event;
using luefterklappe::EventId;
using luefterklappe::EventSink;
using luefterklappe::FanFlapController;
using luefterklappe::FaultReason;
using luefterklappe::HomingConfig;
using luefterklappe::HomingDirection;
using luefterklappe::HomingSwitch;
using luefterklappe::InputDebouncer;
using luefterklappe::ModbusRegister;
using luefterklappe::MotorConfig;
using luefterklappe::ModbusRtuServer;
using luefterklappe::PersistentSettings;
using luefterklappe::PersistentSettingsStore;
using luefterklappe::SettingsStoragePort;
using luefterklappe::StepperPort;
using luefterklappe::Tmc2209Driver;
using luefterklappe::Tmc2209PollResult;
using luefterklappe::UartPort;
using luefterklappe::kDefaultHomingConfig;
using luefterklappe::kDefaultMotorConfig;

constexpr ControllerConfig kFastTestConfig{1000L, 5UL, 100UL, 400.0F, 200.0F,
                                           1000.0F, 500U, 20L, 2000UL,
                                           3000UL, 2L};

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
    if (stopClearsSpeed_) {
      speed_ = 0.0F;
    }
    ++stopCount_;
  }
  void setCurrentPosition(const std::int32_t position) override {
    position_ = position;
    target_ = position;
    speed_ = 0.0F;
  }
  std::int32_t currentPosition() const override { return position_; }
  float speed() const override { return speed_; }

  bool enabled_{false};
  float maxSpeed_{0.0F};
  float acceleration_{0.0F};
  std::int32_t target_{0};
  std::int32_t position_{0};
  float speed_{0.0F};
  bool stopClearsSpeed_{true};
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

  std::size_t count(const EventId id) const {
    std::size_t matches = 0U;
    for (std::size_t index = 0U; index < count_; ++index) {
      if (events_[index].id == id) {
        ++matches;
      }
    }

    return matches;
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

  void flush() override {
    ++flushCount_;
    for (std::size_t index = 0U; index < nextFlushReadCount_; ++index) {
      queueRead(nextFlushRead_[index]);
    }
    nextFlushReadCount_ = 0U;
  }

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

  void queueReadOnNextFlush(const std::uint8_t value) {
    if (nextFlushReadCount_ < kCapacity) {
      nextFlushRead_[nextFlushReadCount_] = value;
      ++nextFlushReadCount_;
    }
  }

  void clearWritten() { writtenCount_ = 0U; }

  static constexpr std::size_t kCapacity = 128U;
  std::uint8_t written_[kCapacity]{};
  std::uint8_t read_[kCapacity]{};
  std::uint8_t nextFlushRead_[kCapacity]{};
  std::size_t writtenCount_{0U};
  std::size_t readCount_{0U};
  std::size_t readIndex_{0U};
  std::size_t nextFlushReadCount_{0U};
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

  std::size_t slotCount() const override { return kSlotCount; }

  std::size_t slotSize() const override { return kSlotSize; }

  bool readSlot(const std::size_t slot, std::uint8_t* const data,
                const std::size_t size) override {
    if ((!readOk_) || (slot >= kSlotCount) || (size > kSlotSize)) {
      return false;
    }

    const std::size_t offset = slot * kSlotSize;
    for (std::size_t index = 0U; index < size; ++index) {
      data[index] = bytes_[offset + index];
    }

    return true;
  }

  bool writeSlot(const std::size_t slot, const std::uint8_t* const data,
                 const std::size_t size) override {
    if ((!writeOk_) || (slot >= kSlotCount) || (size > kSlotSize)) {
      return false;
    }

    if (failNextWriteSlot_ == slot) {
      failNextWriteSlot_ = kNoSlot;
      return false;
    }

    const std::size_t offset = slot * kSlotSize;
    for (std::size_t index = 0U; index < size; ++index) {
      bytes_[offset + index] = data[index];
    }
    ++writeCount_;
    lastWrittenSlot_ = slot;
    return true;
  }

  void corruptByte(const std::size_t index) {
    if (index < kCapacity) {
      bytes_[index] = static_cast<std::uint8_t>(bytes_[index] ^ 0x5AU);
    }
  }

  void failNextWriteForSlot(const std::size_t slot) {
    failNextWriteSlot_ = slot;
  }

  static constexpr std::size_t kSlotCount = 2U;
  static constexpr std::size_t kSlotSize =
      PersistentSettingsStore::storageSize();
  static constexpr std::size_t kCapacity = kSlotSize * kSlotCount;
  static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);
  std::uint8_t bytes_[kCapacity]{};
  bool readOk_{true};
  bool writeOk_{true};
  std::uint32_t writeCount_{0U};
  std::size_t lastWrittenSlot_{kNoSlot};
  std::size_t failNextWriteSlot_{kNoSlot};
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

void queueTmcResponseOnNextFlush(FakeUart& uart,
                                 const std::uint8_t registerAddress,
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
    uart.queueReadOnNextFlush(valueByte);
  }
}

void queueTmcReadEchoOnNextFlush(FakeUart& uart,
                                 const std::uint8_t registerAddress) {
  std::uint8_t request[4]{0x05U, 0x00U, registerAddress, 0U};
  request[3] = tmcCrc(request, 3U);

  for (const std::uint8_t valueByte : request) {
    uart.queueReadOnNextFlush(valueByte);
  }
}

void queueTmcWriteEcho(FakeUart& uart, const std::uint8_t registerAddress,
                       const std::uint32_t value) {
  std::uint8_t datagram[8]{
      0x05U,
      0x00U,
      static_cast<std::uint8_t>(registerAddress | 0x80U),
      static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(value & 0xFFU),
      0U};

  datagram[7] = tmcCrc(datagram, 7U);

  for (const std::uint8_t valueByte : datagram) {
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

void assertTmcWrite(const FakeUart& uart, const std::size_t offset,
                    const std::uint8_t registerAddress,
                    const std::uint32_t value) {
  TEST_ASSERT_EQUAL_UINT8(0x05U, uart.written_[offset]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[offset + 1U]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(registerAddress | 0x80U),
                          uart.written_[offset + 2U]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
                          uart.written_[offset + 3U]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
                          uart.written_[offset + 4U]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
                          uart.written_[offset + 5U]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(value & 0xFFU),
                          uart.written_[offset + 6U]);
  TEST_ASSERT_EQUAL_UINT8(tmcCrc(&uart.written_[offset], 7U),
                          uart.written_[offset + 7U]);
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

void test_pico_pin_mapping_matches_current_tmc_wiring() {
  TEST_ASSERT_EQUAL_UINT8(4U, luefterklappe::firmware::kStepPin);
  TEST_ASSERT_EQUAL_UINT8(2U, luefterklappe::firmware::kDirPin);
  TEST_ASSERT_EQUAL_UINT8(7U, luefterklappe::firmware::kEnablePin);
  TEST_ASSERT_EQUAL_UINT8(8U, luefterklappe::firmware::kTmcUartTxPin);
  TEST_ASSERT_EQUAL_UINT8(9U, luefterklappe::firmware::kTmcUartRxPin);
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
  const PersistentSettings saved{42U, 250U, 64U};

  TEST_ASSERT_TRUE(writer.save(saved));
  TEST_ASSERT_EQUAL_UINT32(1U, storage.writeCount_);

  PersistentSettingsStore reader(storage);
  const PersistentSettings loaded = reader.load();

  TEST_ASSERT_EQUAL_UINT8(saved.deviceId, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(saved.safePositionPermille,
                           loaded.safePositionPermille);
  TEST_ASSERT_EQUAL_UINT8(saved.stallGuardThreshold,
                          loaded.stallGuardThreshold);
}

void test_persistent_settings_save_and_reload_homing_config() {
  FakeSettingsStorage storage;
  PersistentSettingsStore writer(storage);
  const HomingConfig homing{HomingSwitch::MaxInput, HomingSwitch::MinInput,
                            HomingDirection::Positive,
                            HomingDirection::Negative, true};
  const PersistentSettings saved{42U, 250U, 64U, homing};

  TEST_ASSERT_TRUE(writer.save(saved));

  PersistentSettingsStore reader(storage);
  const PersistentSettings loaded = reader.load();

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MaxInput),
      static_cast<std::uint8_t>(loaded.homing.minSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MinInput),
      static_cast<std::uint8_t>(loaded.homing.maxSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Positive),
      static_cast<std::uint8_t>(loaded.homing.minDirection));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Negative),
      static_cast<std::uint8_t>(loaded.homing.maxDirection));
  TEST_ASSERT_TRUE(loaded.homing.stepperDirectionInverted);
}

void test_persistent_settings_save_and_reload_motor_config() {
  FakeSettingsStorage storage;
  PersistentSettingsStore writer(storage);
  const MotorConfig motor{900U, 150U, 850U};
  const PersistentSettings saved{42U, 250U, 64U, kDefaultHomingConfig, motor};

  TEST_ASSERT_TRUE(writer.save(saved));

  PersistentSettingsStore reader(storage);
  const PersistentSettings loaded = reader.load();

  TEST_ASSERT_EQUAL_UINT16(motor.normalMaxSpeedStepsPerSecond,
                           loaded.motor.normalMaxSpeedStepsPerSecond);
  TEST_ASSERT_EQUAL_UINT16(motor.homingMaxSpeedStepsPerSecond,
                           loaded.motor.homingMaxSpeedStepsPerSecond);
  TEST_ASSERT_EQUAL_UINT16(motor.runCurrentMilliamps,
                           loaded.motor.runCurrentMilliamps);
}

void test_persistent_settings_save_and_reload_auto_home_interval() {
  FakeSettingsStorage storage;
  PersistentSettingsStore writer(storage);
  const PersistentSettings saved{42U,
                                 250U,
                                 64U,
                                 kDefaultHomingConfig,
                                 kDefaultMotorConfig,
                                 1440U};

  TEST_ASSERT_TRUE(writer.save(saved));

  PersistentSettingsStore reader(storage);
  const PersistentSettings loaded = reader.load();

  TEST_ASSERT_EQUAL_UINT16(1440U, loaded.autoHomeIntervalMinutes);
}

void test_persistent_settings_reject_corrupt_or_out_of_range_data() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U}));
  storage.corruptByte(4U);

  PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(1U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(1000U, loaded.safePositionPermille);
  TEST_ASSERT_EQUAL_UINT8(100U, loaded.stallGuardThreshold);

  TEST_ASSERT_FALSE(store.save(PersistentSettings{248U, 1001U}));
  loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(1U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(1000U, loaded.safePositionPermille);
  TEST_ASSERT_EQUAL_UINT8(100U, loaded.stallGuardThreshold);

  const HomingConfig invalidSameSwitch{
      HomingSwitch::MinInput, HomingSwitch::MinInput,
      HomingDirection::Negative, HomingDirection::Positive, false};
  TEST_ASSERT_FALSE(
      store.save(PersistentSettings{7U, 750U, 90U, invalidSameSwitch}));

  TEST_ASSERT_FALSE(store.save(PersistentSettings{
      7U, 750U, 90U, kDefaultHomingConfig, MotorConfig{0U, 200U, 800U}}));
  TEST_ASSERT_FALSE(store.save(PersistentSettings{
      7U, 750U, 90U, kDefaultHomingConfig, MotorConfig{400U, 200U, 1200U}}));
  TEST_ASSERT_FALSE(store.save(PersistentSettings{
      7U, 750U, 90U, kDefaultHomingConfig, kDefaultMotorConfig, 10081U}));
}

void test_persistent_settings_loads_newest_valid_record() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U, 90U}));
  TEST_ASSERT_TRUE(store.save(PersistentSettings{8U, 250U, 55U}));

  const PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(8U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(250U, loaded.safePositionPermille);
  TEST_ASSERT_EQUAL_UINT8(55U, loaded.stallGuardThreshold);
}

void test_persistent_settings_falls_back_to_previous_slot_after_corruption() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U, 88U}));
  TEST_ASSERT_TRUE(store.save(PersistentSettings{8U, 250U, 55U}));
  storage.corruptByte(FakeSettingsStorage::kSlotSize);

  const PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(7U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(750U, loaded.safePositionPermille);
  TEST_ASSERT_EQUAL_UINT8(88U, loaded.stallGuardThreshold);
}

void test_persistent_settings_survives_target_slot_write_failure() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U, 88U}));
  storage.failNextWriteForSlot(1U);
  TEST_ASSERT_FALSE(store.save(PersistentSettings{8U, 250U, 55U}));

  const PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(7U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(750U, loaded.safePositionPermille);
  TEST_ASSERT_EQUAL_UINT8(88U, loaded.stallGuardThreshold);
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

void test_homing_config_defaults_match_existing_wiring() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  const HomingConfig homing = controller.homingConfig();

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MinInput),
      static_cast<std::uint8_t>(homing.minSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MaxInput),
      static_cast<std::uint8_t>(homing.maxSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Negative),
      static_cast<std::uint8_t>(homing.minDirection));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Positive),
      static_cast<std::uint8_t>(homing.maxDirection));
  TEST_ASSERT_FALSE(homing.stepperDirectionInverted);
}

void test_homing_config_maps_logical_min_and_max_to_selected_switches() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  const HomingConfig homing{HomingSwitch::MaxInput, HomingSwitch::MinInput,
                            HomingDirection::Positive,
                            HomingDirection::Negative, false};

  TEST_ASSERT_TRUE(controller.setHomingConfig(homing));

  controller.begin();
  controller.tick(kNoInputs, 0U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());
  TEST_ASSERT_EQUAL_INT32(1000, stepper.target_);

  controller.tick(DigitalInputs{false, true, false}, 1U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMinSettling, controller.state());
  TEST_ASSERT_EQUAL_INT32(0, controller.currentPosition());

  controller.tick(kNoInputs, 6U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMax, controller.state());
  TEST_ASSERT_EQUAL_INT32(-1000, stepper.target_);

  stepper.position_ = -750;
  controller.tick(DigitalInputs{true, false, false}, 7U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMaxSettling, controller.state());
  TEST_ASSERT_EQUAL_INT32(750, controller.maxPosition());
  TEST_ASSERT_EQUAL_INT32(750, controller.currentPosition());

  controller.tick(kNoInputs, 12U);
  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_EQUAL_INT32(375, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(-375, stepper.target_);
}

void test_homing_config_inverts_stepper_direction_without_changing_logic() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  const HomingConfig homing{HomingSwitch::MinInput, HomingSwitch::MaxInput,
                            HomingDirection::Negative,
                            HomingDirection::Positive, true};

  TEST_ASSERT_TRUE(controller.setHomingConfig(homing));

  controller.begin();
  controller.tick(kNoInputs, 0U);
  TEST_ASSERT_EQUAL_INT32(1000, stepper.target_);

  controller.tick(DigitalInputs{true, false, false}, 1U);
  controller.tick(kNoInputs, 6U);
  TEST_ASSERT_EQUAL_INT32(-1000, stepper.target_);

  stepper.position_ = -800;
  controller.tick(DigitalInputs{false, true, false}, 7U);
  controller.tick(kNoInputs, 12U);
  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());

  controller.handleCommand("GOTO 400");
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(-400, stepper.target_);
}

void test_input_debouncer_requires_stable_switch_state() {
  InputDebouncer debouncer(5U);

  DigitalInputs stable = debouncer.tick(kNoInputs, 0U);
  TEST_ASSERT_FALSE(stable.minSwitchActive);
  TEST_ASSERT_FALSE(stable.maxSwitchActive);
  TEST_ASSERT_FALSE(stable.stallGuardActive);

  stable = debouncer.tick(DigitalInputs{true, false, false}, 1U);
  TEST_ASSERT_FALSE(stable.minSwitchActive);

  stable = debouncer.tick(DigitalInputs{true, false, false}, 6U);
  TEST_ASSERT_TRUE(stable.minSwitchActive);
}

void test_both_endstops_active_at_boot_enters_fault() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(DigitalInputs{true, true, false}, 0U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::BothEndstopsActiveAtBoot),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
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

void test_degree_position_commands_move_and_report_angle() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = 800;

  controller.handleCommand("DEG?");
  TEST_ASSERT_EQUAL(EventId::DegreePositionReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(0, events.last().first);

  events.clear();
  controller.handleCommand("GOTO_DEG 45");
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_INT32(400, stepper.target_);
  TEST_ASSERT_EQUAL(EventId::MoveAccepted, events.last().id);

  events.clear();
  stepper.position_ = 400;
  controller.tick(kNoInputs, 20U);
  TEST_ASSERT_EQUAL(EventId::MoveReached, events.last().id);
  TEST_ASSERT_EQUAL_INT32(400, events.last().first);
  TEST_ASSERT_EQUAL_INT32(500, events.last().second);
  TEST_ASSERT_EQUAL_INT32(45, events.last().third);

  events.clear();
  controller.handleCommand("GOTO_DEG 91");
  TEST_ASSERT_TRUE(events.contains(EventId::InvalidCommandArgument));
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
}

void test_degree_soft_endstop_commands_configure_allowed_angle_range() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("SOFTMIN_DEG 10");
  TEST_ASSERT_EQUAL_INT32(711, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsSet));

  events.clear();
  controller.handleCommand("SOFTMAX_DEG 80");
  TEST_ASSERT_EQUAL_INT32(88, controller.softMinPosition());
  TEST_ASSERT_EQUAL_INT32(711, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopsSet));

  events.clear();
  controller.handleCommand("DEGLIMITS?");
  TEST_ASSERT_EQUAL(EventId::DegreeLimitsReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(10, events.last().first);
  TEST_ASSERT_EQUAL_INT32(80, events.last().second);

  events.clear();
  controller.handleCommand("GOTO_DEG 0");
  TEST_ASSERT_EQUAL_INT32(711, controller.targetPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopMaxClamped));

  events.clear();
  controller.handleCommand("SOFTMIN_DEG 85");
  TEST_ASSERT_EQUAL_INT32(88, controller.softMinPosition());
  TEST_ASSERT_EQUAL_INT32(711, controller.softMaxPosition());
  TEST_ASSERT_TRUE(events.contains(EventId::SoftEndstopRangeInvalid));
}

void test_stallguard_threshold_command_validates_and_reports_setting() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  TEST_ASSERT_EQUAL_UINT8(100U, controller.stallGuardThreshold());

  controller.handleCommand("STALLGUARD 64");
  TEST_ASSERT_EQUAL_UINT8(64U, controller.stallGuardThreshold());
  TEST_ASSERT_TRUE(events.contains(EventId::StallGuardThresholdChanged));

  events.clear();
  controller.handleCommand("STALLGUARD?");
  TEST_ASSERT_EQUAL(EventId::StallGuardThresholdReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(64, events.last().first);

  events.clear();
  controller.handleCommand("STALLGUARD 256");
  TEST_ASSERT_EQUAL_UINT8(64U, controller.stallGuardThreshold());
  TEST_ASSERT_TRUE(events.contains(EventId::StallGuardThresholdInvalid));
}

void test_setup_set_commands_acknowledge_unchanged_values() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.handleCommand("ID 1");
  TEST_ASSERT_EQUAL(EventId::DeviceIdReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(1, events.last().first);

  events.clear();
  controller.handleCommand("SAFE 500");
  TEST_ASSERT_EQUAL(EventId::SafePositionReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(500, events.last().first);

  events.clear();
  controller.handleCommand("STALLGUARD 100");
  TEST_ASSERT_EQUAL(EventId::StallGuardThresholdReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(100, events.last().first);
}

void test_homing_config_command_validates_reports_and_updates() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.handleCommand("HOMECFG 1 0 1 0 1");

  const HomingConfig homing = controller.homingConfig();
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MaxInput),
      static_cast<std::uint8_t>(homing.minSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MinInput),
      static_cast<std::uint8_t>(homing.maxSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Positive),
      static_cast<std::uint8_t>(homing.minDirection));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Negative),
      static_cast<std::uint8_t>(homing.maxDirection));
  TEST_ASSERT_TRUE(homing.stepperDirectionInverted);
  TEST_ASSERT_TRUE(events.contains(EventId::HomingConfigChanged));

  events.clear();
  controller.handleCommand("HOMECFG?");
  TEST_ASSERT_EQUAL(EventId::HomingConfigReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(1, events.last().first);
  TEST_ASSERT_EQUAL_INT32(0, events.last().second);
  TEST_ASSERT_EQUAL_INT32(1, events.last().third);
  TEST_ASSERT_EQUAL_INT32(0, events.last().fourth);
  TEST_ASSERT_EQUAL_INT32(1, events.last().fifth);

  events.clear();
  controller.handleCommand("HOMECFG 0 0 0 1 0");
  TEST_ASSERT_TRUE(events.contains(EventId::HomingConfigInvalid));
  TEST_ASSERT_TRUE(controller.homingConfig().stepperDirectionInverted);
}

void test_homing_config_command_acknowledges_unchanged_config_during_homing() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  controller.begin();
  controller.tick(kNoInputs, 0U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());

  events.clear();
  controller.handleCommand("HOMECFG 0 1 0 1 0");

  TEST_ASSERT_FALSE(events.contains(EventId::HomingConfigInvalid));
  TEST_ASSERT_EQUAL(EventId::HomingConfigReported, events.last().id);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());
}

void test_homing_config_command_can_restart_homing_during_setup() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  controller.begin();
  controller.tick(kNoInputs, 0U);
  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());

  events.clear();
  controller.handleCommand("HOMECFG 1 0 1 0 1");

  TEST_ASSERT_FALSE(events.contains(EventId::HomingConfigInvalid));
  TEST_ASSERT_TRUE(events.contains(EventId::HomingConfigChanged));
  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
}

void test_motor_config_command_validates_reports_and_updates() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();

  controller.handleCommand("MOTORCFG?");
  TEST_ASSERT_TRUE(events.contains(EventId::MotorConfigReported));
  TEST_ASSERT_EQUAL_INT32(400, events.last().first);
  TEST_ASSERT_EQUAL_INT32(200, events.last().second);
  TEST_ASSERT_EQUAL_INT32(1000, events.last().third);

  events.clear();
  controller.handleCommand("MOTORCFG 900 150 850");
  TEST_ASSERT_TRUE(events.contains(EventId::MotorConfigChanged));
  TEST_ASSERT_EQUAL_UINT16(900U,
                           controller.motorConfig()
                               .normalMaxSpeedStepsPerSecond);
  TEST_ASSERT_EQUAL_UINT16(150U,
                           controller.motorConfig()
                               .homingMaxSpeedStepsPerSecond);
  TEST_ASSERT_EQUAL_UINT16(850U, controller.motorConfig().runCurrentMilliamps);
  TEST_ASSERT_EQUAL_FLOAT(900.0F, stepper.maxSpeed_);

  events.clear();
  controller.handleCommand("MOTORCFG 900 150 850");
  TEST_ASSERT_EQUAL(EventId::MotorConfigReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(900, events.last().first);
  TEST_ASSERT_EQUAL_INT32(150, events.last().second);
  TEST_ASSERT_EQUAL_INT32(850, events.last().third);

  events.clear();
  controller.handleCommand("MOTORCFG 0 150 850");
  TEST_ASSERT_TRUE(events.contains(EventId::MotorConfigInvalid));
  TEST_ASSERT_EQUAL_UINT16(900U,
                           controller.motorConfig()
                               .normalMaxSpeedStepsPerSecond);
}

void test_auto_home_interval_command_validates_reports_and_updates() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  TEST_ASSERT_EQUAL_UINT16(0U, controller.autoHomeIntervalMinutes());

  controller.handleCommand("AUTOHOME 1440");
  TEST_ASSERT_TRUE(events.contains(EventId::AutoHomeIntervalChanged));
  TEST_ASSERT_EQUAL_UINT16(1440U, controller.autoHomeIntervalMinutes());

  events.clear();
  controller.handleCommand("AUTOHOME?");
  TEST_ASSERT_EQUAL(EventId::AutoHomeIntervalReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(1440, events.last().first);

  events.clear();
  controller.handleCommand("AUTOHOME 10081");
  TEST_ASSERT_TRUE(events.contains(EventId::AutoHomeIntervalInvalid));
  TEST_ASSERT_EQUAL_UINT16(1440U, controller.autoHomeIntervalMinutes());
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

void test_homing_ignores_stallguard_until_minimum_travel_is_reached() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  stepper.position_ = -1;
  controller.tick(DigitalInputs{false, false, true}, 1U);

  TEST_ASSERT_EQUAL(ControllerState::HomingMin, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::SensorlessMinPositionReached));
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

void test_homing_max_ignores_stallguard_until_minimum_travel_is_reached() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  controller.tick(DigitalInputs{true, false, false}, 1U);
  controller.tick(kNoInputs, 6U);
  stepper.position_ = 1;
  controller.tick(DigitalInputs{false, false, true}, 7U);

  TEST_ASSERT_EQUAL(ControllerState::HomingMax, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::SensorlessMaxPositionReached));
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

void test_fault_reason_records_homing_min_timeout() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  stepper.position_ = -1000;
  controller.tick(kNoInputs, 1U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<std::uint16_t>(FaultReason::HomingMinTravelExceeded),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
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

void test_fault_reason_records_unexpected_switch_direction() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<std::uint16_t>(FaultReason::UnexpectedMinSwitch),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
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

void test_ready_move_reenables_driver_after_direct_service_tests() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.enabled_ = false;

  controller.handleCommand("GOTO 300");

  TEST_ASSERT_TRUE(stepper.enabled_);
  TEST_ASSERT_EQUAL_INT32(300, stepper.target_);
  TEST_ASSERT_TRUE(events.contains(EventId::MoveAccepted));
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
  ControllerConfig stalledConfig = kFastTestConfig;
  stalledConfig.freeCheckSteps = 0;
  FanFlapController stalledController(stalledStepper, stalledEvents,
                                      stalledConfig);
  bringControllerToReady(stalledController, stalledStepper, stalledEvents);
  stalledStepper.position_ = 500;
  stalledEvents.clear();

  stalledController.tick(DigitalInputs{false, false, true}, 21U);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, stalledController.state());
  TEST_ASSERT_TRUE(stalledEvents.contains(EventId::StallDetected));
}

void test_ready_ignores_stale_stallguard_when_motor_is_stopped() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = controller.targetPosition();
  stepper.speed_ = 0.0F;
  events.clear();

  controller.tick(DigitalInputs{false, false, true}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::StallDetected));
  TEST_ASSERT_EQUAL(static_cast<std::uint16_t>(FaultReason::None),
                    static_cast<std::uint16_t>(controller.lastFaultReason()));
}

void test_ready_ignores_stallguard_until_move_activation_travel_is_reached() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = 778;
  stepper.speed_ = -1.0F;
  events.clear();

  controller.tick(DigitalInputs{false, false, true}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::ValveFreeCheckPassed));
  TEST_ASSERT_FALSE(events.contains(EventId::StallDetected));
  TEST_ASSERT_EQUAL(static_cast<std::uint16_t>(FaultReason::None),
                    static_cast<std::uint16_t>(controller.lastFaultReason()));
}

void test_fault_stops_and_disables_driver_in_same_tick() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  const std::uint32_t stopCountBeforeFault = stepper.stopCount_;

  controller.tick(DigitalInputs{true, false, false}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_FALSE(stepper.enabled_);
  TEST_ASSERT_EQUAL_FLOAT(0.0F, stepper.speed_);
  TEST_ASSERT_EQUAL_UINT32(stopCountBeforeFault + 1U, stepper.stopCount_);
}

void test_repeated_external_fault_does_not_flood_fault_counter() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;

  controller.reportExternalFault(FaultReason::TmcCommunicationLost);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::TmcCommunicationLost),
      static_cast<std::uint16_t>(controller.lastFaultReason()));

  controller.reportExternalFault(FaultReason::UnexpectedMinSwitch);
  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::TmcCommunicationLost),
      static_cast<std::uint16_t>(controller.lastFaultReason()));

  controller.tick(kNoInputs, 21U);
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  controller.reportExternalFault(FaultReason::UnexpectedMaxSwitch);

  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::TmcCommunicationLost),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
}

void test_fault_latches_zero_speed_when_driver_stop_coasts() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.stopClearsSpeed_ = false;
  stepper.speed_ = 1.0F;
  stepper.position_ = 420;

  controller.reportExternalFault(FaultReason::TmcCommunicationLost);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL_FLOAT(0.0F, stepper.speed_);
  TEST_ASSERT_EQUAL_INT32(420, stepper.currentPosition());
  TEST_ASSERT_EQUAL_INT32(420, stepper.target_);
}

void test_motor_test_runs_bounded_steps_and_refreshes_wait_timer() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  controller.tick(kNoInputs, 21U);
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());

  events.clear();
  controller.handleCommand("MOTORTEST 12");

  TEST_ASSERT_EQUAL(ControllerState::ServiceMotorTest, controller.state());
  TEST_ASSERT_TRUE(stepper.enabled_);
  TEST_ASSERT_EQUAL_FLOAT(kFastTestConfig.homingMaxSpeed, stepper.maxSpeed_);
  TEST_ASSERT_EQUAL_INT32(12, stepper.target_);
  TEST_ASSERT_EQUAL(EventId::MotorTestStarted, events.last().id);
  TEST_ASSERT_EQUAL_INT32(12, events.last().first);

  events.clear();
  stepper.position_ = 12;
  controller.tick(DigitalInputs{false, false, true}, 200U);

  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_FALSE(stepper.enabled_);
  TEST_ASSERT_EQUAL_FLOAT(0.0F, stepper.speed_);
  TEST_ASSERT_EQUAL(EventId::MotorTestFinished, events.last().id);
  TEST_ASSERT_EQUAL_INT32(12, events.last().first);

  events.clear();
  controller.tick(kNoInputs, 201U);

  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::AutoRehomeTimeout));
}

void test_motor_test_rejects_unsafe_state_and_invalid_step_counts() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("MOTORTEST 12");
  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::MotorNotReady));

  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  controller.tick(kNoInputs, 21U);
  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());

  const char* const invalidCommands[] = {"MOTORTEST 0", "MOTORTEST 3201",
                                         "MOTORTEST -3201", "MOTORTEST abc"};

  for (const char* const command : invalidCommands) {
    events.clear();
    controller.handleCommand(command);
    TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
    TEST_ASSERT_TRUE(events.contains(EventId::InvalidCommandArgument));
  }
}

void test_motor_test_ignores_external_faults_until_bounded_motion_finishes() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);
  controller.tick(kNoInputs, 21U);
  const std::uint16_t faultCountBeforeTest = controller.faultCount();

  events.clear();
  controller.handleCommand("MOTORTEST -8");
  TEST_ASSERT_EQUAL(ControllerState::ServiceMotorTest, controller.state());

  controller.reportExternalFault(FaultReason::TmcCommunicationLost);
  TEST_ASSERT_EQUAL(ControllerState::ServiceMotorTest, controller.state());
  TEST_ASSERT_EQUAL_UINT16(faultCountBeforeTest, controller.faultCount());

  stepper.position_ = -8;
  controller.tick(kNoInputs, 40U);

  TEST_ASSERT_EQUAL(ControllerState::WaitReset, controller.state());
  TEST_ASSERT_EQUAL(EventId::MotorTestFinished, events.last().id);
  TEST_ASSERT_EQUAL_INT32(-8, events.last().first);
}

void test_home_command_after_fault_does_not_bypass_disabled_driver() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;

  controller.tick(DigitalInputs{true, false, false}, 20U);
  controller.handleCommand("HOME");

  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_FALSE(stepper.enabled_);
  TEST_ASSERT_EQUAL_FLOAT(0.0F, stepper.speed_);
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

void test_move_reached_event_is_emitted_after_target_position_is_reached() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = 400;
  controller.tick(kNoInputs, 20U);
  events.clear();

  controller.handleCommand("GOTO 600");

  TEST_ASSERT_TRUE(events.contains(EventId::MoveAccepted));
  TEST_ASSERT_FALSE(events.contains(EventId::MoveReached));

  events.clear();
  stepper.position_ = 600;
  controller.tick(kNoInputs, 21U);

  TEST_ASSERT_EQUAL_UINT32(1U, events.count(EventId::MoveReached));
  TEST_ASSERT_EQUAL(EventId::MoveReached, events.last().id);
  TEST_ASSERT_EQUAL_INT32(600, events.last().first);
  TEST_ASSERT_EQUAL_INT32(750, events.last().second);
  TEST_ASSERT_FALSE(stepper.enabled_);

  controller.tick(kNoInputs, 22U);

  TEST_ASSERT_EQUAL_UINT32(1U, events.count(EventId::MoveReached));
}

void test_new_move_reenables_driver_after_position_hold_disable() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = 400;
  controller.tick(kNoInputs, 20U);
  controller.handleCommand("GOTO 600");
  stepper.position_ = 600;
  controller.tick(kNoInputs, 21U);
  TEST_ASSERT_FALSE(stepper.enabled_);

  events.clear();
  controller.handleCommand("GOTO 300");

  TEST_ASSERT_TRUE(stepper.enabled_);
  TEST_ASSERT_TRUE(events.contains(EventId::MoveAccepted));
  TEST_ASSERT_EQUAL_INT32(300, controller.targetPosition());
}

void test_auto_home_interval_references_after_idle_interval() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = controller.targetPosition();
  controller.tick(kNoInputs, 20U);
  events.clear();
  TEST_ASSERT_TRUE(controller.setAutoHomeIntervalMinutes(1U));
  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());

  controller.tick(kNoInputs, 60012U);

  TEST_ASSERT_EQUAL(ControllerState::AutoRehome, controller.state());
  TEST_ASSERT_TRUE(events.contains(EventId::AutoHomeIntervalElapsed));
}

void test_auto_home_interval_waits_until_move_is_finished() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  TEST_ASSERT_TRUE(controller.setAutoHomeIntervalMinutes(1U));
  stepper.position_ = 400;
  controller.handleCommand("GOTO 600");
  events.clear();

  controller.tick(kNoInputs, 60011U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::AutoHomeIntervalElapsed));
  TEST_ASSERT_EQUAL_INT32(600, controller.targetPosition());
}

void test_move_free_check_faults_when_valve_is_blocked() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);

  controller.handleCommand("GOTO 600");
  events.clear();
  controller.tick(DigitalInputs{false, false, true}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::Ready, controller.state());
  TEST_ASSERT_FALSE(events.contains(EventId::StallDetected));

  controller.tick(DigitalInputs{false, false, true}, 2021U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::ValveBlockedDuringFreeCheck),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
}

void test_ready_move_faults_when_position_does_not_progress() {
  FakeStepper stepper;
  CapturingEvents events;
  ControllerConfig config = kFastTestConfig;
  config.motionNoProgressTimeoutMs = 25U;
  config.motionProgressMinSteps = 2L;
  FanFlapController controller(stepper, events, config);

  bringControllerToReady(controller, stepper, events);
  std::int32_t accepted = 0;
  TEST_ASSERT_TRUE(controller.requestMoveTo(700, accepted));
  stepper.speed_ = 1.0F;
  stepper.position_ = 400;

  controller.tick(kNoInputs, 20U);
  controller.tick(kNoInputs, 46U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::MotionNoProgress),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
}

void test_free_check_times_out_before_required_travel() {
  FakeStepper stepper;
  CapturingEvents events;
  ControllerConfig config = kFastTestConfig;
  config.freeCheckTimeoutMs = 15U;
  FanFlapController controller(stepper, events, config);

  bringControllerToReady(controller, stepper, events);
  static_cast<void>(controller.moveTo(700));
  stepper.speed_ = 1.0F;

  controller.tick(kNoInputs, 20U);
  controller.tick(kNoInputs, 36U);

  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::ValveBlockedDuringFreeCheck),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
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
  std::uint8_t tooManyRegisters[8]{1U, 0x03U, 0U, 0U, 0U, 38U, 0U, 0U};
  feedModbusFrame(server, tooManyRegisters, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x83U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);

  uart.clearWritten();
  std::uint8_t endOverflow[8]{1U, 0x03U, 0U, 36U, 0U, 2U, 0U, 0U};
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
  TEST_ASSERT_EQUAL_UINT16(0x000DU, responseUint16(uart, 5U));
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

void test_modbus_reads_release_diagnostic_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 17U, 0U, 6U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(12U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<std::uint16_t>(FaultReason::UnexpectedMinSwitch),
      responseUint16(uart, 3U));
  TEST_ASSERT_EQUAL_UINT16(1U, responseUint16(uart, 5U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 7U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 9U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 11U));
  TEST_ASSERT_EQUAL_UINT16(6U, responseUint16(uart, 13U));
}

void test_modbus_rejects_writes_to_release_diagnostic_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[8]{1U, 0x06U, 0U, 17U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_modbus_rejects_multiple_writes_to_release_diagnostic_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[11]{1U, 0x10U, 0U, 17U, 0U, 1U,
                         2U, 0U,    1U, 0U, 0U};
  feedModbusFrame(server, frame, 9U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x90U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_modbus_reads_full_release_register_block() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 0U, 0U, 37U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(74U, uart.written_[2]);
}

void test_modbus_reads_configured_boot_reason_register() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);
  server.setBootReason(BootReason::Watchdog);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 21U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(2U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT16(static_cast<std::uint16_t>(BootReason::Watchdog),
                           responseUint16(uart, 3U));
}

void test_modbus_rejects_read_past_release_register_block() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 36U, 0U, 2U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x83U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_fault_and_diag_text_commands_report_fault_snapshot() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);

  events.clear();
  controller.handleCommand("FAULT?");
  TEST_ASSERT_EQUAL(EventId::FaultReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(
      static_cast<std::int32_t>(FaultReason::UnexpectedMinSwitch),
      events.last().first);
  TEST_ASSERT_EQUAL_INT32(1, events.last().second);

  events.clear();
  controller.handleCommand("DIAG?");
  TEST_ASSERT_EQUAL(EventId::DiagnosticsReported, events.last().id);
  TEST_ASSERT_EQUAL_INT32(
      static_cast<std::int32_t>(FaultReason::UnexpectedMinSwitch),
      events.last().first);
  TEST_ASSERT_EQUAL_INT32(1, events.last().second);
}

void test_service_selftest_command_reports_without_moving_motor() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.handleCommand("SELFTEST?");

  TEST_ASSERT_TRUE(events.contains(EventId::SelfTestReported));
  TEST_ASSERT_EQUAL_INT32(0, stepper.target_);
  TEST_ASSERT_FALSE(stepper.enabled_);
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

void test_modbus_flags_report_motion_until_position_reached() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = 400;

  std::uint8_t readFlags[8]{1U, 0x03U, 0U, 9U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, readFlags, 6U);
  TEST_ASSERT_EQUAL_UINT16(0x0005U, responseUint16(uart, 3U));

  uart.clearWritten();
  std::uint8_t writeTarget[8]{1U, 0x06U, 0U, 14U, 0U, 0U, 0U, 0U};
  writeModbusUint16(writeTarget, 4U, 750U);
  feedModbusFrame(server, writeTarget, 6U);

  uart.clearWritten();
  std::uint8_t readMovingFlags[8]{1U, 0x03U, 0U, 9U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, readMovingFlags, 6U);
  TEST_ASSERT_EQUAL_UINT16(0x000DU, responseUint16(uart, 3U));

  stepper.position_ = 600;
  controller.tick(kNoInputs, 20U);

  uart.clearWritten();
  std::uint8_t readReachedFlags[8]{1U, 0x03U, 0U, 9U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, readReachedFlags, 6U);
  TEST_ASSERT_EQUAL_UINT16(0x0005U, responseUint16(uart, 3U));
}

void test_modbus_degree_registers_move_and_configure_limits() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  stepper.position_ = 800;

  std::uint8_t readDegrees[8]{1U, 0x03U, 0U, 23U, 0U, 4U, 0U, 0U};
  feedModbusFrame(server, readDegrees, 6U);
  TEST_ASSERT_EQUAL_UINT8(8U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT16(45U, responseUint16(uart, 3U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 5U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 7U));
  TEST_ASSERT_EQUAL_UINT16(90U, responseUint16(uart, 9U));

  uart.clearWritten();
  std::uint8_t targetDegree[8]{1U, 0x06U, 0U, 23U, 0U, 45U, 0U, 0U};
  feedModbusFrame(server, targetDegree, 6U);
  TEST_ASSERT_EQUAL_INT32(400, controller.targetPosition());
  TEST_ASSERT_EQUAL_UINT32(8U, uart.writtenCount_);

  uart.clearWritten();
  std::uint8_t softMinDegree[8]{1U, 0x06U, 0U, 25U, 0U, 10U, 0U, 0U};
  feedModbusFrame(server, softMinDegree, 6U);
  TEST_ASSERT_EQUAL_INT32(711, controller.softMaxPosition());

  uart.clearWritten();
  std::uint8_t softMaxDegree[8]{1U, 0x06U, 0U, 26U, 0U, 80U, 0U, 0U};
  feedModbusFrame(server, softMaxDegree, 6U);
  TEST_ASSERT_EQUAL_INT32(88, controller.softMinPosition());

  uart.clearWritten();
  std::uint8_t invalidMinDegree[8]{1U, 0x06U, 0U, 25U, 0U, 85U, 0U, 0U};
  feedModbusFrame(server, invalidMinDegree, 6U);
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
  TEST_ASSERT_EQUAL_INT32(88, controller.softMinPosition());
  TEST_ASSERT_EQUAL_INT32(711, controller.softMaxPosition());
}

void test_modbus_stallguard_threshold_register_validates_and_updates() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t readThreshold[8]{1U, 0x03U, 0U, 27U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, readThreshold, 6U);
  TEST_ASSERT_EQUAL_UINT16(100U, responseUint16(uart, 3U));

  uart.clearWritten();
  std::uint8_t writeThreshold[8]{1U, 0x06U, 0U, 27U, 0U, 64U, 0U, 0U};
  feedModbusFrame(server, writeThreshold, 6U);
  TEST_ASSERT_EQUAL_UINT8(64U, controller.stallGuardThreshold());
  TEST_ASSERT_TRUE(events.contains(EventId::StallGuardThresholdChanged));
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x06U, uart.written_[1]);

  uart.clearWritten();
  std::uint8_t invalidThreshold[8]{1U, 0x06U, 0U, 27U, 1U, 0U, 0U, 0U};
  feedModbusFrame(server, invalidThreshold, 6U);
  TEST_ASSERT_EQUAL_UINT8(64U, controller.stallGuardThreshold());
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
}

void test_modbus_homing_config_registers_validate_and_update() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);

  std::uint8_t readDefaults[8]{1U, 0x03U, 0x00U, 0x1CU, 0x00U, 0x05U, 0U,
                               0U};
  feedModbusFrame(server, readDefaults, 6U);

  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 3U));
  TEST_ASSERT_EQUAL_UINT16(1U, responseUint16(uart, 5U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 7U));
  TEST_ASSERT_EQUAL_UINT16(1U, responseUint16(uart, 9U));
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 11U));

  uart.clearWritten();
  std::uint8_t writeConfig[19]{1U, 0x10U, 0x00U, 0x1CU, 0x00U, 0x05U,
                               0x0AU, 0x00U, 0x01U, 0x00U, 0x00U,
                               0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
                               0x01U, 0U, 0U};
  feedModbusFrame(server, writeConfig, 17U);

  const HomingConfig homing = controller.homingConfig();
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MaxInput),
      static_cast<std::uint8_t>(homing.minSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingSwitch::MinInput),
      static_cast<std::uint8_t>(homing.maxSwitch));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Positive),
      static_cast<std::uint8_t>(homing.minDirection));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(HomingDirection::Negative),
      static_cast<std::uint8_t>(homing.maxDirection));
  TEST_ASSERT_TRUE(homing.stepperDirectionInverted);

  uart.clearWritten();
  std::uint8_t invalidSameSwitch[8]{
      1U, 0x06U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::HomeMaxSwitch), 0x00U,
      0x01U, 0U, 0U};
  feedModbusFrame(server, invalidSameSwitch, 6U);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
}

void test_modbus_motor_config_registers_validate_and_update() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t readDefaults[8]{
      1U, 0x03U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::NormalMaxSpeed), 0x00U, 0x03U,
      0U, 0U};
  feedModbusFrame(server, readDefaults, 6U);

  TEST_ASSERT_EQUAL_UINT16(400U, responseUint16(uart, 3U));
  TEST_ASSERT_EQUAL_UINT16(200U, responseUint16(uart, 5U));
  TEST_ASSERT_EQUAL_UINT16(1000U, responseUint16(uart, 7U));

  uart.clearWritten();
  std::uint8_t writeConfig[15]{
      1U, 0x10U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::NormalMaxSpeed), 0x00U, 0x03U,
      0x06U, 0x03U, 0x84U, 0x00U, 0x96U, 0x03U, 0x52U, 0U, 0U};
  feedModbusFrame(server, writeConfig, 13U);

  TEST_ASSERT_EQUAL_UINT16(900U,
                           controller.motorConfig()
                               .normalMaxSpeedStepsPerSecond);
  TEST_ASSERT_EQUAL_UINT16(150U,
                           controller.motorConfig()
                               .homingMaxSpeedStepsPerSecond);
  TEST_ASSERT_EQUAL_UINT16(850U, controller.motorConfig().runCurrentMilliamps);
  TEST_ASSERT_TRUE(events.contains(EventId::MotorConfigChanged));
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x10U, uart.written_[1]);

  uart.clearWritten();
  std::uint8_t invalidCurrent[8]{
      1U, 0x06U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::RunCurrentMilliamps), 0x03U,
      0xE9U, 0U, 0U};
  feedModbusFrame(server, invalidCurrent, 6U);
  TEST_ASSERT_EQUAL_UINT16(850U, controller.motorConfig().runCurrentMilliamps);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
}

void test_modbus_auto_home_interval_register_validates_and_updates() {
  FakeStepper stepper;
  CapturingEvents events;
  FakeUart uart;
  FanFlapController controller(stepper, events, kFastTestConfig);
  ModbusRtuServer server(controller, uart);

  std::uint8_t readDefault[8]{
      1U, 0x03U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::AutoHomeIntervalMinutes), 0x00U,
      0x01U, 0U, 0U};
  feedModbusFrame(server, readDefault, 6U);
  TEST_ASSERT_EQUAL_UINT16(0U, responseUint16(uart, 3U));

  uart.clearWritten();
  std::uint8_t writeInterval[8]{
      1U, 0x06U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::AutoHomeIntervalMinutes), 0x05U,
      0xA0U, 0U, 0U};
  feedModbusFrame(server, writeInterval, 6U);
  TEST_ASSERT_EQUAL_UINT16(1440U, controller.autoHomeIntervalMinutes());
  TEST_ASSERT_TRUE(events.contains(EventId::AutoHomeIntervalChanged));
  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x06U, uart.written_[1]);

  uart.clearWritten();
  std::uint8_t invalidInterval[8]{
      1U, 0x06U, 0x00U,
      static_cast<std::uint8_t>(ModbusRegister::AutoHomeIntervalMinutes), 0x27U,
      0x61U, 0U, 0U};
  feedModbusFrame(server, invalidInterval, 6U);
  TEST_ASSERT_EQUAL_UINT16(1440U, controller.autoHomeIntervalMinutes());
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[2]);
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

void test_modbus_rejects_soft_endstop_writes_while_faulted() {
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

  std::uint8_t request[8]{1U, 0x06U, 0U, 0U, 0U, 250U, 0U, 0U};
  writeModbusUint16(request, 2U,
                    static_cast<std::uint16_t>(ModbusRegister::SoftMinLow));
  writeModbusUint16(request, 4U, 250U);
  feedModbusFrame(server, request, 6U);

  TEST_ASSERT_EQUAL_INT32(0, controller.softMinPosition());
  TEST_ASSERT_EQUAL_UINT32(0U, events.count_);
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
  TEST_ASSERT_EQUAL_UINT32(7U, uart.flushCount_);
  TEST_ASSERT_EQUAL_UINT32(56U, uart.writtenCount_);
  assertTmcWrite(uart, 0U, 0x00U, 0x000001C4UL);
  assertTmcWrite(uart, 8U, 0x10U, 0x00081F10UL);
  assertTmcWrite(uart, 16U, 0x11U, 0x00000014UL);
  assertTmcWrite(uart, 24U, 0x6CU, 0x14030053UL);
  assertTmcWrite(uart, 32U, 0x70U, 0xC80D0E24UL);
  assertTmcWrite(uart, 40U, 0x40U, 100U);
  assertTmcWrite(uart, 48U, 0x01U, 0x00000007UL);

  uart.clearWritten();
  TEST_ASSERT_FALSE(driver.pollStallGuard());
  TEST_ASSERT_EQUAL_UINT32(12U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(0x05U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x41U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT8(tmcCrc(uart.written_, 3U), uart.written_[3]);

  queueTmcResponseOnNextFlush(uart, 0x41U, 200U);
  TEST_ASSERT_FALSE(driver.pollStallGuard());

  queueTmcReadEchoOnNextFlush(uart, 0x41U);
  queueTmcResponseOnNextFlush(uart, 0x41U, 10U);
  TEST_ASSERT_TRUE(driver.pollStallGuard());
}

void test_tmc_driver_updates_stallguard_threshold_register_and_poll_limit() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  driver.setStallGuardThreshold(64U);

  TEST_ASSERT_EQUAL_UINT8(64U, driver.stallGuardThreshold());
  TEST_ASSERT_EQUAL_UINT32(8U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT8(0x05U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0xC0U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT8(64U, uart.written_[6]);
  TEST_ASSERT_EQUAL_UINT8(tmcCrc(uart.written_, 7U), uart.written_[7]);

  queueTmcResponseOnNextFlush(uart, 0x41U, 65U);
  TEST_ASSERT_EQUAL(Tmc2209PollResult::NotStalled,
                    driver.pollStallGuardStatus());

  queueTmcReadEchoOnNextFlush(uart, 0x41U);
  queueTmcResponseOnNextFlush(uart, 0x41U, 64U);
  TEST_ASSERT_EQUAL(Tmc2209PollResult::Stalled,
                    driver.pollStallGuardStatus());
}

void test_tmc_driver_updates_run_current_register_from_milliamps() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  driver.setRunCurrentMilliamps(500U);

  TEST_ASSERT_EQUAL_UINT16(500U, driver.runCurrentMilliamps());
  TEST_ASSERT_EQUAL_UINT32(8U, uart.writtenCount_);
  assertTmcWrite(uart, 0U, 0x10U, 0x00080F08UL);

  uart.clearWritten();
  driver.setRunCurrentMilliamps(1000U);

  TEST_ASSERT_EQUAL_UINT16(1000U, driver.runCurrentMilliamps());
  assertTmcWrite(uart, 0U, 0x10U, 0x00081F10UL);
}

void test_tmc_driver_accepts_datasheet_read_reply_master_address() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  queueTmcReadEchoOnNextFlush(uart, 0x41U);
  queueTmcResponseOnNextFlush(uart, 0x41U, 10U, 0xFFU);

  TEST_ASSERT_TRUE(driver.pollStallGuard());
}

void test_tmc_poll_reports_communication_error_without_response() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  const Tmc2209PollResult result = driver.pollStallGuardStatus();

  TEST_ASSERT_EQUAL(Tmc2209PollResult::CommunicationError, result);
  TEST_ASSERT_EQUAL_UINT32(12U, uart.writtenCount_);
  TEST_ASSERT_EQUAL_UINT32(3U, uart.flushCount_);
}

void test_tmc_poll_reports_not_stalled_above_threshold() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);
  queueTmcResponseOnNextFlush(uart, 0x41U, 0x000003FFUL);

  const Tmc2209PollResult result = driver.pollStallGuardStatus();

  TEST_ASSERT_EQUAL(Tmc2209PollResult::NotStalled, result);
}

void test_tmc_verify_communication_requires_valid_readback() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  TEST_ASSERT_FALSE(driver.verifyCommunication());

  queueTmcResponseOnNextFlush(uart, 0x00U, 0x000001C4UL);
  TEST_ASSERT_TRUE(driver.verifyCommunication());
}

void test_tmc_read_discards_stale_single_wire_write_echoes_before_poll() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  queueTmcWriteEcho(uart, 0x00U, 0x000001C4UL);
  queueTmcWriteEcho(uart, 0x10U, 0x00081F10UL);
  queueTmcWriteEcho(uart, 0x11U, 0x00000014UL);
  queueTmcWriteEcho(uart, 0x6CU, 0x14030053UL);
  queueTmcWriteEcho(uart, 0x70U, 0xC80D0E24UL);
  queueTmcWriteEcho(uart, 0x40U, 100U);
  queueTmcWriteEcho(uart, 0x01U, 0x00000007UL);
  queueTmcWriteEcho(uart, 0x40U, 64U);
  queueTmcReadEchoOnNextFlush(uart, 0x41U);
  queueTmcResponseOnNextFlush(uart, 0x41U, 200U, 0xFFU);

  const Tmc2209PollResult result = driver.pollStallGuardStatus();

  TEST_ASSERT_EQUAL(Tmc2209PollResult::NotStalled, result);
}

}  // namespace

void setUp() {}

void tearDown() {}

int main(int argc, char** argv) {
  static_cast<void>(argc);
  static_cast<void>(argv);

  UNITY_BEGIN();
  RUN_TEST(test_pico_pin_mapping_matches_current_tmc_wiring);
  RUN_TEST(test_persistent_settings_load_defaults_from_blank_storage);
  RUN_TEST(test_persistent_settings_save_and_reload_valid_settings);
  RUN_TEST(test_persistent_settings_save_and_reload_homing_config);
  RUN_TEST(test_persistent_settings_save_and_reload_motor_config);
  RUN_TEST(test_persistent_settings_save_and_reload_auto_home_interval);
  RUN_TEST(test_persistent_settings_reject_corrupt_or_out_of_range_data);
  RUN_TEST(test_persistent_settings_loads_newest_valid_record);
  RUN_TEST(test_persistent_settings_falls_back_to_previous_slot_after_corruption);
  RUN_TEST(test_persistent_settings_survives_target_slot_write_failure);
  RUN_TEST(test_homing_sequence_sets_range_and_midpoint);
  RUN_TEST(test_homing_config_defaults_match_existing_wiring);
  RUN_TEST(test_homing_config_maps_logical_min_and_max_to_selected_switches);
  RUN_TEST(test_homing_config_inverts_stepper_direction_without_changing_logic);
  RUN_TEST(test_input_debouncer_requires_stable_switch_state);
  RUN_TEST(test_both_endstops_active_at_boot_enters_fault);
  RUN_TEST(test_homing_moves_to_configured_safe_position);
  RUN_TEST(test_safe_position_commands_validate_and_report_permille);
  RUN_TEST(test_degree_position_commands_move_and_report_angle);
  RUN_TEST(test_degree_soft_endstop_commands_configure_allowed_angle_range);
  RUN_TEST(test_stallguard_threshold_command_validates_and_reports_setting);
  RUN_TEST(test_setup_set_commands_acknowledge_unchanged_values);
  RUN_TEST(test_homing_config_command_validates_reports_and_updates);
  RUN_TEST(test_homing_config_command_acknowledges_unchanged_config_during_homing);
  RUN_TEST(test_homing_config_command_can_restart_homing_during_setup);
  RUN_TEST(test_motor_config_command_validates_reports_and_updates);
  RUN_TEST(test_auto_home_interval_command_validates_reports_and_updates);
  RUN_TEST(test_homing_uses_stallguard_as_min_endstop_redundancy);
  RUN_TEST(test_homing_ignores_stallguard_until_minimum_travel_is_reached);
  RUN_TEST(test_homing_uses_stallguard_as_max_endstop_redundancy);
  RUN_TEST(test_homing_max_ignores_stallguard_until_minimum_travel_is_reached);
  RUN_TEST(test_invalid_homing_range_enters_fault_state);
  RUN_TEST(test_homing_min_fails_when_switch_is_not_reached_within_travel);
  RUN_TEST(test_fault_reason_records_homing_min_timeout);
  RUN_TEST(test_homing_max_fails_when_switch_is_not_reached_within_travel);
  RUN_TEST(test_fault_reason_records_unexpected_switch_direction);
  RUN_TEST(test_reset_timeout_is_exact_and_wraparound_safe);
  RUN_TEST(test_serial_goto_clamps_edges_and_rejects_bad_numbers);
  RUN_TEST(test_ready_move_reenables_driver_after_direct_service_tests);
  RUN_TEST(test_soft_endstop_configuration_handles_clamps_and_invalid_ranges);
  RUN_TEST(test_commands_are_safe_before_ready_and_status_is_always_available);
  RUN_TEST(test_ready_faults_on_unexpected_switch_and_stall);
  RUN_TEST(test_ready_ignores_stale_stallguard_when_motor_is_stopped);
  RUN_TEST(test_ready_ignores_stallguard_until_move_activation_travel_is_reached);
  RUN_TEST(test_fault_stops_and_disables_driver_in_same_tick);
  RUN_TEST(test_repeated_external_fault_does_not_flood_fault_counter);
  RUN_TEST(test_fault_latches_zero_speed_when_driver_stop_coasts);
  RUN_TEST(test_motor_test_runs_bounded_steps_and_refreshes_wait_timer);
  RUN_TEST(test_motor_test_rejects_unsafe_state_and_invalid_step_counts);
  RUN_TEST(test_motor_test_ignores_external_faults_until_bounded_motion_finishes);
  RUN_TEST(test_home_command_after_fault_does_not_bypass_disabled_driver);
  RUN_TEST(test_move_starts_and_passes_valve_free_check);
  RUN_TEST(test_move_reached_event_is_emitted_after_target_position_is_reached);
  RUN_TEST(test_new_move_reenables_driver_after_position_hold_disable);
  RUN_TEST(test_auto_home_interval_references_after_idle_interval);
  RUN_TEST(test_auto_home_interval_waits_until_move_is_finished);
  RUN_TEST(test_move_free_check_faults_when_valve_is_blocked);
  RUN_TEST(test_ready_move_faults_when_position_does_not_progress);
  RUN_TEST(test_free_check_times_out_before_required_travel);
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
  RUN_TEST(test_modbus_reads_release_diagnostic_registers);
  RUN_TEST(test_modbus_rejects_writes_to_release_diagnostic_registers);
  RUN_TEST(test_modbus_rejects_multiple_writes_to_release_diagnostic_registers);
  RUN_TEST(test_modbus_reads_full_release_register_block);
  RUN_TEST(test_modbus_reads_configured_boot_reason_register);
  RUN_TEST(test_modbus_rejects_read_past_release_register_block);
  RUN_TEST(test_fault_and_diag_text_commands_report_fault_snapshot);
  RUN_TEST(test_service_selftest_command_reports_without_moving_motor);
  RUN_TEST(test_modbus_write_target_permille_moves_when_ready);
  RUN_TEST(test_modbus_flags_report_motion_until_position_reached);
  RUN_TEST(test_modbus_degree_registers_move_and_configure_limits);
  RUN_TEST(test_modbus_stallguard_threshold_register_validates_and_updates);
  RUN_TEST(test_modbus_homing_config_registers_validate_and_update);
  RUN_TEST(test_modbus_motor_config_registers_validate_and_update);
  RUN_TEST(test_modbus_auto_home_interval_register_validates_and_updates);
  RUN_TEST(test_modbus_broadcast_write_is_ignored_for_home_safety);
  RUN_TEST(test_modbus_safe_position_register_validates_and_updates);
  RUN_TEST(test_modbus_rejects_soft_endstop_writes_while_faulted);
  RUN_TEST(test_modbus_rejects_invalid_register_and_device_id);
  RUN_TEST(test_modbus_rejects_invalid_command_values);
  RUN_TEST(test_modbus_refresh_machine_command_rehomes_from_fault);
  RUN_TEST(test_modbus_write_multiple_registers_is_atomic_on_late_error);
  RUN_TEST(test_modbus_write_multiple_target_words_uses_final_32bit_value);
  RUN_TEST(test_modbus_write_multiple_target_words_preserves_signed_32bit_value);
  RUN_TEST(test_modbus_write_multiple_rejects_command_mixed_with_target);
  RUN_TEST(test_tmc_driver_initializes_and_polls_stall_status);
  RUN_TEST(test_tmc_driver_updates_stallguard_threshold_register_and_poll_limit);
  RUN_TEST(test_tmc_driver_updates_run_current_register_from_milliamps);
  RUN_TEST(test_tmc_driver_accepts_datasheet_read_reply_master_address);
  RUN_TEST(test_tmc_poll_reports_communication_error_without_response);
  RUN_TEST(test_tmc_poll_reports_not_stalled_above_threshold);
  RUN_TEST(test_tmc_verify_communication_requires_valid_readback);
  RUN_TEST(test_tmc_read_discards_stale_single_wire_write_echoes_before_poll);
  return UNITY_END();
}

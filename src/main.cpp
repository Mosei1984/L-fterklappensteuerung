#include <Arduino.h>
#include <AccelStepper.h>

#include <cstddef>
#include <cstdint>
#include <array>

#include <FanFlapController.h>
#include <InputDebouncer.h>
#include <ModbusRtuServer.h>
#include <PersistentSettings.h>
#include <Tmc2209Driver.h>

#include "FirmwarePins.h"

#ifndef LUEFTERKLAPPE_ENABLE_WATCHDOG
#define LUEFTERKLAPPE_ENABLE_WATCHDOG 1
#endif

#include "drivers/FlashIAP.h"
#if defined(DEVICE_RESET_REASON)
#include "drivers/ResetReason.h"
#endif
#if LUEFTERKLAPPE_ENABLE_WATCHDOG && defined(DEVICE_WATCHDOG)
#include "drivers/Watchdog.h"
#endif
#include "pico/bootrom.h"

#ifndef LUEFTERKLAPPE_USE_TMC2209_UART
#define LUEFTERKLAPPE_USE_TMC2209_UART 1
#endif

#ifndef LUEFTERKLAPPE_EVENTS_TO_RS485
#define LUEFTERKLAPPE_EVENTS_TO_RS485 0
#endif

#ifndef LUEFTERKLAPPE_REQUIRE_TMC2209_UART
#define LUEFTERKLAPPE_REQUIRE_TMC2209_UART 1
#endif

#ifndef LUEFTERKLAPPE_DEFAULT_DEVICE_ID
#define LUEFTERKLAPPE_DEFAULT_DEVICE_ID 1
#endif

namespace {

using luefterklappe::BootReason;
using luefterklappe::ControllerState;
using luefterklappe::DelayPort;
using luefterklappe::DigitalInputs;
using luefterklappe::Event;
using luefterklappe::EventId;
using luefterklappe::EventSink;
using luefterklappe::FanFlapController;
using luefterklappe::FaultReason;
using luefterklappe::InputDebouncer;
using luefterklappe::ModbusRtuServer;
using luefterklappe::PersistentSettings;
using luefterklappe::PersistentSettingsStore;
using luefterklappe::SettingsStoragePort;
using luefterklappe::StepperPort;
using luefterklappe::UartPort;
using luefterklappe::firmware::kDirPin;
using luefterklappe::firmware::kEnablePin;
using luefterklappe::firmware::kMaxSwitchPin;
using luefterklappe::firmware::kMinSwitchPin;
using luefterklappe::firmware::kStepPin;
using luefterklappe::firmware::kTmcUartRxPin;
using luefterklappe::firmware::kTmcUartTxPin;
#if LUEFTERKLAPPE_USE_TMC2209_UART
using luefterklappe::Tmc2209Driver;
using luefterklappe::Tmc2209PollResult;
#endif

constexpr std::uint8_t kStatusLedPin = LED_BUILTIN;
constexpr std::uint16_t kDefaultDeviceId = LUEFTERKLAPPE_DEFAULT_DEVICE_ID;
constexpr std::uint32_t kUsbDebugBaud = 115200UL;
constexpr std::uint32_t kModuleBaud = 38400UL;
constexpr std::uint32_t kTmcBaud = 115200UL;
constexpr std::size_t kCommandBufferSize = 64U;
constexpr std::uint32_t kModbusFrameTimeoutMs = 5UL;
constexpr std::uint32_t kTextCommandTimeoutMs = 1000UL;
constexpr std::uint32_t kInputDebounceMs = 10UL;
constexpr std::uint32_t kLedSlowBlinkMs = 500UL;
constexpr std::uint32_t kLedFastBlinkMs = 125UL;
constexpr std::uint32_t kWatchdogTimeoutMs = 2000UL;
constexpr unsigned int kStepPulseWidthUs = 20U;
constexpr unsigned int kDirectStepHighUs = 100U;
constexpr unsigned int kDirectStepLowUs = 900U;
constexpr unsigned int kSlowDirectStepHighUs = 1000U;
constexpr unsigned int kSlowDirectStepLowUs = 4000U;
constexpr std::uint32_t kDriverEnableSettleMs = 50UL;
constexpr std::int32_t kMaxDirectStepTestSteps = 3200;
constexpr std::uint8_t kPinWalkPins[] = {2U, 3U, 7U};
constexpr std::uint8_t kPinWalkPulseCounts[] = {2U, 3U, 7U};
constexpr std::uint32_t kPinWalkPulseMs = 20UL;

void kickWatchdog();
constexpr std::uint32_t kPinWalkGapMs = 80UL;
constexpr std::size_t kMaxBusBytesPerLoop = 32U;
constexpr std::size_t kSettingsFlashScratchSize = 4096U;
constexpr std::size_t kSettingsFlashSlotCount = 2U;

static_assert(PIN_SERIAL_TX == 0UL,
              "Serial1 TX must be GP0 for Waveshare RS485 CH0.");
static_assert(PIN_SERIAL_RX == 1UL,
              "Serial1 RX must be GP1 for Waveshare RS485 CH0.");
static_assert((kDefaultDeviceId >= 1U) && (kDefaultDeviceId <= 247U),
              "Default Modbus device ID must be in range 1..247.");

AccelStepper stepper(AccelStepper::DRIVER, kStepPin, kDirPin);

enum class BusProtocol : std::uint8_t { Idle, TextCandidate, Text, Modbus };
enum class StatusLedMode : std::uint8_t { Off, On, SlowBlink, FastBlink };

class ArduinoStepperPort final : public StepperPort {
 public:
  void setDriverEnabled(const bool enabled) override {
    const bool wasEnabled = driverEnabled_;
    digitalWrite(kEnablePin, enabled ? LOW : HIGH);
    driverEnabled_ = enabled;
    if (enabled && !wasEnabled) {
      delay(kDriverEnableSettleMs);
    }
  }

  void setMaxSpeed(const float speed) override { stepper.setMaxSpeed(speed); }

  void setAcceleration(const float acceleration) override {
    stepper.setAcceleration(acceleration);
  }

  void moveTo(const std::int32_t position) override {
    stepper.moveTo(static_cast<long>(position));
  }

  void run() override { static_cast<void>(stepper.run()); }

  void stop() override { stepper.stop(); }

  void setCurrentPosition(const std::int32_t position) override {
    stepper.setCurrentPosition(static_cast<long>(position));
  }

  std::int32_t currentPosition() const override {
    return static_cast<std::int32_t>(stepper.currentPosition());
  }

  float speed() const override { return stepper.speed(); }

 private:
  bool driverEnabled_{false};
};

class ArduinoUartPort final : public UartPort {
 public:
  explicit ArduinoUartPort(arduino::HardwareSerial& serial) : serial_(serial) {}

  void writeByte(const std::uint8_t value) override {
    static_cast<void>(serial_.write(value));
  }

  void flush() override { serial_.flush(); }

  std::size_t available() const override {
    const int availableBytes = serial_.available();
    if (availableBytes <= 0) {
      return 0U;
    }

    return static_cast<std::size_t>(availableBytes);
  }

  bool readByte(std::uint8_t& value) override {
    const int nextByte = serial_.read();

    if (nextByte < 0) {
      return false;
    }

    value = static_cast<std::uint8_t>(nextByte);
    return true;
  }

 private:
  arduino::HardwareSerial& serial_;
};

class ArduinoDelayPort final : public DelayPort {
 public:
  void delayMilliseconds(const std::uint32_t milliseconds) override {
    delay(milliseconds);
  }
};

class FlashSettingsStorage final : public SettingsStoragePort {
 public:
  std::size_t slotCount() const override { return kSettingsFlashSlotCount; }

  std::size_t slotSize() const override { return kSettingsFlashScratchSize; }

  bool readSlot(const std::size_t slot, std::uint8_t* const data,
                const std::size_t size) override {
    if (!ensureReady() || (slot >= kSettingsFlashSlotCount) ||
        (size > sectorSize_)) {
      return false;
    }

    return flash_.read(data, slotAddress(slot),
                       static_cast<std::uint32_t>(size)) == 0;
  }

  bool writeSlot(const std::size_t slot, const std::uint8_t* const data,
                 const std::size_t size) override {
    if (!ensureReady() || (slot >= kSettingsFlashSlotCount) ||
        (size > sectorSize_) || (sectorSize_ > scratch_.size())) {
      return false;
    }

    const std::uint8_t eraseValue = flash_.get_erase_value();
    for (std::uint8_t& byte : scratch_) {
      byte = eraseValue;
    }

    for (std::size_t index = 0U; index < size; ++index) {
      scratch_[index] = data[index];
    }

    const std::uint32_t address = slotAddress(slot);
    if (flash_.erase(address, sectorSize_) != 0) {
      return false;
    }

    return flash_.program(scratch_.data(), address, sectorSize_) == 0;
  }

 private:
  bool ensureReady() {
    if (ready_) {
      return true;
    }

    if (flash_.init() != 0) {
      return false;
    }

    const std::uint32_t flashStart = flash_.get_flash_start();
    const std::uint32_t flashSize = flash_.get_flash_size();
    const std::uint32_t lastAddress =
        static_cast<std::uint32_t>(flashStart + flashSize - 1U);
    sectorSize_ = flash_.get_sector_size(lastAddress);

    if ((sectorSize_ == 0U) || (sectorSize_ > scratch_.size()) ||
        (flashSize < (sectorSize_ * kSettingsFlashSlotCount))) {
      return false;
    }

    storageAddress_ = static_cast<std::uint32_t>(flashStart + flashSize -
                                                 (sectorSize_ *
                                                  kSettingsFlashSlotCount));
    ready_ = true;
    return true;
  }

  std::uint32_t slotAddress(const std::size_t slot) const {
    return static_cast<std::uint32_t>(storageAddress_ +
                                      (static_cast<std::uint32_t>(slot) *
                                       sectorSize_));
  }

  mbed::FlashIAP flash_;
  std::array<std::uint8_t, kSettingsFlashScratchSize> scratch_{};
  std::uint32_t storageAddress_{0U};
  std::uint32_t sectorSize_{0U};
  bool ready_{false};
};

void persistSettingsFromEvent(const Event& event);
bool readSwitch(std::uint8_t pin);

class SerialEventSink final : public EventSink {
 public:
  void onEvent(const Event& event) override {
    switch (event.id) {
      case EventId::SystemStarted:
        output().println(F("System gestartet."));
        break;
      case EventId::HomingStarted:
        output().println(F("Starte Homing."));
        break;
      case EventId::MinPositionReached:
        output().println(F("Min-Position erreicht."));
        break;
      case EventId::MaxPositionReached:
        output().print(F("Max-Position erreicht bei Steps: "));
        output().println(event.first);
        break;
      case EventId::HomingRangeInvalid:
        output().print(F("FEHLER: ungueltiger Homing-Bereich: "));
        output().println(event.first);
        break;
      case EventId::SoftEndstopsSet:
        output().print(F("Soft Endstops gesetzt: Min="));
        output().print(event.first);
        output().print(F(", Max="));
        output().println(event.second);
        break;
      case EventId::SoftEndstopMinClamped:
        output().print(F("Soft Min Endstop erreicht. Begrenze auf "));
        output().println(event.first);
        break;
      case EventId::SoftEndstopMaxClamped:
        output().print(F("Soft Max Endstop erreicht. Begrenze auf "));
        output().println(event.first);
        break;
      case EventId::SoftEndstopRangeInvalid:
        output().print(F("Fehler: Min muss kleiner als Max sein. Min="));
        output().print(event.first);
        output().print(F(", Max="));
        output().println(event.second);
        break;
      case EventId::MoveAccepted:
        output().print(F("Bewege zu Position: "));
        output().println(event.first);
        break;
      case EventId::MoveReached:
        output().print(F("Position erreicht: Steps="));
        output().print(event.first);
        output().print(F(" Promille="));
        output().print(event.second);
        output().print(F(" Grad="));
        output().println(event.third);
        break;
      case EventId::MotorNotReady:
        output().println(F("Motor nicht bereit."));
        break;
      case EventId::PositionReported:
        output().print(F("Aktuelle Position: "));
        output().println(event.first);
        break;
      case EventId::DegreePositionReported:
        output().print(F("Aktuelle Position Grad: "));
        output().println(event.first);
        break;
      case EventId::ResetDuringWait:
        output().println(F("RESET empfangen. Starte Homing neu."));
        break;
      case EventId::ResetIgnored:
        output().println(F("RESET ignoriert, kein Fehler aktiv."));
        break;
      case EventId::ManualHomingStarted:
        output().println(F("Manuelles Homing gestartet."));
        break;
      case EventId::MachineRefreshStarted:
        output().println(F("Machine Refresh gestartet. Homing ohne MCU-Reset."));
        break;
      case EventId::SoftEndstopsEnabled:
        output().println(F("Soft Endstops aktiviert."));
        break;
      case EventId::SoftEndstopsDisabled:
        output().println(F("Soft Endstops deaktiviert."));
        break;
      case EventId::SoftEndstopsStatus:
        output().print(F("Soft Endstops: "));
        output().println(event.flag ? F("aktiviert") : F("deaktiviert"));
        output().print(F("Min: "));
        output().print(event.first);
        output().print(F(", Max: "));
        output().println(event.second);
        break;
      case EventId::DegreeLimitsReported:
        output().print(F("Grad-Limits: Min="));
        output().print(event.first);
        output().print(F(", Max="));
        output().println(event.second);
        break;
      case EventId::DeviceIdReported:
        output().print(F("ID: "));
        output().println(event.first);
        break;
      case EventId::DeviceIdChanged:
        output().print(F("ID gesetzt: "));
        output().println(event.first);
        break;
      case EventId::InvalidDeviceId:
        output().println(F("Ungueltige ID. Erlaubt: 1..247."));
        break;
      case EventId::SafePositionReported:
        output().print(F("Safe-Position Promille: "));
        output().println(event.first);
        break;
      case EventId::SafePositionChanged:
        output().print(F("Safe-Position gesetzt: "));
        output().println(event.first);
        break;
      case EventId::SafePositionInvalid:
        output().println(F("Ungueltige Safe-Position. Erlaubt: 0..1000."));
        break;
      case EventId::SafePositionApplied:
        output().print(F("Safe-Position angefahren: "));
        output().println(event.first);
        break;
      case EventId::SensorlessMinPositionReached:
        output().println(F("Min-Position sensorlos per StallGuard erreicht."));
        break;
      case EventId::SensorlessMaxPositionReached:
        output().print(F("Max-Position sensorlos per StallGuard erreicht: "));
        output().println(event.first);
        break;
      case EventId::ValveFreeCheckStarted:
        output().println(F("Ventil-Freilaufpruefung gestartet."));
        break;
      case EventId::ValveFreeCheckPassed:
        output().println(F("Ventil-Freilaufpruefung bestanden."));
        break;
      case EventId::UnknownCommand:
        output().println(F("Unbekannter Befehl."));
        break;
      case EventId::InvalidCommandArgument:
        output().println(F("Ungueltiges Befehlsargument."));
        break;
      case EventId::ErrorMotorStopped:
        output().println(F("FEHLER: Motor gestoppt. REFRESH oder RESET moeglich."));
        break;
      case EventId::AutoRehomeTimeout:
        output().println(F("Reset-Timeout erreicht. Automatisches Re-Homing."));
        break;
      case EventId::StallDetected:
        output().println(F("Stall erkannt."));
        break;
      case EventId::StallGuardThresholdReported:
        output().print(F("StallGuard Threshold: "));
        output().println(event.first);
        break;
      case EventId::StallGuardThresholdChanged:
        output().print(F("StallGuard Threshold gesetzt: "));
        output().println(event.first);
        break;
      case EventId::StallGuardThresholdInvalid:
        output().println(F("Ungueltiger StallGuard Threshold. Erlaubt: 0..255."));
        break;
      case EventId::TmcInitializationStarted:
        output().println(F("Initialisiere TMC2209."));
        break;
      case EventId::TmcConfigured:
        output().println(F("TMC2209 konfiguriert."));
        break;
      case EventId::FaultReported:
        output().print(F("FAULT REASON="));
        output().print(event.first);
        output().print(F(" COUNT="));
        output().println(event.second);
        break;
      case EventId::DiagnosticsReported:
        output().print(F("DIAG FAULT="));
        output().print(event.first);
        output().print(F(" FAULTCOUNT="));
        output().println(event.second);
        break;
      case EventId::SelfTestReported:
        output().print(F("SELFTEST STATE="));
        output().println(event.first);
        output().print(F("SELFTEST ID="));
        output().println(event.fourth);
        output().print(F("SELFTEST SAFE="));
        output().println(event.fifth);
        output().print(F("SELFTEST FAULT="));
        output().println(event.second);
        output().print(F("SELFTEST FAULTCOUNT="));
        output().println(event.third);
        output().print(F("SELFTEST SOFTENDSTOPS="));
        output().println(event.flag ? 1 : 0);
        output().print(F("SELFTEST ENDSTOPS MIN="));
        output().print(readSwitch(kMinSwitchPin) ? 1 : 0);
        output().print(F(" MAX="));
        output().println(readSwitch(kMaxSwitchPin) ? 1 : 0);
        break;
      case EventId::MotorTestStarted:
        output().print(F("Motor-Test gestartet: "));
        output().print(event.first);
        output().println(F(" Steps."));
        break;
      case EventId::MotorTestFinished:
        output().print(F("Motor-Test beendet: "));
        output().print(event.first);
        output().println(F(" Steps."));
        break;
    }

    persistSettingsFromEvent(event);
  }

 private:
  static Print& output() {
#if LUEFTERKLAPPE_EVENTS_TO_RS485
    return Serial1;
#else
    return Serial;
#endif
  }
};

ArduinoStepperPort stepperPort;
ArduinoUartPort rs485Port(Serial1);
#if LUEFTERKLAPPE_USE_TMC2209_UART
arduino::UART tmcSerial(kTmcUartTxPin, kTmcUartRxPin);
ArduinoUartPort tmcUartPort(tmcSerial);
#endif
ArduinoDelayPort delayPort;
SerialEventSink eventSink;
InputDebouncer inputDebouncer(kInputDebounceMs);
FanFlapController controller(stepperPort, eventSink);
ModbusRtuServer modbusServer(controller, rs485Port);
#if LUEFTERKLAPPE_USE_TMC2209_UART
Tmc2209Driver tmcDriver(tmcUartPort, delayPort, eventSink);
#endif
PersistentSettings activeSettings{
    static_cast<std::uint8_t>(kDefaultDeviceId),
    luefterklappe::kDefaultControllerConfig.safePositionPermille,
    luefterklappe::kDefaultTmc2209Config.stallGuardThreshold};
FlashSettingsStorage settingsStorage;
PersistentSettingsStore settingsStore(settingsStorage, activeSettings);
BootReason bootReason = BootReason::Unknown;
bool settingsPersistenceEnabled = false;

char commandBuffer[kCommandBufferSize]{};
std::size_t commandLength = 0U;
bool commandOverflow = false;
char usbCommandBuffer[kCommandBufferSize]{};
std::size_t usbCommandLength = 0U;
bool usbCommandOverflow = false;
BusProtocol busProtocol = BusProtocol::Idle;
std::uint32_t lastBusByteMs = 0U;

void persistSettingsFromEvent(const Event& event) {
  if (!settingsPersistenceEnabled) {
    return;
  }

  bool changed = false;

  switch (event.id) {
    case EventId::DeviceIdChanged:
      if ((event.first >= 1) && (event.first <= 247)) {
        activeSettings.deviceId = static_cast<std::uint8_t>(event.first);
        changed = true;
      }
      break;
    case EventId::SafePositionChanged:
      if ((event.first >= 0) && (event.first <= 1000)) {
        activeSettings.safePositionPermille =
            static_cast<std::uint16_t>(event.first);
        changed = true;
      }
      break;
    case EventId::StallGuardThresholdChanged:
      if ((event.first >= 0) && (event.first <= 255)) {
        activeSettings.stallGuardThreshold =
            static_cast<std::uint8_t>(event.first);
#if LUEFTERKLAPPE_USE_TMC2209_UART
        tmcDriver.setStallGuardThreshold(activeSettings.stallGuardThreshold);
#endif
        changed = true;
      }
      break;
    default:
      break;
  }

  if (changed && !settingsStore.save(activeSettings)) {
    Serial.println(F("WARNUNG: Einstellungen konnten nicht gespeichert werden."));
  }
}

void resetCommandBuffer() {
  commandLength = 0U;
  commandOverflow = false;
  commandBuffer[0] = '\0';
}

void resetUsbCommandBuffer() {
  usbCommandLength = 0U;
  usbCommandOverflow = false;
  usbCommandBuffer[0] = '\0';
}

void handleCompletedCommand() {
  const bool isAddressedCommand =
      (commandLength >= 3U) && (commandBuffer[0] == 'I') &&
      (commandBuffer[1] == 'D') && (commandBuffer[2] >= '0') &&
      (commandBuffer[2] <= '9');

  if (!commandOverflow && isAddressedCommand) {
    commandBuffer[commandLength] = '\0';
    controller.handleCommand(commandBuffer);
  }

  resetCommandBuffer();
}

bool usbCommandEquals(const char* const expected) {
  std::size_t index = 0U;
  while ((index < usbCommandLength) && (expected[index] != '\0')) {
    if (usbCommandBuffer[index] != expected[index]) {
      return false;
    }
    ++index;
  }

  return (index == usbCommandLength) && (expected[index] == '\0');
}

void prepareSafeOutputs();

bool usbCommandArgument(const char* const command, std::int32_t& value) {
  std::size_t index = 0U;
  while ((index < usbCommandLength) && (command[index] != '\0')) {
    if (usbCommandBuffer[index] != command[index]) {
      return false;
    }
    ++index;
  }

  if ((command[index] != '\0') || (index >= usbCommandLength) ||
      (usbCommandBuffer[index] != ' ')) {
    return false;
  }

  ++index;
  bool negative = false;
  if ((index < usbCommandLength) && (usbCommandBuffer[index] == '-')) {
    negative = true;
    ++index;
  }

  if (index >= usbCommandLength) {
    return false;
  }

  std::int32_t parsed = 0;
  while (index < usbCommandLength) {
    const char digit = usbCommandBuffer[index];
    if ((digit < '0') || (digit > '9')) {
      return false;
    }
    parsed = (parsed * 10) + static_cast<std::int32_t>(digit - '0');
    ++index;
  }

  value = negative ? -parsed : parsed;
  return true;
}

void printPinDiagnostics() {
  Serial.print(F("PINS STEP="));
  Serial.print(kStepPin);
  Serial.print(F(" DIR="));
  Serial.print(kDirPin);
  Serial.print(F(" EN="));
  Serial.print(kEnablePin);
  Serial.print(F(" TMC_TX="));
  Serial.print(kTmcUartTxPin);
  Serial.print(F(" TMC_RX="));
  Serial.print(kTmcUartRxPin);
  Serial.print(F(" STEP_US="));
  Serial.println(kStepPulseWidthUs);
}

bool isBringupStepPin(const std::uint8_t pin) {
  return (pin == 2U) || (pin == 3U);
}

void runDirectStepTestOnPins(const std::uint8_t stepPin,
                             const std::uint8_t dirPin,
                             const std::int32_t steps,
                             const unsigned int highUs,
                             const unsigned int lowUs,
                             const __FlashStringHelper* const label) {
  if ((steps == 0) || (steps < -kMaxDirectStepTestSteps) ||
      (steps > kMaxDirectStepTestSteps) || !isBringupStepPin(stepPin) ||
      !isBringupStepPin(dirPin) || (stepPin == dirPin)) {
    Serial.println(F("STEPTEST ungueltig."));
    return;
  }

  const std::int32_t absoluteSteps = steps < 0 ? -steps : steps;
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin, steps >= 0 ? HIGH : LOW);
  digitalWrite(kEnablePin, LOW);
  delay(kDriverEnableSettleMs);
  delayMicroseconds(kStepPulseWidthUs);

  for (std::int32_t index = 0; index < absoluteSteps; ++index) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(highUs);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(lowUs);
    if ((index % 64) == 0) {
      kickWatchdog();
    }
  }

  digitalWrite(kEnablePin, HIGH);
  Serial.print(label);
  Serial.print(F(" beendet: STEP=GP"));
  Serial.print(stepPin);
  Serial.print(F(" DIR=GP"));
  Serial.print(dirPin);
  Serial.print(F(" "));
  Serial.print(steps);
  Serial.println(F(" Steps."));
}

void runDirectStepTest(const std::int32_t steps) {
  runDirectStepTestOnPins(kStepPin, kDirPin, steps, kSlowDirectStepHighUs,
                          kSlowDirectStepLowUs, F("STEPTEST"));
}

#if LUEFTERKLAPPE_USE_TMC2209_UART
void readTmcRegister(const std::int32_t registerAddress) {
  if ((registerAddress < 0) || (registerAddress > 127)) {
    Serial.println(F("TMCREAD ungueltig."));
    return;
  }

  std::uint32_t value = 0U;
  const auto address = static_cast<std::uint8_t>(registerAddress);
  if (!tmcDriver.readRegister(address, value)) {
    Serial.print(F("TMCREAD FEHLER REG="));
    Serial.println(registerAddress);
    return;
  }

  Serial.print(F("TMCREAD REG="));
  Serial.print(registerAddress);
  Serial.print(F(" VALUE=0x"));
  Serial.print(value, HEX);
  Serial.print(F(" DEC="));
  Serial.println(value);
}
#endif

void runPinWalk() {
  Serial.println(F("PINWALK Start."));

  for (std::size_t pinIndex = 0U; pinIndex < 3U; ++pinIndex) {
    const std::uint8_t pin = kPinWalkPins[pinIndex];
    const std::uint8_t pulseCount = kPinWalkPulseCounts[pinIndex];

    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(kPinWalkPulseMs);
    for (std::uint8_t pulse = 0U; pulse < pulseCount; ++pulse) {
      digitalWrite(pin, HIGH);
      delay(kPinWalkPulseMs);
      digitalWrite(pin, LOW);
      delay(kPinWalkPulseMs);
    }
    delay(kPinWalkGapMs);
  }

  prepareSafeOutputs();
  Serial.println(F("PINWALK Ende."));
}

void runPinPulse(const std::int32_t pin) {
  if ((pin != 2) && (pin != 3) && (pin != 7)) {
    Serial.println(F("PINPULSE ungueltig."));
    return;
  }

  const auto pulsePin = static_cast<std::uint8_t>(pin);
  pinMode(pulsePin, OUTPUT);
  digitalWrite(pulsePin, LOW);
  delay(50);
  for (std::uint8_t pulse = 0U; pulse < 20U; ++pulse) {
    digitalWrite(pulsePin, HIGH);
    delay(10);
    digitalWrite(pulsePin, LOW);
    delay(10);
  }
  prepareSafeOutputs();
  Serial.print(F("PINPULSE beendet: GP"));
  Serial.println(pin);
}

void rebootToBootsel() {
  Serial.println(F("BOOTSEL Neustart."));
  Serial.flush();
  delay(50);
  reset_usb_boot(1U << digitalPinToPinName(LED_BUILTIN), 0U);
}

void handleCompletedUsbCommand() {
  if (!usbCommandOverflow && (usbCommandLength > 0U)) {
    usbCommandBuffer[usbCommandLength] = '\0';
    std::int32_t directStepTestSteps = 0;
    std::int32_t pulsePin = 0;
    if (usbCommandEquals("PINS?")) {
      printPinDiagnostics();
    } else if (usbCommandEquals("BOOTSEL")) {
      rebootToBootsel();
#if LUEFTERKLAPPE_USE_TMC2209_UART
    } else if (usbCommandArgument("TMCREAD", directStepTestSteps)) {
      readTmcRegister(directStepTestSteps);
    } else if (usbCommandEquals("TMCREINIT")) {
      tmcDriver.initialize();
      tmcDriver.setStallGuardThreshold(activeSettings.stallGuardThreshold);
      Serial.println(F("TMCREINIT beendet."));
#endif
    } else if (usbCommandEquals("PINWALK")) {
      runPinWalk();
    } else if (usbCommandArgument("PINPULSE", pulsePin)) {
      runPinPulse(pulsePin);
    } else if (usbCommandArgument("STEPTEST2", directStepTestSteps)) {
      runDirectStepTestOnPins(2U, 3U, directStepTestSteps, kDirectStepHighUs,
                              kDirectStepLowUs, F("STEPTEST2"));
    } else if (usbCommandArgument("STEPTEST3", directStepTestSteps)) {
      runDirectStepTestOnPins(3U, 2U, directStepTestSteps, kDirectStepHighUs,
                              kDirectStepLowUs, F("STEPTEST3"));
    } else if (usbCommandArgument("STEPTEST2SLOW", directStepTestSteps)) {
      runDirectStepTestOnPins(2U, 3U, directStepTestSteps,
                              kSlowDirectStepHighUs, kSlowDirectStepLowUs,
                              F("STEPTEST2SLOW"));
    } else if (usbCommandArgument("STEPTEST3SLOW", directStepTestSteps)) {
      runDirectStepTestOnPins(3U, 2U, directStepTestSteps,
                              kSlowDirectStepHighUs, kSlowDirectStepLowUs,
                              F("STEPTEST3SLOW"));
    } else if (usbCommandArgument("STEPTEST", directStepTestSteps)) {
      runDirectStepTest(directStepTestSteps);
    } else {
      controller.handleCommand(usbCommandBuffer);
    }
  }

  resetUsbCommandBuffer();
}

void resetBusFrame() {
  resetCommandBuffer();
  modbusServer.reset();
  busProtocol = BusProtocol::Idle;
}

bool hasElapsed(const std::uint32_t nowMs, const std::uint32_t sinceMs,
                const std::uint32_t durationMs) {
  return static_cast<std::uint32_t>(nowMs - sinceMs) >= durationMs;
}

StatusLedMode statusLedModeForState(const ControllerState state) {
  switch (state) {
    case ControllerState::Init:
    case ControllerState::HomingMin:
    case ControllerState::HomingMinSettling:
    case ControllerState::HomingMax:
    case ControllerState::HomingMaxSettling:
    case ControllerState::AutoRehome:
      return StatusLedMode::SlowBlink;
    case ControllerState::Ready:
      return StatusLedMode::On;
    case ControllerState::ErrorDetected:
    case ControllerState::WaitReset:
    case ControllerState::ServiceMotorTest:
      return StatusLedMode::FastBlink;
  }

  return StatusLedMode::Off;
}

void updateStatusLed(const std::uint32_t nowMs) {
  const StatusLedMode mode = statusLedModeForState(controller.state());
  bool enabled = false;

  switch (mode) {
    case StatusLedMode::Off:
      enabled = false;
      break;
    case StatusLedMode::On:
      enabled = true;
      break;
    case StatusLedMode::SlowBlink:
      enabled = ((nowMs / kLedSlowBlinkMs) % 2UL) == 0UL;
      break;
    case StatusLedMode::FastBlink:
      enabled = ((nowMs / kLedFastBlinkMs) % 2UL) == 0UL;
      break;
  }

  digitalWrite(kStatusLedPin, enabled ? HIGH : LOW);
}

std::uint32_t busFrameTimeoutMs() {
  if (busProtocol == BusProtocol::Text) {
    return kTextCommandTimeoutMs;
  }

  return kModbusFrameTimeoutMs;
}

bool isTextProtocolCandidateStart(const std::uint8_t value) {
  return value == 'I';
}

void startTextCandidate() {
  resetCommandBuffer();
  commandBuffer[0] = 'I';
  commandLength = 1U;
  busProtocol = BusProtocol::TextCandidate;
}

void replayTextCandidateAsModbus(const std::uint8_t value) {
  modbusServer.handleByte(static_cast<std::uint8_t>('I'));
  resetCommandBuffer();
  busProtocol = BusProtocol::Modbus;
  modbusServer.handleByte(value);
}

void handleTextByte(const char nextChar) {
  if (nextChar == '\n') {
    handleCompletedCommand();
    busProtocol = BusProtocol::Idle;
  } else if (nextChar == '\r') {
    // Ignore CR. LF completes the command.
  } else if (commandLength < (kCommandBufferSize - 1U)) {
    commandBuffer[commandLength] = nextChar;
    ++commandLength;
  } else {
    commandOverflow = true;
  }
}

void pollRs485Bus(const std::uint32_t nowMs) {
  if ((busProtocol != BusProtocol::Idle) &&
      hasElapsed(nowMs, lastBusByteMs, busFrameTimeoutMs())) {
    resetBusFrame();
  }

  std::size_t processedBytes = 0U;
  while ((Serial1.available() > 0) && (processedBytes < kMaxBusBytesPerLoop)) {
    const int nextByte = Serial1.read();

    if (nextByte < 0) {
      return;
    }

    ++processedBytes;
    lastBusByteMs = nowMs;
    const auto byteValue = static_cast<std::uint8_t>(nextByte);

    if (busProtocol == BusProtocol::Idle) {
      if ((byteValue == '\n') || (byteValue == '\r')) {
        continue;
      }

      if (isTextProtocolCandidateStart(byteValue)) {
        startTextCandidate();
        continue;
      }

      busProtocol = BusProtocol::Modbus;
    }

    if (busProtocol == BusProtocol::TextCandidate) {
      if (byteValue == 'D') {
        busProtocol = BusProtocol::Text;
        handleTextByte(static_cast<char>(byteValue));
      } else {
        replayTextCandidateAsModbus(byteValue);
        if (!modbusServer.isReceiving()) {
          busProtocol = BusProtocol::Idle;
        }
      }
      continue;
    }

    if (busProtocol == BusProtocol::Text) {
      handleTextByte(static_cast<char>(byteValue));
    } else {
      modbusServer.handleByte(byteValue);
      if (!modbusServer.isReceiving()) {
        busProtocol = BusProtocol::Idle;
      }
    }
  }
}

void pollUsbServicePort() {
  while (Serial.available() > 0) {
    const int nextByte = Serial.read();

    if (nextByte < 0) {
      return;
    }

    const auto nextChar = static_cast<char>(nextByte);

    if (nextChar == '\n') {
      handleCompletedUsbCommand();
    } else if (nextChar == '\r') {
      // Ignore CR. LF completes the command.
    } else if (usbCommandLength < (kCommandBufferSize - 1U)) {
      usbCommandBuffer[usbCommandLength] = nextChar;
      ++usbCommandLength;
    } else {
      usbCommandOverflow = true;
    }
  }
}

bool readSwitch(const std::uint8_t pin) {
  return digitalRead(pin) == LOW;
}

void prepareSafeOutputs() {
  digitalWrite(kStepPin, LOW);
  pinMode(kStepPin, OUTPUT);
  digitalWrite(kStepPin, LOW);
  digitalWrite(kDirPin, LOW);
  pinMode(kDirPin, OUTPUT);
  digitalWrite(kDirPin, LOW);
  digitalWrite(kEnablePin, HIGH);
  pinMode(kEnablePin, OUTPUT);
  digitalWrite(kEnablePin, HIGH);
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
}

BootReason detectBootReason() {
#if defined(DEVICE_RESET_REASON)
  const reset_reason_t reason = mbed::ResetReason::get();
  switch (reason) {
    case RESET_REASON_POWER_ON:
      return BootReason::PowerOn;
    case RESET_REASON_WATCHDOG:
      return BootReason::Watchdog;
    case RESET_REASON_SOFTWARE:
      return BootReason::SoftwareReset;
    case RESET_REASON_PIN_RESET:
    case RESET_REASON_BROWN_OUT:
    case RESET_REASON_LOCKUP:
    case RESET_REASON_WAKE_LOW_POWER:
    case RESET_REASON_ACCESS_ERROR:
    case RESET_REASON_BOOT_ERROR:
    case RESET_REASON_MULTIPLE:
    case RESET_REASON_PLATFORM:
    case RESET_REASON_UNKNOWN:
      break;
  }
#endif

  return BootReason::Unknown;
}

void initializeWatchdog() {
#if LUEFTERKLAPPE_ENABLE_WATCHDOG && defined(DEVICE_WATCHDOG)
  mbed::Watchdog& watchdog = mbed::Watchdog::get_instance();
  if ((!watchdog.is_running()) &&
      (watchdog.get_max_timeout() >= kWatchdogTimeoutMs)) {
    static_cast<void>(watchdog.start(kWatchdogTimeoutMs));
  }
#endif
}

void kickWatchdog() {
#if LUEFTERKLAPPE_ENABLE_WATCHDOG && defined(DEVICE_WATCHDOG)
  mbed::Watchdog& watchdog = mbed::Watchdog::get_instance();
  if (watchdog.is_running()) {
    watchdog.kick();
  }
#endif
}

}  // namespace

void setup() {
  prepareSafeOutputs();
  bootReason = detectBootReason();
  modbusServer.setBootReason(bootReason);
  initializeWatchdog();

  Serial.begin(kUsbDebugBaud);
  Serial1.begin(kModuleBaud);
#if LUEFTERKLAPPE_USE_TMC2209_UART
  tmcSerial.begin(kTmcBaud);
#endif
  stepper.enableOutputs();
  stepper.setMinPulseWidth(kStepPulseWidthUs);

  pinMode(kMinSwitchPin, INPUT_PULLUP);
  pinMode(kMaxSwitchPin, INPUT_PULLUP);

  resetCommandBuffer();
  resetUsbCommandBuffer();
  activeSettings = settingsStore.load();
  settingsPersistenceEnabled = false;
  static_cast<void>(controller.setDeviceId(activeSettings.deviceId));
  static_cast<void>(
      controller.setSafePositionPermille(activeSettings.safePositionPermille));
  static_cast<void>(
      controller.setStallGuardThreshold(activeSettings.stallGuardThreshold));
  settingsPersistenceEnabled = true;
#if LUEFTERKLAPPE_USE_TMC2209_UART
  tmcDriver.initialize();
  tmcDriver.setStallGuardThreshold(activeSettings.stallGuardThreshold);
#endif
  controller.begin();
}

void loop() {
  const std::uint32_t nowMs = millis();

  bool stallGuardActive = false;
#if LUEFTERKLAPPE_USE_TMC2209_UART
  if ((stepper.speed() != 0.0F) &&
      (controller.state() != ControllerState::ServiceMotorTest)) {
    const Tmc2209PollResult pollResult = tmcDriver.pollStallGuardStatus();
    if (pollResult == Tmc2209PollResult::Stalled) {
      stallGuardActive = true;
    } else if ((pollResult == Tmc2209PollResult::CommunicationError) &&
               (LUEFTERKLAPPE_REQUIRE_TMC2209_UART != 0)) {
      controller.reportExternalFault(FaultReason::TmcCommunicationLost);
    }
  }
#endif

  const DigitalInputs rawInputs{readSwitch(kMinSwitchPin),
                                readSwitch(kMaxSwitchPin), stallGuardActive};
  const DigitalInputs inputs = inputDebouncer.tick(rawInputs, nowMs);

  controller.tick(inputs, nowMs);
  updateStatusLed(nowMs);
  pollUsbServicePort();
  pollRs485Bus(nowMs);
  kickWatchdog();
}

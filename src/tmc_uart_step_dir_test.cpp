#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "FirmwarePins.h"
#include "hardware/gpio.h"
#include "pico/bootrom.h"

namespace {

using luefterklappe::firmware::kDirPin;
using luefterklappe::firmware::kEnablePin;
using luefterklappe::firmware::kStepPin;
using luefterklappe::firmware::kTmcUartRxPin;
using luefterklappe::firmware::kTmcUartTxPin;

constexpr std::uint32_t kUsbBaud = 115200UL;
constexpr std::uint32_t kTmcBaud = 115200UL;
constexpr std::uint8_t kTmcSync = 0x05U;
constexpr std::uint8_t kTmcSlave = 0x00U;
constexpr std::uint8_t kTmcMasterReply = 0xFFU;
constexpr std::uint8_t kWriteMask = 0x80U;
constexpr std::size_t kReadLength = 4U;
constexpr std::size_t kFrameLength = 8U;
constexpr std::size_t kCommandSize = 96U;
constexpr std::uint32_t kDriverEnableSettleMs = 50UL;
constexpr unsigned int kDefaultStepPeriodUs = 2000U;
constexpr unsigned int kMinStepPulseUs = 100U;
constexpr std::int32_t kMaxStepTestSteps = 20000L;

constexpr std::uint8_t kRegGconf = 0x00U;
constexpr std::uint8_t kRegGstat = 0x01U;
constexpr std::uint8_t kRegIfcnt = 0x02U;
constexpr std::uint8_t kRegIoin = 0x06U;
constexpr std::uint8_t kRegIholdIrun = 0x10U;
constexpr std::uint8_t kRegTpowerdown = 0x11U;
constexpr std::uint8_t kRegSgthrs = 0x40U;
constexpr std::uint8_t kRegSgResult = 0x41U;
constexpr std::uint8_t kRegChopconf = 0x6CU;
constexpr std::uint8_t kRegPwmconf = 0x70U;

constexpr std::uint32_t kValueGconf = 0x000001C4UL;
constexpr std::uint32_t kValueGstatClear = 0x00000007UL;
constexpr std::uint32_t kValueIholdIrun = 0x00081F10UL;
constexpr std::uint32_t kValueTpowerdown = 0x00000014UL;
constexpr std::uint32_t kValueChopconf = 0x14030053UL;
constexpr std::uint32_t kValuePwmconf = 0xC80D0E24UL;
constexpr std::uint8_t kValueSgthrs = 100U;

arduino::UART tmcSerial(kTmcUartTxPin, kTmcUartRxPin);

char commandBuffer[kCommandSize]{};
std::size_t commandLength = 0U;
bool commandOverflow = false;

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

void printHex32(const std::uint32_t value) {
  Serial.print(F("0x"));
  if (value < 0x10000000UL) {
    Serial.print(F("0"));
  }
  if (value < 0x01000000UL) {
    Serial.print(F("0"));
  }
  if (value < 0x00100000UL) {
    Serial.print(F("0"));
  }
  if (value < 0x00010000UL) {
    Serial.print(F("0"));
  }
  if (value < 0x00001000UL) {
    Serial.print(F("0"));
  }
  if (value < 0x00000100UL) {
    Serial.print(F("0"));
  }
  if (value < 0x00000010UL) {
    Serial.print(F("0"));
  }
  Serial.print(value, HEX);
}

void drainTmcRx() {
  std::uint8_t guard = 0U;
  while ((tmcSerial.available() > 0) && (guard < 64U)) {
    static_cast<void>(tmcSerial.read());
    ++guard;
  }
}

void writeTmcRegister(const std::uint8_t reg, const std::uint32_t value) {
  std::uint8_t frame[kFrameLength]{
      kTmcSync,
      kTmcSlave,
      static_cast<std::uint8_t>(reg | kWriteMask),
      static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(value & 0xFFU),
      0U};
  frame[kFrameLength - 1U] = tmcCrc(frame, kFrameLength - 1U);

  drainTmcRx();
  for (std::uint8_t byte : frame) {
    static_cast<void>(tmcSerial.write(byte));
  }
  tmcSerial.flush();
  delay(2);
}

bool isTmcReply(const std::uint8_t* const frame, const std::uint8_t reg) {
  const bool addressedToMaster =
      (frame[1] == kTmcMasterReply) || (frame[1] == kTmcSlave);
  return (frame[0] == kTmcSync) && addressedToMaster &&
         ((frame[2] & ~kWriteMask) == (reg & ~kWriteMask)) &&
         (frame[kFrameLength - 1U] == tmcCrc(frame, kFrameLength - 1U));
}

bool readTmcRegister(const std::uint8_t reg, std::uint32_t& value) {
  std::uint8_t request[kReadLength]{
      kTmcSync, kTmcSlave, static_cast<std::uint8_t>(reg & ~kWriteMask), 0U};
  std::uint8_t window[kFrameLength]{};
  std::size_t windowLength = 0U;
  std::size_t bytesRead = 0U;

  request[kReadLength - 1U] = tmcCrc(request, kReadLength - 1U);
  drainTmcRx();
  for (std::uint8_t byte : request) {
    static_cast<void>(tmcSerial.write(byte));
  }
  tmcSerial.flush();
  delay(3);

  while ((tmcSerial.available() > 0) && (bytesRead < 32U)) {
    const int next = tmcSerial.read();
    if (next < 0) {
      return false;
    }
    ++bytesRead;

    if (windowLength < kFrameLength) {
      window[windowLength] = static_cast<std::uint8_t>(next);
      ++windowLength;
    } else {
      for (std::size_t index = 1U; index < kFrameLength; ++index) {
        window[index - 1U] = window[index];
      }
      window[kFrameLength - 1U] = static_cast<std::uint8_t>(next);
    }

    if ((windowLength == kFrameLength) && isTmcReply(window, reg)) {
      value = (static_cast<std::uint32_t>(window[3]) << 24U) |
              (static_cast<std::uint32_t>(window[4]) << 16U) |
              (static_cast<std::uint32_t>(window[5]) << 8U) |
              static_cast<std::uint32_t>(window[6]);
      return true;
    }
  }

  return false;
}

void safePins() {
  _gpio_init(kStepPin);
  _gpio_init(kDirPin);
  _gpio_init(kEnablePin);
  gpio_set_dir(kStepPin, GPIO_OUT);
  gpio_set_dir(kDirPin, GPIO_OUT);
  gpio_set_dir(kEnablePin, GPIO_OUT);
  gpio_put(kStepPin, 0);
  gpio_put(kDirPin, 0);
  gpio_put(kEnablePin, 1);
}

void setDriverEnabled(const bool enabled) {
  _gpio_init(kEnablePin);
  gpio_set_dir(kEnablePin, GPIO_OUT);
  gpio_put(kEnablePin, enabled ? 0 : 1);
  delay(kDriverEnableSettleMs);
}

void printTmcRead(const std::uint8_t reg) {
  std::uint32_t value = 0U;
  Serial.print(F("TMCREAD REG="));
  Serial.print(reg);
  if (!readTmcRegister(reg, value)) {
    Serial.println(F(" ERROR"));
    return;
  }
  Serial.print(F(" VALUE="));
  printHex32(value);
  Serial.print(F(" DEC="));
  Serial.println(value);
}

void printIoin() {
  std::uint32_t value = 0U;
  if (!readTmcRegister(kRegIoin, value)) {
    Serial.println(F("IOIN ERROR"));
    return;
  }

  Serial.print(F("IOIN RAW="));
  printHex32(value);
  Serial.print(F(" ENN="));
  Serial.print((value >> 0U) & 0x01UL);
  Serial.print(F(" MS1="));
  Serial.print((value >> 2U) & 0x01UL);
  Serial.print(F(" MS2="));
  Serial.print((value >> 3U) & 0x01UL);
  Serial.print(F(" DIAG="));
  Serial.print((value >> 4U) & 0x01UL);
  Serial.print(F(" PDN_UART="));
  Serial.print((value >> 6U) & 0x01UL);
  Serial.print(F(" STEP="));
  Serial.print((value >> 7U) & 0x01UL);
  Serial.print(F(" SPREAD="));
  Serial.print((value >> 8U) & 0x01UL);
  Serial.print(F(" DIR="));
  Serial.print((value >> 9U) & 0x01UL);
  Serial.print(F(" VERSION="));
  Serial.println((value >> 24U) & 0xFFUL);
}

void printPicoPins() {
  Serial.print(F("PICO STEP_GP"));
  Serial.print(kStepPin);
  Serial.print(F("_PAD="));
  Serial.print(gpio_get(kStepPin));
  Serial.print(F("_OUT="));
  Serial.print(gpio_get_out_level(kStepPin));
  Serial.print(F(" DIR_GP"));
  Serial.print(kDirPin);
  Serial.print(F("_PAD="));
  Serial.print(gpio_get(kDirPin));
  Serial.print(F("_OUT="));
  Serial.print(gpio_get_out_level(kDirPin));
  Serial.print(F(" EN_GP"));
  Serial.print(kEnablePin);
  Serial.print(F("_PAD="));
  Serial.print(gpio_get(kEnablePin));
  Serial.print(F("_OUT="));
  Serial.println(gpio_get_out_level(kEnablePin));
}

void printPinSnapshot() {
  printPicoPins();
  printIoin();
}

void printIfcnt() { printTmcRead(kRegIfcnt); }

void tmcInit() {
  const std::uint32_t before = [] {
    std::uint32_t value = 0U;
    return readTmcRegister(kRegIfcnt, value) ? value : 0xFFFFFFFFUL;
  }();

  writeTmcRegister(kRegGconf, kValueGconf);
  delay(10);
  writeTmcRegister(kRegIholdIrun, kValueIholdIrun);
  writeTmcRegister(kRegTpowerdown, kValueTpowerdown);
  writeTmcRegister(kRegChopconf, kValueChopconf);
  writeTmcRegister(kRegPwmconf, kValuePwmconf);
  writeTmcRegister(kRegSgthrs, kValueSgthrs);
  writeTmcRegister(kRegGstat, kValueGstatClear);

  std::uint32_t after = 0U;
  Serial.print(F("TMCINIT IFCNT_BEFORE="));
  if (before == 0xFFFFFFFFUL) {
    Serial.print(F("ERR"));
  } else {
    Serial.print(before);
  }
  Serial.print(F(" IFCNT_AFTER="));
  if (readTmcRegister(kRegIfcnt, after)) {
    Serial.println(after);
  } else {
    Serial.println(F("ERR"));
  }
  printTmcRead(kRegGconf);
  printTmcRead(kRegChopconf);
  printTmcRead(kRegPwmconf);
  printIoin();
}

bool parseLong(const char* const token, long& value) {
  if (token == nullptr) {
    return false;
  }
  char* end = nullptr;
  value = std::strtol(token, &end, 0);
  return (end != token) && (end != nullptr) && (*end == '\0');
}

bool parseU32(const char* const token, std::uint32_t& value) {
  if (token == nullptr) {
    return false;
  }
  char* end = nullptr;
  value = std::strtoul(token, &end, 0);
  return (end != token) && (end != nullptr) && (*end == '\0');
}

unsigned int normalizedPeriodUs(const long period) {
  if ((period < static_cast<long>(2U * kMinStepPulseUs)) ||
      (period > static_cast<long>(60000000UL))) {
    return kDefaultStepPeriodUs;
  }

  return static_cast<unsigned int>(period);
}

bool isTestGpio(const long pin) {
  return (pin >= 0L) && (pin <= 28L) &&
         (pin != static_cast<long>(kTmcUartTxPin)) &&
         (pin != static_cast<long>(kTmcUartRxPin));
}

void printPinVerify(const std::uint8_t pin, const bool expectedHigh) {
  const bool padHigh = gpio_get(pin);
  const bool outHigh = gpio_get_out_level(pin);
  Serial.print(F("PINVERIFY GP"));
  Serial.print(pin);
  Serial.print(F(" EXPECT="));
  Serial.print(expectedHigh ? 1 : 0);
  Serial.print(F(" OUT="));
  Serial.print(outHigh ? 1 : 0);
  Serial.print(F(" PAD="));
  Serial.print(padHigh ? 1 : 0);
  Serial.print(F(" "));
  Serial.println((outHigh == expectedHigh) && (padHigh == expectedHigh)
                     ? F("OK")
                     : F("FAIL"));
}

void setPinLevel(const std::uint8_t pin, const bool high) {
  _gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, high ? 1 : 0);
  delayMicroseconds(10);
  Serial.print(F("PIN GP"));
  Serial.print(pin);
  Serial.print(F("="));
  Serial.println(high ? F("HIGH") : F("LOW"));
  printPinVerify(pin, high);
  printPinSnapshot();
}

void runStepsOnPins(const std::uint8_t stepPin, const std::uint8_t dirPin,
                    const long requestedSteps, unsigned int periodUs,
                    const __FlashStringHelper* const label) {
  if ((requestedSteps == 0L) || (requestedSteps < -kMaxStepTestSteps) ||
      (requestedSteps > kMaxStepTestSteps)) {
    Serial.println(F("STEP ERROR: range -20000..20000 ohne 0"));
    return;
  }

  if (periodUs < (2U * kMinStepPulseUs)) {
    periodUs = kDefaultStepPeriodUs;
  }

  const long steps = requestedSteps < 0L ? -requestedSteps : requestedSteps;
  const bool directionHigh = requestedSteps >= 0L;
  unsigned int highUs = periodUs / 2U;
  if (highUs < kMinStepPulseUs) {
    highUs = kMinStepPulseUs;
  }
  if (highUs >= periodUs) {
    highUs = periodUs / 2U;
  }
  const unsigned int lowUs = periodUs - highUs;

  _gpio_init(stepPin);
  _gpio_init(dirPin);
  _gpio_init(kEnablePin);
  gpio_set_dir(stepPin, GPIO_OUT);
  gpio_set_dir(dirPin, GPIO_OUT);
  gpio_set_dir(kEnablePin, GPIO_OUT);
  gpio_put(stepPin, 0);
  gpio_put(dirPin, directionHigh ? 1 : 0);
  setDriverEnabled(true);

  Serial.print(label);
  Serial.print(F(" START STEP=GP"));
  Serial.print(stepPin);
  Serial.print(F(" DIR=GP"));
  Serial.print(dirPin);
  Serial.print(F(" STEPS="));
  Serial.print(requestedSteps);
  Serial.print(F(" PERIOD_US="));
  Serial.println(periodUs);
  Serial.print(F("STEP_TIMING HIGH_US="));
  Serial.print(highUs);
  Serial.print(F(" LOW_US="));
  Serial.println(lowUs);
  printPinSnapshot();

  for (long index = 0; index < steps; ++index) {
    gpio_put(stepPin, 1);
    delayMicroseconds(highUs);
    gpio_put(stepPin, 0);
    delayMicroseconds(lowUs);
  }

  Serial.print(label);
  Serial.println(F(" DONE"));
  printPinSnapshot();
}

void holdStepLevel(const bool high, std::uint32_t holdMs) {
  if (holdMs == 0UL) {
    holdMs = 1000UL;
  }

  _gpio_init(kStepPin);
  gpio_set_dir(kStepPin, GPIO_OUT);
  gpio_put(kStepPin, high ? 1 : 0);
  delayMicroseconds(10);
  Serial.print(F("STEPHOLD START LEVEL="));
  Serial.print(high ? 1 : 0);
  Serial.print(F(" MS="));
  Serial.println(holdMs);
  printPinSnapshot();
  delay(holdMs);
  Serial.println(F("STEPHOLD DONE"));
  printPinSnapshot();
}

void runStepClockOnPins(const std::uint8_t stepPin, const std::uint8_t dirPin,
                        std::uint32_t durationMs, unsigned int periodUs,
                        const __FlashStringHelper* const label) {
  if (durationMs == 0UL) {
    durationMs = 3000UL;
  }
  if (periodUs < (2U * kMinStepPulseUs)) {
    periodUs = kDefaultStepPeriodUs;
  }

  const unsigned int highUs = periodUs / 2U;
  const unsigned int lowUs = periodUs - highUs;
  std::uint32_t pulses = 0UL;
  const std::uint32_t startMs = millis();

  _gpio_init(stepPin);
  _gpio_init(dirPin);
  _gpio_init(kEnablePin);
  gpio_set_dir(stepPin, GPIO_OUT);
  gpio_set_dir(dirPin, GPIO_OUT);
  gpio_set_dir(kEnablePin, GPIO_OUT);
  gpio_put(stepPin, 0);
  gpio_put(dirPin, 1);
  setDriverEnabled(true);

  Serial.print(label);
  Serial.print(F(" START STEP=GP"));
  Serial.print(stepPin);
  Serial.print(F(" DIR=GP"));
  Serial.print(dirPin);
  Serial.print(F(" MS="));
  Serial.print(durationMs);
  Serial.print(F(" PERIOD_US="));
  Serial.println(periodUs);
  printPinSnapshot();

  while ((millis() - startMs) < durationMs) {
    gpio_put(stepPin, 1);
    delayMicroseconds(highUs);
    gpio_put(stepPin, 0);
    delayMicroseconds(lowUs);
    ++pulses;
  }

  Serial.print(label);
  Serial.print(F(" DONE PULSES="));
  Serial.println(pulses);
  printPinSnapshot();
}

void runStepClock(std::uint32_t durationMs, const unsigned int periodUs) {
  runStepClockOnPins(kStepPin, kDirPin, durationMs, periodUs, F("CLOCK"));
}

void printPins() {
  Serial.print(F("PINS STEP=GP"));
  Serial.print(kStepPin);
  Serial.print(F(" DIR=GP"));
  Serial.print(kDirPin);
  Serial.print(F(" EN=GP"));
  Serial.print(kEnablePin);
  Serial.print(F(" TMC_TX=GP"));
  Serial.print(kTmcUartTxPin);
  Serial.print(F(" TMC_RX=GP"));
  Serial.println(kTmcUartRxPin);
}

void printHelp() {
  Serial.println(F("TMC UART STEP/DIR/EN test firmware"));
  Serial.println(F("HELP | PINS? | IOIN? | IFCNT? | GSTAT? | DRV?"));
  Serial.println(F("TMCINIT | TMCREAD <reg> | TMCWRITE <reg> <value>"));
  Serial.println(F("EN <0|1> | DIR <0|1> | STEPLEVEL <0|1> | PIN <gp> <0|1>"));
  Serial.println(F("PINREAD? | STEPHOLD <0|1> [ms] | CLOCK [ms] [period_us]"));
  Serial.println(F("STEP <steps> [period_us] | STEP2 <steps> [period_us]"));
  Serial.println(F("STEPON <step_gp> <dir_gp> <steps> [period_us]"));
  Serial.println(F("CLOCKON <step_gp> <dir_gp> [ms] [period_us]"));
  Serial.println(F("SWEEP [steps] [period_us] | SWEEP2 [steps] [period_us]"));
  Serial.println(F("PINSAFE | BOOTSEL"));
}

void handleCommand(char* const line) {
  char* const command = std::strtok(line, " \t");
  if (command == nullptr) {
    return;
  }

  if ((std::strcmp(command, "HELP") == 0) || (std::strcmp(command, "?") == 0)) {
    printHelp();
  } else if (std::strcmp(command, "PINS?") == 0) {
    printPins();
  } else if (std::strcmp(command, "IOIN?") == 0) {
    printIoin();
  } else if (std::strcmp(command, "PINREAD?") == 0) {
    printPinSnapshot();
  } else if (std::strcmp(command, "IFCNT?") == 0) {
    printIfcnt();
  } else if (std::strcmp(command, "GSTAT?") == 0) {
    printTmcRead(kRegGstat);
  } else if (std::strcmp(command, "DRV?") == 0) {
    printTmcRead(kRegSgResult);
  } else if (std::strcmp(command, "TMCINIT") == 0) {
    tmcInit();
  } else if (std::strcmp(command, "TMCREAD") == 0) {
    long reg = 0L;
    if (!parseLong(std::strtok(nullptr, " \t"), reg) || (reg < 0L) ||
        (reg > 127L)) {
      Serial.println(F("TMCREAD ERROR"));
      return;
    }
    printTmcRead(static_cast<std::uint8_t>(reg));
  } else if (std::strcmp(command, "TMCWRITE") == 0) {
    long reg = 0L;
    std::uint32_t value = 0U;
    if (!parseLong(std::strtok(nullptr, " \t"), reg) || (reg < 0L) ||
        (reg > 127L) || !parseU32(std::strtok(nullptr, " \t"), value)) {
      Serial.println(F("TMCWRITE ERROR"));
      return;
    }
    writeTmcRegister(static_cast<std::uint8_t>(reg), value);
    Serial.println(F("TMCWRITE OK"));
    printIfcnt();
  } else if (std::strcmp(command, "EN") == 0) {
    long enabled = 0L;
    if (!parseLong(std::strtok(nullptr, " \t"), enabled)) {
      Serial.println(F("EN ERROR"));
      return;
    }
    setDriverEnabled(enabled != 0L);
    Serial.println(enabled != 0L ? F("ENABLED") : F("DISABLED"));
    printIoin();
  } else if (std::strcmp(command, "DIR") == 0) {
    long high = 0L;
    if (!parseLong(std::strtok(nullptr, " \t"), high)) {
      Serial.println(F("DIR ERROR"));
      return;
    }
    setPinLevel(kDirPin, high != 0L);
  } else if (std::strcmp(command, "STEPLEVEL") == 0) {
    long high = 0L;
    if (!parseLong(std::strtok(nullptr, " \t"), high)) {
      Serial.println(F("STEPLEVEL ERROR"));
      return;
    }
    setPinLevel(kStepPin, high != 0L);
  } else if (std::strcmp(command, "STEPHOLD") == 0) {
    long high = 0L;
    long holdMs = 1000L;
    if (!parseLong(std::strtok(nullptr, " \t"), high)) {
      Serial.println(F("STEPHOLD ERROR"));
      return;
    }
    char* const holdToken = std::strtok(nullptr, " \t");
    if (holdToken != nullptr) {
      static_cast<void>(parseLong(holdToken, holdMs));
    }
    if (holdMs < 0L) {
      holdMs = 0L;
    }
    holdStepLevel(high != 0L, static_cast<std::uint32_t>(holdMs));
  } else if (std::strcmp(command, "CLOCK") == 0) {
    long durationMs = 3000L;
    long period = static_cast<long>(kDefaultStepPeriodUs);
    char* const durationToken = std::strtok(nullptr, " \t");
    if (durationToken != nullptr) {
      static_cast<void>(parseLong(durationToken, durationMs));
    }
    char* const periodToken = std::strtok(nullptr, " \t");
    if (periodToken != nullptr) {
      static_cast<void>(parseLong(periodToken, period));
    }
    if (durationMs < 0L) {
      durationMs = 0L;
    }
    runStepClock(static_cast<std::uint32_t>(durationMs),
                 normalizedPeriodUs(period));
  } else if (std::strcmp(command, "PIN") == 0) {
    long pin = 0L;
    long high = 0L;
    if (!parseLong(std::strtok(nullptr, " \t"), pin) ||
        !parseLong(std::strtok(nullptr, " \t"), high) ||
        !isTestGpio(pin)) {
      Serial.println(F("PIN ERROR"));
      return;
    }
    setPinLevel(static_cast<std::uint8_t>(pin), high != 0L);
  } else if (std::strcmp(command, "STEPON") == 0) {
    long stepPin = 0L;
    long dirPin = 0L;
    long steps = 0L;
    long period = static_cast<long>(kDefaultStepPeriodUs);
    if (!parseLong(std::strtok(nullptr, " \t"), stepPin) ||
        !parseLong(std::strtok(nullptr, " \t"), dirPin) ||
        !parseLong(std::strtok(nullptr, " \t"), steps) ||
        !isTestGpio(stepPin) || !isTestGpio(dirPin) || (stepPin == dirPin)) {
      Serial.println(F("STEPON ERROR"));
      return;
    }
    char* const periodToken = std::strtok(nullptr, " \t");
    if (periodToken != nullptr) {
      static_cast<void>(parseLong(periodToken, period));
    }
    runStepsOnPins(static_cast<std::uint8_t>(stepPin),
                   static_cast<std::uint8_t>(dirPin), steps,
                   normalizedPeriodUs(period), F("STEPON"));
  } else if (std::strcmp(command, "CLOCKON") == 0) {
    long stepPin = 0L;
    long dirPin = 0L;
    long durationMs = 3000L;
    long period = static_cast<long>(kDefaultStepPeriodUs);
    if (!parseLong(std::strtok(nullptr, " \t"), stepPin) ||
        !parseLong(std::strtok(nullptr, " \t"), dirPin) ||
        !isTestGpio(stepPin) || !isTestGpio(dirPin) || (stepPin == dirPin)) {
      Serial.println(F("CLOCKON ERROR"));
      return;
    }
    char* const durationToken = std::strtok(nullptr, " \t");
    if (durationToken != nullptr) {
      static_cast<void>(parseLong(durationToken, durationMs));
    }
    char* const periodToken = std::strtok(nullptr, " \t");
    if (periodToken != nullptr) {
      static_cast<void>(parseLong(periodToken, period));
    }
    if (durationMs < 0L) {
      durationMs = 0L;
    }
    runStepClockOnPins(static_cast<std::uint8_t>(stepPin),
                       static_cast<std::uint8_t>(dirPin),
                       static_cast<std::uint32_t>(durationMs),
                       normalizedPeriodUs(period), F("CLOCKON"));
  } else if ((std::strcmp(command, "STEP") == 0) ||
             (std::strcmp(command, "STEP2") == 0)) {
    long steps = 0L;
    long period = static_cast<long>(kDefaultStepPeriodUs);
    if (!parseLong(std::strtok(nullptr, " \t"), steps)) {
      Serial.println(F("STEP ERROR"));
      return;
    }
    char* const periodToken = std::strtok(nullptr, " \t");
    if (periodToken != nullptr) {
      static_cast<void>(parseLong(periodToken, period));
    }
    const bool swapped = std::strcmp(command, "STEP2") == 0;
    runStepsOnPins(swapped ? kDirPin : kStepPin, swapped ? kStepPin : kDirPin,
                   steps, normalizedPeriodUs(period),
                   swapped ? F("STEP2") : F("STEP"));
  } else if ((std::strcmp(command, "SWEEP") == 0) ||
             (std::strcmp(command, "SWEEP2") == 0)) {
    long steps = 800L;
    long period = static_cast<long>(kDefaultStepPeriodUs);
    char* const stepsToken = std::strtok(nullptr, " \t");
    if (stepsToken != nullptr) {
      static_cast<void>(parseLong(stepsToken, steps));
    }
    char* const periodToken = std::strtok(nullptr, " \t");
    if (periodToken != nullptr) {
      static_cast<void>(parseLong(periodToken, period));
    }
    const bool swapped = std::strcmp(command, "SWEEP2") == 0;
    const std::uint8_t stepPin = swapped ? kDirPin : kStepPin;
    const std::uint8_t dirPin = swapped ? kStepPin : kDirPin;
    runStepsOnPins(stepPin, dirPin, steps, normalizedPeriodUs(period),
                   swapped ? F("SWEEP2") : F("SWEEP"));
    delay(250);
    runStepsOnPins(stepPin, dirPin, -steps, normalizedPeriodUs(period),
                   swapped ? F("SWEEP2") : F("SWEEP"));
  } else if (std::strcmp(command, "PINSAFE") == 0) {
    safePins();
    Serial.println(F("PINSAFE OK"));
    printIoin();
  } else if (std::strcmp(command, "BOOTSEL") == 0) {
    Serial.println(F("BOOTSEL"));
    Serial.flush();
    delay(50);
    reset_usb_boot(1U << digitalPinToPinName(LED_BUILTIN), 0U);
  } else {
    Serial.println(F("UNKNOWN"));
  }
}

void processUsb() {
  while (Serial.available() > 0) {
    const int next = Serial.read();
    if (next < 0) {
      continue;
    }

    const char nextChar = static_cast<char>(next);
    if (nextChar == '\n') {
      if (!commandOverflow) {
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
      } else {
        Serial.println(F("COMMAND OVERFLOW"));
      }
      commandLength = 0U;
      commandOverflow = false;
    } else if (nextChar != '\r') {
      if (commandLength < (kCommandSize - 1U)) {
        commandBuffer[commandLength] = nextChar;
        ++commandLength;
      } else {
        commandOverflow = true;
      }
    }
  }
}

}  // namespace

void setup() {
  safePins();
  Serial.begin(kUsbBaud);
  tmcSerial.begin(kTmcBaud);

  const std::uint32_t startMs = millis();
  while (!Serial && ((millis() - startMs) < 2500UL)) {
    delay(10);
  }

  Serial.println();
  printHelp();
  printPins();
  tmcInit();
}

void loop() { processUsb(); }

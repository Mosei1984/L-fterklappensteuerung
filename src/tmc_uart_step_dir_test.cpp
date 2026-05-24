#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "FirmwarePins.h"
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
constexpr unsigned int kStepHighUs = 50U;
constexpr unsigned int kDefaultStepPeriodUs = 2000U;
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
  pinMode(kStepPin, OUTPUT);
  pinMode(kDirPin, OUTPUT);
  pinMode(kEnablePin, OUTPUT);
  digitalWrite(kStepPin, LOW);
  digitalWrite(kDirPin, LOW);
  digitalWrite(kEnablePin, HIGH);
}

void setDriverEnabled(const bool enabled) {
  digitalWrite(kEnablePin, enabled ? LOW : HIGH);
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

void setPinLevel(const std::uint8_t pin, const bool high) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, high ? HIGH : LOW);
  Serial.print(F("PIN GP"));
  Serial.print(pin);
  Serial.print(F("="));
  Serial.println(high ? F("HIGH") : F("LOW"));
  printIoin();
}

void runStepsOnPins(const std::uint8_t stepPin, const std::uint8_t dirPin,
                    const long requestedSteps, unsigned int periodUs,
                    const __FlashStringHelper* const label) {
  if ((requestedSteps == 0L) || (requestedSteps < -kMaxStepTestSteps) ||
      (requestedSteps > kMaxStepTestSteps)) {
    Serial.println(F("STEP ERROR: range -20000..20000 ohne 0"));
    return;
  }

  if (periodUs < (kStepHighUs + 10U)) {
    periodUs = kDefaultStepPeriodUs;
  }

  const long steps = requestedSteps < 0L ? -requestedSteps : requestedSteps;
  const bool directionHigh = requestedSteps >= 0L;
  const unsigned int lowUs = periodUs - kStepHighUs;

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(kEnablePin, OUTPUT);
  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin, directionHigh ? HIGH : LOW);
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
  printIoin();

  for (long index = 0; index < steps; ++index) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(kStepHighUs);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(lowUs);
  }

  Serial.print(label);
  Serial.println(F(" DONE"));
  printIoin();
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
  Serial.println(F("STEP <steps> [period_us] | STEP2 <steps> [period_us]"));
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
  } else if (std::strcmp(command, "PIN") == 0) {
    long pin = 0L;
    long high = 0L;
    if (!parseLong(std::strtok(nullptr, " \t"), pin) ||
        !parseLong(std::strtok(nullptr, " \t"), high) ||
        ((pin != 2L) && (pin != 3L) && (pin != 7L))) {
      Serial.println(F("PIN ERROR"));
      return;
    }
    setPinLevel(static_cast<std::uint8_t>(pin), high != 0L);
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
                   steps, static_cast<unsigned int>(period),
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
    runStepsOnPins(stepPin, dirPin, steps, static_cast<unsigned int>(period),
                   swapped ? F("SWEEP2") : F("SWEEP"));
    delay(250);
    runStepsOnPins(stepPin, dirPin, -steps, static_cast<unsigned int>(period),
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


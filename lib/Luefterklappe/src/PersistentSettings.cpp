#include "PersistentSettings.h"

namespace luefterklappe {
namespace {

constexpr std::uint8_t kMagic0 = 'L';
constexpr std::uint8_t kMagic1 = 'K';
constexpr std::uint8_t kLegacyVersion = 2U;
constexpr std::uint8_t kStallGuardVersion = 3U;
constexpr std::uint8_t kHomingVersion = 4U;
constexpr std::uint8_t kVersion = 5U;
constexpr std::size_t kMagic0Offset = 0U;
constexpr std::size_t kMagic1Offset = 1U;
constexpr std::size_t kVersionOffset = 2U;
constexpr std::size_t kDeviceIdOffset = 3U;
constexpr std::size_t kSafePositionOffset = 4U;
constexpr std::size_t kLegacyGenerationOffset = 6U;
constexpr std::size_t kStallGuardThresholdOffset = 6U;
constexpr std::size_t kGenerationOffset = 7U;
constexpr std::size_t kHomeMinSwitchOffset = 11U;
constexpr std::size_t kHomeMaxSwitchOffset = 12U;
constexpr std::size_t kHomeMinDirectionOffset = 13U;
constexpr std::size_t kHomeMaxDirectionOffset = 14U;
constexpr std::size_t kStepperDirectionInvertedOffset = 15U;
constexpr std::size_t kNormalMaxSpeedOffset = 16U;
constexpr std::size_t kHomingMaxSpeedOffset = 18U;
constexpr std::size_t kRunCurrentMilliampsOffset = 20U;
constexpr std::size_t kLegacyCrcOffset = 14U;
constexpr std::size_t kLegacyCrcLength = 14U;
constexpr std::size_t kCrcOffset = 22U;
constexpr std::size_t kCrcLength = 22U;
constexpr PersistentSettings kDefaultSettings{1U, 1000U, 100U,
                                              kDefaultHomingConfig,
                                              kDefaultMotorConfig};

bool settingsEqual(const PersistentSettings& first,
                   const PersistentSettings& second) {
  return (first.deviceId == second.deviceId) &&
         (first.safePositionPermille == second.safePositionPermille) &&
         (first.stallGuardThreshold == second.stallGuardThreshold) &&
         (first.homing.minSwitch == second.homing.minSwitch) &&
         (first.homing.maxSwitch == second.homing.maxSwitch) &&
         (first.homing.minDirection == second.homing.minDirection) &&
         (first.homing.maxDirection == second.homing.maxDirection) &&
         (first.homing.stepperDirectionInverted ==
          second.homing.stepperDirectionInverted) &&
         (first.motor.normalMaxSpeedStepsPerSecond ==
          second.motor.normalMaxSpeedStepsPerSecond) &&
         (first.motor.homingMaxSpeedStepsPerSecond ==
          second.motor.homingMaxSpeedStepsPerSecond) &&
         (first.motor.runCurrentMilliamps == second.motor.runCurrentMilliamps);
}

}  // namespace

PersistentSettingsStore::PersistentSettingsStore(SettingsStoragePort& storage)
    : PersistentSettingsStore(storage, kDefaultSettings) {}

PersistentSettingsStore::PersistentSettingsStore(
    SettingsStoragePort& storage, const PersistentSettings defaults)
    : storage_(storage),
      defaults_(isValid(defaults) ? defaults : kDefaultSettings) {}

PersistentSettings PersistentSettingsStore::load() {
  if (!storageUsable()) {
    return defaults_;
  }

  const DecodedRecord first = readRecord(0U);
  const DecodedRecord second = readRecord(1U);

  if (first.valid && second.valid) {
    return generationIsNewer(second.generation, first.generation)
               ? second.settings
               : first.settings;
  }

  if (first.valid) {
    return first.settings;
  }

  if (second.valid) {
    return second.settings;
  }

  return defaults_;
}

bool PersistentSettingsStore::save(const PersistentSettings settings) {
  if (!isValid(settings) || !storageUsable()) {
    return false;
  }

  const DecodedRecord first = readRecord(0U);
  const DecodedRecord second = readRecord(1U);
  DecodedRecord newest{false, 0U, 0U, defaults_};

  if (first.valid) {
    newest = first;
  }

  if (second.valid &&
      ((!newest.valid) ||
       generationIsNewer(second.generation, newest.generation))) {
    newest = second;
  }

  const std::size_t targetSlot =
      newest.valid ? ((newest.slot + 1U) % kJournalSlotCount) : 0U;
  const std::uint32_t nextGeneration =
      newest.valid ? static_cast<std::uint32_t>(newest.generation + 1U) : 1U;

  if (!writeRecord(targetSlot, nextGeneration, settings)) {
    return false;
  }

  const DecodedRecord written = readRecord(targetSlot);
  return written.valid && (written.generation == nextGeneration) &&
         settingsEqual(written.settings, settings);
}

bool PersistentSettingsStore::isValid(const PersistentSettings settings) {
  return (settings.deviceId >= 1U) && (settings.deviceId <= 247U) &&
         (settings.safePositionPermille <= 1000U) &&
         homingConfigValuesAreValid(settings.homing) &&
         motorConfigValuesAreValid(settings.motor);
}

bool PersistentSettingsStore::storageUsable() const {
  return (storage_.slotCount() >= kJournalSlotCount) &&
         (storage_.slotSize() >= kStorageSize);
}

PersistentSettingsStore::DecodedRecord PersistentSettingsStore::readRecord(
    const std::size_t slot) {
  DecodedRecord record{false, slot, 0U, defaults_};
  std::uint8_t buffer[kStorageSize]{};

  if ((slot >= kJournalSlotCount) ||
      !storage_.readSlot(slot, buffer, kStorageSize)) {
    return record;
  }

  static_cast<void>(decodeRecord(buffer, slot, record));
  return record;
}

bool PersistentSettingsStore::writeRecord(const std::size_t slot,
                                          const std::uint32_t generation,
                                          const PersistentSettings settings) {
  std::uint8_t buffer[kStorageSize]{};
  encodeRecord(buffer, generation, settings);
  return storage_.writeSlot(slot, buffer, kStorageSize);
}

bool PersistentSettingsStore::decodeRecord(
    const std::uint8_t* const data, const std::size_t slot,
    DecodedRecord& record) const {
  if ((data[kMagic0Offset] != kMagic0) || (data[kMagic1Offset] != kMagic1) ||
      ((data[kVersionOffset] != kVersion) &&
       (data[kVersionOffset] != kHomingVersion) &&
       (data[kVersionOffset] != kStallGuardVersion) &&
       (data[kVersionOffset] != kLegacyVersion))) {
    return false;
  }

  const bool legacyRecord = data[kVersionOffset] == kLegacyVersion;
  const bool stallGuardRecord = data[kVersionOffset] == kStallGuardVersion;
  const bool homingRecord = data[kVersionOffset] == kHomingVersion;
  const std::size_t crcLength =
      (legacyRecord || stallGuardRecord) ? kLegacyCrcLength : kCrcLength;
  const std::size_t crcOffset =
      (legacyRecord || stallGuardRecord) ? kLegacyCrcOffset : kCrcOffset;
  const std::uint16_t expectedCrc = crc16(data, crcLength);
  const std::uint16_t storedCrc = readUint16(data, crcOffset);
  if (expectedCrc != storedCrc) {
    return false;
  }

  const PersistentSettings settings{
      data[kDeviceIdOffset],
      readUint16(data, kSafePositionOffset),
      legacyRecord ? defaults_.stallGuardThreshold
                   : data[kStallGuardThresholdOffset],
      (legacyRecord || stallGuardRecord)
          ? defaults_.homing
          : HomingConfig{
                data[kHomeMinSwitchOffset] == 0U ? HomingSwitch::MinInput
                                                 : HomingSwitch::MaxInput,
                data[kHomeMaxSwitchOffset] == 0U ? HomingSwitch::MinInput
                                                 : HomingSwitch::MaxInput,
                data[kHomeMinDirectionOffset] == 0U
                    ? HomingDirection::Negative
                    : HomingDirection::Positive,
                data[kHomeMaxDirectionOffset] == 0U
                    ? HomingDirection::Negative
                    : HomingDirection::Positive,
                data[kStepperDirectionInvertedOffset] != 0U},
      (legacyRecord || stallGuardRecord || homingRecord)
          ? defaults_.motor
          : MotorConfig{readUint16(data, kNormalMaxSpeedOffset),
                        readUint16(data, kHomingMaxSpeedOffset),
                        readUint16(data, kRunCurrentMilliampsOffset)}};
  if ((!legacyRecord) && (!stallGuardRecord) &&
      ((data[kHomeMinSwitchOffset] > 1U) ||
       (data[kHomeMaxSwitchOffset] > 1U) ||
       (data[kHomeMinDirectionOffset] > 1U) ||
       (data[kHomeMaxDirectionOffset] > 1U) ||
       (data[kStepperDirectionInvertedOffset] > 1U))) {
    return false;
  }

  if (!isValid(settings)) {
    return false;
  }

  record.valid = true;
  record.slot = slot;
  record.generation = readUint32(
      data, legacyRecord ? kLegacyGenerationOffset : kGenerationOffset);
  record.settings = settings;
  return true;
}

void PersistentSettingsStore::encodeRecord(
    std::uint8_t* const data, const std::uint32_t generation,
    const PersistentSettings settings) const {
  for (std::size_t index = 0U; index < kStorageSize; ++index) {
    data[index] = 0U;
  }

  data[kMagic0Offset] = kMagic0;
  data[kMagic1Offset] = kMagic1;
  data[kVersionOffset] = kVersion;
  data[kDeviceIdOffset] = settings.deviceId;
  writeUint16(data, kSafePositionOffset, settings.safePositionPermille);
  data[kStallGuardThresholdOffset] = settings.stallGuardThreshold;
  writeUint32(data, kGenerationOffset, generation);
  data[kHomeMinSwitchOffset] = static_cast<std::uint8_t>(settings.homing.minSwitch);
  data[kHomeMaxSwitchOffset] = static_cast<std::uint8_t>(settings.homing.maxSwitch);
  data[kHomeMinDirectionOffset] =
      static_cast<std::uint8_t>(settings.homing.minDirection);
  data[kHomeMaxDirectionOffset] =
      static_cast<std::uint8_t>(settings.homing.maxDirection);
  data[kStepperDirectionInvertedOffset] =
      settings.homing.stepperDirectionInverted ? 1U : 0U;
  writeUint16(data, kNormalMaxSpeedOffset,
              settings.motor.normalMaxSpeedStepsPerSecond);
  writeUint16(data, kHomingMaxSpeedOffset,
              settings.motor.homingMaxSpeedStepsPerSecond);
  writeUint16(data, kRunCurrentMilliampsOffset,
              settings.motor.runCurrentMilliamps);
  writeUint16(data, kCrcOffset, crc16(data, kCrcLength));
}

bool PersistentSettingsStore::generationIsNewer(
    const std::uint32_t candidate, const std::uint32_t current) {
  return (candidate != current) &&
         (static_cast<std::uint32_t>(candidate - current) < 0x80000000UL);
}

std::uint16_t PersistentSettingsStore::crc16(
    const std::uint8_t* const data, const std::size_t lengthWithoutCrc) {
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

std::uint16_t PersistentSettingsStore::readUint16(
    const std::uint8_t* const data, const std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(data[offset]) << 8U) |
      static_cast<std::uint16_t>(data[offset + 1U]));
}

void PersistentSettingsStore::writeUint16(std::uint8_t* const data,
                                          const std::size_t offset,
                                          const std::uint16_t value) {
  data[offset] = static_cast<std::uint8_t>((value >> 8U) & 0x00FFU);
  data[offset + 1U] = static_cast<std::uint8_t>(value & 0x00FFU);
}

std::uint32_t PersistentSettingsStore::readUint32(
    const std::uint8_t* const data, const std::size_t offset) {
  return (static_cast<std::uint32_t>(data[offset]) << 24U) |
         (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(data[offset + 3U]);
}

void PersistentSettingsStore::writeUint32(std::uint8_t* const data,
                                          const std::size_t offset,
                                          const std::uint32_t value) {
  data[offset] = static_cast<std::uint8_t>((value >> 24U) & 0x000000FFUL);
  data[offset + 1U] =
      static_cast<std::uint8_t>((value >> 16U) & 0x000000FFUL);
  data[offset + 2U] =
      static_cast<std::uint8_t>((value >> 8U) & 0x000000FFUL);
  data[offset + 3U] = static_cast<std::uint8_t>(value & 0x000000FFUL);
}

}  // namespace luefterklappe

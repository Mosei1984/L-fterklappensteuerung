#include "PersistentSettings.h"

namespace luefterklappe {
namespace {

constexpr std::uint8_t kMagic0 = 'L';
constexpr std::uint8_t kMagic1 = 'K';
constexpr std::uint8_t kVersion = 2U;
constexpr std::size_t kMagic0Offset = 0U;
constexpr std::size_t kMagic1Offset = 1U;
constexpr std::size_t kVersionOffset = 2U;
constexpr std::size_t kDeviceIdOffset = 3U;
constexpr std::size_t kSafePositionOffset = 4U;
constexpr std::size_t kGenerationOffset = 6U;
constexpr std::size_t kReservedOffset = 10U;
constexpr std::size_t kCrcOffset = 14U;
constexpr std::size_t kCrcLength = 14U;
constexpr PersistentSettings kDefaultSettings{1U, 1000U};

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
         (written.settings.deviceId == settings.deviceId) &&
         (written.settings.safePositionPermille ==
          settings.safePositionPermille);
}

bool PersistentSettingsStore::isValid(const PersistentSettings settings) {
  return (settings.deviceId >= 1U) && (settings.deviceId <= 247U) &&
         (settings.safePositionPermille <= 1000U);
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
      (data[kVersionOffset] != kVersion)) {
    return false;
  }

  const std::uint16_t expectedCrc = crc16(data, kCrcLength);
  const std::uint16_t storedCrc = readUint16(data, kCrcOffset);
  if (expectedCrc != storedCrc) {
    return false;
  }

  const PersistentSettings settings{
      data[kDeviceIdOffset], readUint16(data, kSafePositionOffset)};
  if (!isValid(settings)) {
    return false;
  }

  record.valid = true;
  record.slot = slot;
  record.generation = readUint32(data, kGenerationOffset);
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
  writeUint32(data, kGenerationOffset, generation);
  writeUint32(data, kReservedOffset, 0U);
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

#include "PersistentSettings.h"

#include <array>

namespace luefterklappe {
namespace {

constexpr std::uint8_t kMagic0 = 'L';
constexpr std::uint8_t kMagic1 = 'K';
constexpr std::uint8_t kVersion = 1U;
constexpr PersistentSettings kDefaultSettings{1U, 1000U};

}  // namespace

PersistentSettingsStore::PersistentSettingsStore(SettingsStoragePort& storage)
    : PersistentSettingsStore(storage, kDefaultSettings) {}

PersistentSettingsStore::PersistentSettingsStore(
    SettingsStoragePort& storage, const PersistentSettings defaults)
    : storage_(storage),
      defaults_(isValid(defaults) ? defaults : kDefaultSettings) {}

PersistentSettings PersistentSettingsStore::load() {
  std::array<std::uint8_t, kStorageSize> buffer{};

  if (!storage_.read(buffer.data(), buffer.size())) {
    return defaults_;
  }

  if ((buffer[0] != kMagic0) || (buffer[1] != kMagic1) ||
      (buffer[2] != kVersion)) {
    return defaults_;
  }

  const std::uint16_t expectedCrc = crc16(buffer.data(), buffer.size() - 2U);
  const std::uint16_t storedCrc = readUint16(buffer.data(), buffer.size() - 2U);

  if (expectedCrc != storedCrc) {
    return defaults_;
  }

  const PersistentSettings settings{buffer[3], readUint16(buffer.data(), 4U)};

  if (!isValid(settings)) {
    return defaults_;
  }

  return settings;
}

bool PersistentSettingsStore::save(const PersistentSettings settings) {
  if (!isValid(settings)) {
    return false;
  }

  std::array<std::uint8_t, kStorageSize> buffer{};
  buffer[0] = kMagic0;
  buffer[1] = kMagic1;
  buffer[2] = kVersion;
  buffer[3] = settings.deviceId;
  writeUint16(buffer.data(), 4U, settings.safePositionPermille);

  const std::uint16_t crc = crc16(buffer.data(), buffer.size() - 2U);
  writeUint16(buffer.data(), buffer.size() - 2U, crc);

  return storage_.write(buffer.data(), buffer.size());
}

bool PersistentSettingsStore::isValid(const PersistentSettings settings) {
  return (settings.deviceId >= 1U) && (settings.deviceId <= 247U) &&
         (settings.safePositionPermille <= 1000U);
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

}  // namespace luefterklappe

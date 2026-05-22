#ifndef LUEFTERKLAPPE_PERSISTENT_SETTINGS_H
#define LUEFTERKLAPPE_PERSISTENT_SETTINGS_H

#include <cstddef>
#include <cstdint>

namespace luefterklappe {

struct PersistentSettings {
  std::uint8_t deviceId;
  std::uint16_t safePositionPermille;
};

class SettingsStoragePort {
 public:
  virtual ~SettingsStoragePort() = default;
  virtual bool read(std::uint8_t* data, std::size_t size) = 0;
  virtual bool write(const std::uint8_t* data, std::size_t size) = 0;
};

class PersistentSettingsStore {
 public:
  explicit PersistentSettingsStore(SettingsStoragePort& storage);
  PersistentSettingsStore(SettingsStoragePort& storage,
                          PersistentSettings defaults);

  PersistentSettings load();
  bool save(PersistentSettings settings);

  static constexpr std::size_t storageSize() { return kStorageSize; }
  static bool isValid(PersistentSettings settings);

 private:
  static constexpr std::size_t kStorageSize = 10U;

  static std::uint16_t crc16(const std::uint8_t* data,
                             std::size_t lengthWithoutCrc);
  static std::uint16_t readUint16(const std::uint8_t* data,
                                  std::size_t offset);
  static void writeUint16(std::uint8_t* data, std::size_t offset,
                          std::uint16_t value);

  SettingsStoragePort& storage_;
  PersistentSettings defaults_;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_PERSISTENT_SETTINGS_H

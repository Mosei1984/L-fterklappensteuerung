#ifndef LUEFTERKLAPPE_PERSISTENT_SETTINGS_H
#define LUEFTERKLAPPE_PERSISTENT_SETTINGS_H

#include <cstddef>
#include <cstdint>

namespace luefterklappe {

struct PersistentSettings {
  std::uint8_t deviceId{1U};
  std::uint16_t safePositionPermille{1000U};
  std::uint8_t stallGuardThreshold{100U};
};

class SettingsStoragePort {
 public:
  virtual ~SettingsStoragePort() = default;
  virtual std::size_t slotCount() const = 0;
  virtual std::size_t slotSize() const = 0;
  virtual bool readSlot(std::size_t slot, std::uint8_t* data,
                        std::size_t size) = 0;
  virtual bool writeSlot(std::size_t slot, const std::uint8_t* data,
                         std::size_t size) = 0;
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
  struct DecodedRecord {
    bool valid;
    std::size_t slot;
    std::uint32_t generation;
    PersistentSettings settings;
  };

  static constexpr std::size_t kStorageSize = 16U;
  static constexpr std::size_t kJournalSlotCount = 2U;

  bool storageUsable() const;
  DecodedRecord readRecord(std::size_t slot);
  bool writeRecord(std::size_t slot, std::uint32_t generation,
                   PersistentSettings settings);
  bool decodeRecord(const std::uint8_t* data, std::size_t slot,
                    DecodedRecord& record) const;
  void encodeRecord(std::uint8_t* data, std::uint32_t generation,
                    PersistentSettings settings) const;
  static bool generationIsNewer(std::uint32_t candidate,
                                std::uint32_t current);
  static std::uint16_t crc16(const std::uint8_t* data,
                             std::size_t lengthWithoutCrc);
  static std::uint16_t readUint16(const std::uint8_t* data,
                                  std::size_t offset);
  static void writeUint16(std::uint8_t* data, std::size_t offset,
                          std::uint16_t value);
  static std::uint32_t readUint32(const std::uint8_t* data,
                                  std::size_t offset);
  static void writeUint32(std::uint8_t* data, std::size_t offset,
                          std::uint32_t value);

  SettingsStoragePort& storage_;
  PersistentSettings defaults_;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_PERSISTENT_SETTINGS_H

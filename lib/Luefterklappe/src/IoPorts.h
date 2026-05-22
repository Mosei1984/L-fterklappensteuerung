#ifndef LUEFTERKLAPPE_IO_PORTS_H
#define LUEFTERKLAPPE_IO_PORTS_H

#include <cstddef>
#include <cstdint>

namespace luefterklappe {

class UartPort {
 public:
  virtual ~UartPort() = default;
  virtual void writeByte(std::uint8_t value) = 0;
  virtual void flush() = 0;
  virtual std::size_t available() const = 0;
  virtual bool readByte(std::uint8_t& value) = 0;
};

class DelayPort {
 public:
  virtual ~DelayPort() = default;
  virtual void delayMilliseconds(std::uint32_t milliseconds) = 0;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_IO_PORTS_H

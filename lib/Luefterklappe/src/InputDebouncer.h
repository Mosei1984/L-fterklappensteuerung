#ifndef LUEFTERKLAPPE_INPUT_DEBOUNCER_H
#define LUEFTERKLAPPE_INPUT_DEBOUNCER_H

#include <cstdint>

#include "FanFlapController.h"

namespace luefterklappe {

class InputDebouncer {
 public:
  explicit InputDebouncer(std::uint32_t debounceMs);

  DigitalInputs tick(DigitalInputs raw, std::uint32_t nowMs);

 private:
  DigitalInputs stable_;
  DigitalInputs candidate_;
  std::uint32_t candidateSinceMs_;
  std::uint32_t debounceMs_;
  bool initialized_;
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_INPUT_DEBOUNCER_H

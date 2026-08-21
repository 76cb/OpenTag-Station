#pragma once

#include <limits>
#include <type_traits>

namespace opentag::core {

// Diagnostic counters must not silently look fresh after reaching their
// representable limit. This helper is deliberately limited to unsigned
// integral counters so callers cannot accidentally hide signed overflow.
template <typename T>
[[nodiscard]] constexpr T saturating_increment(T value) {
  static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
  return value == std::numeric_limits<T>::max()
             ? value
             : static_cast<T>(value + 1U);
}

}  // namespace opentag::core

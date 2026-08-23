#pragma once

#include <cstdint>

namespace opentag::ui {

constexpr std::int32_t rounded_grams_from_milligrams(
    std::int32_t milligrams) noexcept {
  const auto wide = static_cast<std::int64_t>(milligrams);
  return wide >= 0
      ? static_cast<std::int32_t>((wide + 500) / 1000)
      : static_cast<std::int32_t>(-((-wide + 500) / 1000));
}

}  // namespace opentag::ui

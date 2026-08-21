#pragma once

#include <algorithm>
#include <cstdint>

#include "core/saturating_counter.hpp"

namespace opentag::network {

class ExponentialReconnectBackoff {
 public:
  ExponentialReconnectBackoff(
      std::uint32_t initial_ms = 1000U,
      std::uint32_t maximum_ms = 60000U)
      : initial_ms_(initial_ms),
        maximum_ms_(std::max(initial_ms, maximum_ms)),
        next_ms_(initial_ms) {}

  [[nodiscard]] std::uint32_t consume_delay() {
    const auto result = next_ms_;
    next_ms_ = next_ms_ > maximum_ms_ / 2U
                   ? maximum_ms_
                   : std::min(maximum_ms_, next_ms_ * 2U);
    attempts_ = core::saturating_increment(attempts_);
    return result;
  }

  void reset() {
    next_ms_ = initial_ms_;
    attempts_ = 0U;
  }

  [[nodiscard]] std::uint32_t attempts() const { return attempts_; }
  [[nodiscard]] std::uint32_t next_delay_ms() const { return next_ms_; }

 private:
  std::uint32_t initial_ms_;
  std::uint32_t maximum_ms_;
  std::uint32_t next_ms_;
  std::uint32_t attempts_{0U};
};

}  // namespace opentag::network

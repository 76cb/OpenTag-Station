#pragma once
#include <cstdint>

namespace opentag::network {
// One backend command/probe cycle, not a fresh timeout for every HTTP request.
// Owner-task only; not an RTOS timer and never invokes NFC from HTTP frames.
class OperationBudget {
 public:
  static constexpr std::uint32_t duration_ms = 20000U;
  void begin(std::uint32_t now) { started_ = now; active_ = true; }
  void end() { active_ = false; }
  bool expired(std::uint32_t now) const {
    return active_ && static_cast<std::uint32_t>(now - started_) >= duration_ms;
  }
  std::uint32_t remaining(std::uint32_t now) const {
    if (!active_) return duration_ms;
    const auto elapsed = static_cast<std::uint32_t>(now - started_);
    return elapsed >= duration_ms ? 0U : duration_ms - elapsed;
  }
 private:
  std::uint32_t started_{0};
  bool active_{false};
};
}  // namespace opentag::network

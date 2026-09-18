#pragma once
#include <cstdint>

namespace opentag::network {
// One backend command/probe cycle, not a fresh timeout for every HTTP request.
// Owner-task only; not an RTOS timer and never invokes NFC from HTTP frames.
class OperationBudget {
 public:
  static constexpr std::uint32_t duration_ms = 20000U;
  static constexpr std::uint32_t community_duration_ms = 60000U;
  void begin(std::uint32_t now) { started_ = now; active_ = true; limit_ = duration_ms; }
  void begin_community(std::uint32_t now) { begin(now); limit_ = community_duration_ms; }
  void end() { active_ = false; }
  bool expired(std::uint32_t now) const {
    return active_ && static_cast<std::uint32_t>(now - started_) >= limit_;
  }
  std::uint32_t remaining(std::uint32_t now) const {
    if (!active_) return duration_ms;
    const auto elapsed = static_cast<std::uint32_t>(now - started_);
    return elapsed >= limit_ ? 0U : limit_ - elapsed;
  }
 private:
  std::uint32_t started_{0};
  std::uint32_t limit_{duration_ms};
  bool active_{false};
};
}  // namespace opentag::network

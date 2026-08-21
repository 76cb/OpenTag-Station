#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saturating_counter.hpp"

namespace opentag::network {

enum class ProvisioningReason : std::uint8_t {
  none,
  unconfigured,
  connection_failures,
  requested,
};

[[nodiscard]] inline const char* to_string(ProvisioningReason reason) {
  switch (reason) {
    case ProvisioningReason::none: return "none";
    case ProvisioningReason::unconfigured: return "unconfigured";
    case ProvisioningReason::connection_failures: return "connection_failures";
    case ProvisioningReason::requested: return "requested";
  }
  return "none";
}

class ProvisioningPolicy {
 public:
  static constexpr std::uint32_t failure_threshold = 3U;
  static constexpr std::uint32_t connected_grace_ms = 30000U;

  void initialize(bool wifi_configured) {
    failures_ = 0U;
    grace_active_ = false;
    reason_ = wifi_configured ? ProvisioningReason::none
                              : ProvisioningReason::unconfigured;
  }

  void request_setup() {
    grace_active_ = false;
    reason_ = ProvisioningReason::requested;
  }

  void begin_connection_attempt() { grace_active_ = false; }

  void connection_failed() {
    failures_ = core::saturating_increment(failures_);
    grace_active_ = false;
    if (reason_ != ProvisioningReason::unconfigured &&
        failures_ >= failure_threshold) {
      reason_ = ProvisioningReason::connection_failures;
    }
  }

  void connected(std::uint32_t now_ms) {
    failures_ = 0U;
    if (reason_ != ProvisioningReason::none) {
      grace_active_ = true;
      grace_deadline_ms_ = now_ms + connected_grace_ms;
    }
  }

  void poll(std::uint32_t now_ms) {
    if (grace_active_ &&
        static_cast<std::int32_t>(now_ms - grace_deadline_ms_) >= 0) {
      grace_active_ = false;
      reason_ = ProvisioningReason::none;
    }
  }

  [[nodiscard]] bool active() const {
    return reason_ != ProvisioningReason::none;
  }
  [[nodiscard]] bool grace_active() const { return grace_active_; }
  [[nodiscard]] ProvisioningReason reason() const { return reason_; }
  [[nodiscard]] std::uint32_t failures() const { return failures_; }
  [[nodiscard]] std::uint32_t grace_remaining_ms(std::uint32_t now_ms) const {
    if (!grace_active_ ||
        static_cast<std::int32_t>(now_ms - grace_deadline_ms_) >= 0) {
      return 0U;
    }
    return grace_deadline_ms_ - now_ms;
  }

 private:
  ProvisioningReason reason_{ProvisioningReason::none};
  std::uint32_t failures_{0U};
  std::uint32_t grace_deadline_ms_{0U};
  bool grace_active_{false};
};

struct WifiNetwork {
  std::string ssid;
  std::int32_t rssi_dbm{0};
  bool secured{false};
};

[[nodiscard]] inline std::vector<WifiNetwork> normalize_scan_results(
    const std::vector<WifiNetwork>& observed,
    std::size_t maximum_results = 32U) {
  std::vector<WifiNetwork> result;
  result.reserve(std::min(observed.size(), maximum_results));
  for (const auto& candidate : observed) {
    if (candidate.ssid.empty()) continue;
    const auto existing = std::find_if(
        result.begin(), result.end(), [&](const auto& value) {
          return value.ssid == candidate.ssid;
        });
    if (existing == result.end()) {
      result.push_back(candidate);
    } else if (candidate.rssi_dbm > existing->rssi_dbm) {
      *existing = candidate;
    }
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    if (left.rssi_dbm != right.rssi_dbm) {
      return left.rssi_dbm > right.rssi_dbm;
    }
    return left.ssid < right.ssid;
  });
  if (result.size() > maximum_results) result.resize(maximum_results);
  return result;
}

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

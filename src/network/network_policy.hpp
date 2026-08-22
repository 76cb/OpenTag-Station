#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
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

inline constexpr std::int16_t wifi_scan_running_result = -1;

enum class WifiScanOutcome : std::uint8_t {
  idle,
  running,
  complete,
  failed,
};

struct WifiScanTransition {
  WifiScanOutcome outcome{WifiScanOutcome::idle};
  std::int16_t result_code{0};
};

// Transport-independent bookkeeping for the Arduino Wi-Fi asynchronous scan
// contract. A request received while a scan is running is coalesced into one
// follow-up scan instead of being lost or forcing scanDelete() mid-scan.
class AsyncWifiScanState {
 public:
  void request() { requested_.store(true, std::memory_order_release); }

  [[nodiscard]] bool start_due() {
    if (running_) return false;
    return requested_.exchange(false, std::memory_order_acq_rel);
  }

  [[nodiscard]] bool running() const { return running_; }
  [[nodiscard]] std::uint32_t generation() const { return generation_; }
  [[nodiscard]] std::optional<std::int16_t> last_failure_code() const {
    return last_failure_code_;
  }

  [[nodiscard]] WifiScanTransition accept_start_result(
      std::int16_t result_code) {
    return accept_result(result_code);
  }

  [[nodiscard]] WifiScanTransition accept_poll_result(
      std::int16_t result_code) {
    return accept_result(result_code);
  }

 private:
  [[nodiscard]] WifiScanTransition accept_result(std::int16_t result_code) {
    if (result_code == wifi_scan_running_result) {
      running_ = true;
      return {WifiScanOutcome::running, result_code};
    }
    running_ = false;
    if (result_code < 0) {
      last_failure_code_ = result_code;
      return {WifiScanOutcome::failed, result_code};
    }
    last_failure_code_.reset();
    generation_ = core::saturating_increment(generation_);
    return {WifiScanOutcome::complete, result_code};
  }

  std::atomic_bool requested_{false};
  bool running_{false};
  std::uint32_t generation_{0U};
  std::optional<std::int16_t> last_failure_code_;
};

// Save & Connect commands may not touch persistence or the radio until the
// HTTP task confirms that the 202 receipt was delivered. The fixed atomic gate
// carries only an operation id and does not retain credentials.
class NetworkConnectReceiptGate {
 public:
  [[nodiscard]] bool expect(std::uint64_t operation_id) {
    if (operation_id == 0U) return false;
    std::uint64_t empty = 0U;
    if (!expected_.compare_exchange_strong(
            empty, operation_id, std::memory_order_acq_rel)) {
      return false;
    }
    // No acknowledgement can be published until expect() returns the newly
    // reserved id to the submitter. Clear the prior receipt only after the
    // reservation succeeds so a rejected overlap cannot erase it.
    delivered_.store(0U, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool acknowledge(std::uint64_t operation_id) {
    if (operation_id == 0U ||
        expected_.load(std::memory_order_acquire) != operation_id) {
      return false;
    }
    delivered_.store(operation_id, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool delivered(std::uint64_t operation_id) const {
    return operation_id != 0U &&
        delivered_.load(std::memory_order_acquire) == operation_id;
  }

  void clear(std::uint64_t operation_id) {
    if (expected_.load(std::memory_order_acquire) == operation_id) {
      delivered_.store(0U, std::memory_order_release);
      expected_.store(0U, std::memory_order_release);
    }
  }

 private:
  std::atomic<std::uint64_t> expected_{0U};
  std::atomic<std::uint64_t> delivered_{0U};
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

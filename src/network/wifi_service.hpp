#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(ARDUINO)
#include <DNSServer.h>
#endif
#include "application/operation_registry.hpp"
#include "config/configuration_service.hpp"
#include "core/result.hpp"
#include "network/network_policy.hpp"

namespace opentag::network {

enum class WifiState : std::uint8_t {
  uninitialized,
  unconfigured,
  connecting,
  connected,
  reconnect_backoff,
  disconnected,
  fault,
};

[[nodiscard]] const char* to_string(WifiState state);

struct WifiStatus {
  WifiState state{WifiState::uninitialized};
  bool configured{false};
  bool connected{false};
  bool scan_running{false};
  bool mdns_ready{false};
  bool ntp_ready{false};
  std::string ssid;
  std::int32_t rssi_dbm{0};
  std::string ip_address;
  std::string gateway;
  std::string dns_server;
  std::uint32_t reconnect_attempts{0U};
  std::uint32_t next_reconnect_at_ms{0U};
  bool provisioning_active{false};
  bool provisioning_grace_active{false};
  ProvisioningReason provisioning_reason{ProvisioningReason::none};
  std::uint32_t provisioning_failures{0U};
  std::uint32_t provisioning_grace_remaining_ms{0U};
  std::string setup_ap_ssid;
  std::string setup_ap_ip;
  std::uint32_t scan_generation{0U};
  std::uint32_t scan_attempt_generation{0U};
  std::uint32_t scan_result_attempt_generation{0U};
  std::optional<core::Error> scan_error;
  std::optional<core::Error> last_error;
};

class WifiService {
 public:
  explicit WifiService(application::OperationRegistry& operations)
      : operations_(operations) {}
  ~WifiService();

  [[nodiscard]] core::Result<void> initialize(
      const config::DeviceSettings& device,
      const config::WifiSettings& wifi,
      std::uint32_t now_ms);
  void poll(std::uint32_t now_ms);
  void request_scan();
  [[nodiscard]] bool request_scan(std::uint64_t operation_id);
  void request_setup_mode();
  [[nodiscard]] bool request_setup_mode(std::uint64_t operation_id);
  [[nodiscard]] bool request_reconfigure(
      const config::DeviceSettings& device,
      const config::WifiSettings& wifi,
      std::uint64_t operation_id);
  [[nodiscard]] bool reconfigure_operation_finished(
      std::uint64_t operation_id) const;
  [[nodiscard]] bool abandon_reconfigure_operation(
      std::uint64_t operation_id);
  [[nodiscard]] std::vector<WifiNetwork> scan_results() const;
  [[nodiscard]] const WifiStatus& status() const { return status_; }
  [[nodiscard]] core::Result<std::string> resolve_hostname(
      const std::string& hostname);

 private:
  void start_connection(std::uint32_t now_ms);
  void schedule_reconnect(std::uint32_t now_ms, const char* reason);
  void enter_connected(std::uint32_t now_ms);
  void leave_connected();
  void poll_scan(std::uint32_t now_ms, bool start_allowed);
  void finish_scan(std::int16_t result_count);
  void fail_scan(
      std::int16_t result_code,
      bool asynchronous_scan_started,
      std::uint32_t elapsed_ms);
  void complete_setup_mode_request(std::uint32_t now_ms);
  void succeed_reconfigure(std::uint32_t now_ms, const char* message);
  void fail_reconfigure(std::uint32_t now_ms, core::Error error);
  bool start_setup_ap();
  void stop_setup_ap();
  void update_provisioning_status(std::uint32_t now_ms);

  struct PendingConfiguration {
    config::DeviceSettings device;
    config::WifiSettings wifi;
    std::uint64_t operation_id{0U};
  };

  application::OperationRegistry& operations_;
  config::DeviceSettings device_;
  config::WifiSettings wifi_;
  WifiStatus status_;
  ExponentialReconnectBackoff backoff_;
  ProvisioningPolicy provisioning_;
#if defined(ARDUINO)
  DNSServer captive_dns_;
#endif
  std::uint32_t connect_started_ms_{0U};
  std::atomic_bool initialized_{false};
  bool setup_ap_running_{false};
  bool was_connected_{false};
  bool ntp_requested_{false};
  bool setup_mode_pending_{false};
  std::uint32_t scan_started_ms_{0U};
  bool scan_stop_requested_{false};
  AsyncWifiScanState scan_state_;
  CorrelatedOperationClaim scan_operation_;
  CorrelatedOperationClaim setup_operation_;
  std::uint64_t active_scan_operation_id_{0U};
  std::atomic_bool setup_requested_{false};
  std::atomic<std::uint64_t> pending_reconfigure_operation_id_{0U};
  std::atomic<std::uint64_t> active_reconfigure_operation_id_{0U};
  std::atomic<std::uint64_t> completed_reconfigure_operation_id_{0U};
  mutable std::mutex reconfigure_mutex_;
  std::optional<PendingConfiguration> pending_configuration_;
  mutable std::mutex scan_mutex_;
  std::vector<WifiNetwork> scan_results_;
};

}  // namespace opentag::network

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
  std::optional<core::Error> scan_error;
  std::optional<core::Error> last_error;
};

class WifiService {
 public:
  [[nodiscard]] core::Result<void> initialize(
      const config::DeviceSettings& device,
      const config::WifiSettings& wifi,
      std::uint32_t now_ms);
  void poll(std::uint32_t now_ms);
  void request_scan();
  void request_setup_mode();
  void request_reconfigure(
      const config::DeviceSettings& device,
      const config::WifiSettings& wifi);
  [[nodiscard]] std::vector<WifiNetwork> scan_results() const;
  [[nodiscard]] const WifiStatus& status() const { return status_; }
  [[nodiscard]] core::Result<std::string> resolve_hostname(
      const std::string& hostname);

 private:
  void start_connection(std::uint32_t now_ms);
  void schedule_reconnect(std::uint32_t now_ms, const char* reason);
  void enter_connected(std::uint32_t now_ms);
  void leave_connected();
  void poll_scan();
  bool start_setup_ap();
  void stop_setup_ap();
  void update_provisioning_status(std::uint32_t now_ms);

  config::DeviceSettings device_;
  config::WifiSettings wifi_;
  WifiStatus status_;
  ExponentialReconnectBackoff backoff_;
  ProvisioningPolicy provisioning_;
#if defined(ARDUINO)
  DNSServer captive_dns_;
#endif
  std::uint32_t connect_started_ms_{0U};
  bool initialized_{false};
  bool setup_ap_running_{false};
  bool was_connected_{false};
  bool ntp_requested_{false};
  std::atomic_bool scan_requested_{false};
  std::atomic_bool setup_requested_{false};
  mutable std::mutex reconfigure_mutex_;
  std::optional<std::pair<config::DeviceSettings, config::WifiSettings>>
      pending_configuration_;
  mutable std::mutex scan_mutex_;
  std::vector<WifiNetwork> scan_results_;
};

}  // namespace opentag::network

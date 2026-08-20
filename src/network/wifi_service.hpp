#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

struct WifiNetwork {
  std::string ssid;
  std::int32_t rssi_dbm{0};
  bool secured{false};
};

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
  void enter_connected();
  void leave_connected();
  void poll_scan();

  config::DeviceSettings device_;
  config::WifiSettings wifi_;
  WifiStatus status_;
  ExponentialReconnectBackoff backoff_;
  std::uint32_t connect_started_ms_{0U};
  bool initialized_{false};
  bool was_connected_{false};
  bool ntp_requested_{false};
  std::atomic_bool scan_requested_{false};
  mutable std::mutex reconfigure_mutex_;
  std::optional<std::pair<config::DeviceSettings, config::WifiSettings>>
      pending_configuration_;
  mutable std::mutex scan_mutex_;
  std::vector<WifiNetwork> scan_results_;
};

}  // namespace opentag::network

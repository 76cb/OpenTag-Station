#include "network/wifi_service.hpp"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <string>

namespace opentag::network {
namespace {

core::Error network_error(const std::string& message, bool retryable = true) {
  return {core::ErrorCategory::network, message, retryable};
}

}  // namespace

const char* to_string(WifiState state) {
  switch (state) {
    case WifiState::uninitialized: return "uninitialized";
    case WifiState::unconfigured: return "not configured";
    case WifiState::connecting: return "connecting";
    case WifiState::connected: return "connected";
    case WifiState::reconnect_backoff: return "retrying";
    case WifiState::disconnected: return "disconnected";
    case WifiState::fault: return "fault";
  }
  return "unknown";
}

core::Result<void> WifiService::initialize(
    const config::DeviceSettings& device,
    const config::WifiSettings& wifi,
    std::uint32_t now_ms) {
  device_ = device;
  wifi_ = wifi;
  status_ = {};
  status_.configured = !wifi_.ssid.empty();
  status_.ssid = wifi_.ssid;
  backoff_ = ExponentialReconnectBackoff(
      wifi_.reconnect_initial_ms, wifi_.reconnect_max_ms);
  was_connected_ = false;
  ntp_requested_ = false;

  WiFi.scanDelete();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA) || !WiFi.setHostname(device_.hostname.c_str())) {
    const auto error = network_error("Wi-Fi station initialization failed", false);
    status_.state = WifiState::fault;
    status_.last_error = error;
    return core::Result<void>::failure(error);
  }
  initialized_ = true;
  if (!status_.configured) {
    status_.state = WifiState::unconfigured;
    request_scan();
    return core::Result<void>::success();
  }
  start_connection(now_ms);
  return core::Result<void>::success();
}

void WifiService::start_connection(std::uint32_t now_ms) {
  WiFi.disconnect(false, false);
  WiFi.begin(
      wifi_.ssid.c_str(), wifi_.password.empty() ? nullptr : wifi_.password.c_str());
  connect_started_ms_ = now_ms;
  status_.state = WifiState::connecting;
  status_.connected = false;
  status_.last_error.reset();
}

void WifiService::schedule_reconnect(
    std::uint32_t now_ms,
    const char* reason) {
  leave_connected();
  status_.last_error = network_error(reason);
  if (!wifi_.auto_reconnect) {
    status_.state = WifiState::disconnected;
    return;
  }
  const auto delay_ms = backoff_.consume_delay();
  status_.reconnect_attempts = backoff_.attempts();
  status_.next_reconnect_at_ms = now_ms + delay_ms;
  status_.state = WifiState::reconnect_backoff;
}

void WifiService::enter_connected() {
  status_.state = WifiState::connected;
  status_.connected = true;
  status_.rssi_dbm = WiFi.RSSI();
  status_.ip_address = WiFi.localIP().toString().c_str();
  status_.gateway = WiFi.gatewayIP().toString().c_str();
  status_.dns_server = WiFi.dnsIP(0U).toString().c_str();
  status_.last_error.reset();
  backoff_.reset();
  status_.reconnect_attempts = 0U;
  status_.next_reconnect_at_ms = 0U;
  status_.mdns_ready = MDNS.begin(device_.hostname.c_str());
  configTime(0L, 0, "pool.ntp.org", "time.nist.gov", "time.cloudflare.com");
  ntp_requested_ = true;
  was_connected_ = true;
}

void WifiService::leave_connected() {
  if (status_.mdns_ready) MDNS.end();
  status_.connected = false;
  status_.mdns_ready = false;
  status_.ntp_ready = false;
  status_.rssi_dbm = 0;
  status_.ip_address.clear();
  status_.gateway.clear();
  status_.dns_server.clear();
  was_connected_ = false;
  ntp_requested_ = false;
}

void WifiService::request_scan() {
  scan_requested_.store(true, std::memory_order_relaxed);
}

void WifiService::request_reconfigure(
    const config::DeviceSettings& device,
    const config::WifiSettings& wifi) {
  const std::lock_guard<std::mutex> lock(reconfigure_mutex_);
  pending_configuration_ = std::make_pair(device, wifi);
}

std::vector<WifiNetwork> WifiService::scan_results() const {
  const std::lock_guard<std::mutex> lock(scan_mutex_);
  return scan_results_;
}

void WifiService::poll_scan() {
  if (scan_requested_.exchange(false, std::memory_order_relaxed) &&
      !status_.scan_running) {
    WiFi.scanDelete();
    const auto started = WiFi.scanNetworks(true, true, false, 120U);
    status_.scan_running = started == WIFI_SCAN_RUNNING;
    if (!status_.scan_running && started < 0) {
      status_.last_error = network_error("Wi-Fi scan could not be started");
    }
  }
  if (!status_.scan_running) return;
  const auto complete = WiFi.scanComplete();
  if (complete == WIFI_SCAN_RUNNING) return;
  status_.scan_running = false;
  if (complete < 0) {
    status_.last_error = network_error("Wi-Fi scan failed");
    WiFi.scanDelete();
    return;
  }

  std::map<std::string, WifiNetwork> strongest;
  for (std::int16_t index = 0; index < complete && index < 64; ++index) {
    const std::string ssid = WiFi.SSID(index).c_str();
    if (ssid.empty()) continue;
    WifiNetwork network{
        ssid,
        WiFi.RSSI(index),
        WiFi.encryptionType(index) != WIFI_AUTH_OPEN};
    const auto existing = strongest.find(ssid);
    if (existing == strongest.end() ||
        network.rssi_dbm > existing->second.rssi_dbm) {
      strongest[ssid] = std::move(network);
    }
  }
  std::vector<WifiNetwork> results;
  results.reserve(strongest.size());
  for (auto& entry : strongest) results.push_back(std::move(entry.second));
  std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
    return left.rssi_dbm > right.rssi_dbm;
  });
  if (results.size() > 32U) results.resize(32U);
  {
    const std::lock_guard<std::mutex> lock(scan_mutex_);
    scan_results_ = std::move(results);
  }
  WiFi.scanDelete();
}

void WifiService::poll(std::uint32_t now_ms) {
  std::optional<std::pair<config::DeviceSettings, config::WifiSettings>>
      pending;
  {
    const std::lock_guard<std::mutex> lock(reconfigure_mutex_);
    if (pending_configuration_.has_value()) {
      pending = std::move(pending_configuration_);
      pending_configuration_.reset();
    }
  }
  if (pending.has_value()) {
    const auto reconfigured = initialize(
        pending->first, pending->second, now_ms);
    (void)reconfigured;
  }
  if (!initialized_) return;
  poll_scan();
  if (!status_.configured) return;

  const auto wifi_status = WiFi.status();
  if (wifi_status == WL_CONNECTED) {
    if (!was_connected_) enter_connected();
    status_.rssi_dbm = WiFi.RSSI();
    if (ntp_requested_ && std::time(nullptr) >= 1700000000) {
      status_.ntp_ready = true;
    }
    return;
  }
  if (was_connected_) {
    schedule_reconnect(now_ms, "Wi-Fi connection was lost");
    return;
  }
  if (status_.state == WifiState::connecting &&
      static_cast<std::uint32_t>(now_ms - connect_started_ms_) >=
          wifi_.connect_timeout_ms) {
    WiFi.disconnect(false, false);
    schedule_reconnect(
        now_ms,
        wifi_status == WL_NO_SSID_AVAIL
            ? "configured Wi-Fi network was not found"
            : "Wi-Fi connection timed out");
    return;
  }
  if (status_.state == WifiState::reconnect_backoff &&
      static_cast<std::int32_t>(now_ms - status_.next_reconnect_at_ms) >= 0) {
    start_connection(now_ms);
  }
}

core::Result<std::string> WifiService::resolve_hostname(
    const std::string& hostname) {
  if (!status_.connected || hostname.empty() || hostname.size() > 253U) {
    return core::Result<std::string>::failure(
        network_error("DNS lookup requires a connected network and valid hostname"));
  }
  IPAddress address;
  if (!WiFi.hostByName(hostname.c_str(), address)) {
    const auto error = network_error("DNS resolution failed for " + hostname);
    status_.last_error = error;
    return core::Result<std::string>::failure(error);
  }
  status_.dns_server = WiFi.dnsIP(0U).toString().c_str();
  return core::Result<std::string>::success(address.toString().c_str());
}

}  // namespace opentag::network

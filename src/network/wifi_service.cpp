#include "network/wifi_service.hpp"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
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
  provisioning_.initialize(status_.configured);
  was_connected_ = false;
  ntp_requested_ = false;

  WiFi.scanDelete();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(
          provisioning_.active() ? WIFI_AP_STA : WIFI_STA) ||
      !WiFi.setHostname(device_.hostname.c_str())) {
    const auto error = network_error("Wi-Fi station initialization failed", false);
    status_.state = WifiState::fault;
    status_.last_error = error;
    return core::Result<void>::failure(error);
  }
  initialized_ = true;
  if (provisioning_.active() && !start_setup_ap()) {
    const auto error = network_error(
        "Wi-Fi setup access point could not be started", false);
    status_.state = WifiState::fault;
    status_.last_error = error;
    return core::Result<void>::failure(error);
  }
  update_provisioning_status(now_ms);
  if (!status_.configured) {
    status_.state = WifiState::unconfigured;
    request_scan();
    return core::Result<void>::success();
  }
  start_connection(now_ms);
  return core::Result<void>::success();
}

void WifiService::start_connection(std::uint32_t now_ms) {
  provisioning_.begin_connection_attempt();
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
  provisioning_.connection_failed();
  if (provisioning_.active() && !setup_ap_running_) {
    (void)start_setup_ap();
  }
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

void WifiService::enter_connected(std::uint32_t now_ms) {
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
  provisioning_.connected(now_ms);
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

void WifiService::request_setup_mode() {
  setup_requested_.store(true, std::memory_order_relaxed);
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
      status_.scan_error =
          network_error("Wi-Fi scan could not be started");
    }
  }
  if (!status_.scan_running) return;
  const auto complete = WiFi.scanComplete();
  if (complete == WIFI_SCAN_RUNNING) return;
  status_.scan_running = false;
  if (complete < 0) {
    status_.scan_error = network_error("Wi-Fi scan failed");
    WiFi.scanDelete();
    return;
  }

  std::vector<WifiNetwork> observed;
  observed.reserve(static_cast<std::size_t>(std::min<std::int16_t>(
      complete, 64)));
  for (std::int16_t index = 0; index < complete && index < 64; ++index) {
    const std::string ssid = WiFi.SSID(index).c_str();
    if (ssid.empty()) continue;
    observed.push_back({
        ssid,
        WiFi.RSSI(index),
        WiFi.encryptionType(index) != WIFI_AUTH_OPEN});
  }
  auto results = normalize_scan_results(observed);
  {
    const std::lock_guard<std::mutex> lock(scan_mutex_);
    scan_results_ = std::move(results);
  }
  status_.scan_error.reset();
  ++status_.scan_generation;
  WiFi.scanDelete();
}

bool WifiService::start_setup_ap() {
  if (setup_ap_running_) return true;
  if (!WiFi.mode(WIFI_AP_STA)) return false;
  const auto identifier =
      static_cast<std::uint16_t>(ESP.getEfuseMac() & 0xFFFFU);
  char ssid[24]{};
  std::snprintf(
      ssid, sizeof(ssid), "OpenTag-Setup-%04X",
      static_cast<unsigned int>(identifier));
  const IPAddress address(192U, 168U, 4U, 1U);
  const IPAddress netmask(255U, 255U, 255U, 0U);
  if (!WiFi.softAPConfig(address, address, netmask) ||
      !WiFi.softAP(ssid)) {
    return false;
  }
  if (!captive_dns_.start(53U, "*", address)) {
    WiFi.softAPdisconnect(true);
    return false;
  }
  setup_ap_running_ = true;
  status_.setup_ap_ssid = ssid;
  status_.setup_ap_ip = "192.168.4.1";
  return true;
}

void WifiService::stop_setup_ap() {
  if (!setup_ap_running_) return;
  captive_dns_.stop();
  WiFi.softAPdisconnect(true);
  setup_ap_running_ = false;
  status_.setup_ap_ssid.clear();
  status_.setup_ap_ip.clear();
  (void)WiFi.mode(WIFI_STA);
}

void WifiService::update_provisioning_status(std::uint32_t now_ms) {
  status_.provisioning_active =
      provisioning_.active() && setup_ap_running_;
  status_.provisioning_grace_active = provisioning_.grace_active();
  status_.provisioning_reason = provisioning_.reason();
  status_.provisioning_failures = provisioning_.failures();
  status_.provisioning_grace_remaining_ms =
      provisioning_.grace_remaining_ms(now_ms);
}

void WifiService::poll(std::uint32_t now_ms) {
  if (setup_requested_.exchange(false, std::memory_order_relaxed)) {
    provisioning_.request_setup();
    (void)start_setup_ap();
    request_scan();
  }
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
    device_ = pending->first;
    wifi_ = pending->second;
    status_.configured = !wifi_.ssid.empty();
    status_.ssid = wifi_.ssid;
    backoff_ = ExponentialReconnectBackoff(
        wifi_.reconnect_initial_ms, wifi_.reconnect_max_ms);
    (void)WiFi.setHostname(device_.hostname.c_str());
    if (!status_.configured) {
      provisioning_.initialize(false);
      (void)start_setup_ap();
      leave_connected();
      status_.state = WifiState::unconfigured;
      request_scan();
    } else {
      leave_connected();
      start_connection(now_ms);
    }
  }
  if (!initialized_) return;
  provisioning_.poll(now_ms);
  if (!provisioning_.active() && setup_ap_running_) stop_setup_ap();
  if (setup_ap_running_) captive_dns_.processNextRequest();
  poll_scan();
  if (!status_.configured) {
    update_provisioning_status(now_ms);
    return;
  }

  const auto wifi_status = WiFi.status();
  if (wifi_status == WL_CONNECTED) {
    if (!was_connected_) enter_connected(now_ms);
    status_.rssi_dbm = WiFi.RSSI();
    if (ntp_requested_ && std::time(nullptr) >= 1700000000) {
      status_.ntp_ready = true;
    }
    update_provisioning_status(now_ms);
    return;
  }
  if (was_connected_) {
    schedule_reconnect(now_ms, "Wi-Fi connection was lost");
    update_provisioning_status(now_ms);
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
    update_provisioning_status(now_ms);
    return;
  }
  if (status_.state == WifiState::reconnect_backoff &&
      static_cast<std::int32_t>(now_ms - status_.next_reconnect_at_ms) >= 0) {
    start_connection(now_ms);
  }
  update_provisioning_status(now_ms);
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

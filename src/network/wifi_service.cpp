#include "network/wifi_service.hpp"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

namespace opentag::network {
namespace {

constexpr std::uint32_t maximum_async_scan_duration_ms = 15000U;

static_assert(
    WIFI_SCAN_RUNNING == wifi_scan_running_result,
    "Arduino Wi-Fi scan-running result changed");
static_assert(
    WIFI_SCAN_FAILED == wifi_scan_failed_result,
    "Arduino Wi-Fi scan-failed result changed");

core::Error network_error(const std::string& message, bool retryable = true) {
  return {core::ErrorCategory::network, message, retryable};
}

}  // namespace

WifiService::~WifiService() {
  initialized_.store(false, std::memory_order_release);
  const auto driver_result = WiFi.scanComplete();
  if (driver_result >= 0) {
    // WIFI_SCAN_DONE_BIT is published only after the Arduino callback finishes
    // populating its result buffer, so deletion is safe on this branch.
    WiFi.scanDelete();
  } else if (scan_state_.running() && !scan_stop_requested_) {
    // Teardown cannot wait indefinitely for a hardware callback. Stop once and
    // deliberately leave wrapper-owned storage alone if completion is not yet
    // authoritative; its static callback state outlives this service object.
    scan_stop_requested_ = true;
    (void)esp_wifi_scan_stop();
  }
}

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
  if (initialized_.load(std::memory_order_acquire)) {
    // Runtime settings changes belong to request_reconfigure(). Re-entering
    // initialization could otherwise reset ownership during an active scan.
    return core::Result<void>::success();
  }

  const auto existing_scan = WiFi.scanComplete();
  if (scan_stop_requested_ && existing_scan < 0) {
    const auto error = network_error(
        "Wi-Fi initialization is waiting for a stopped scan callback to quiesce",
        false);
    status_.state = WifiState::fault;
    status_.last_error = error;
    return core::Result<void>::failure(error);
  }
  if (existing_scan >= 0) {
    // A nonnegative result means _scanDone() published the done bit after
    // finishing all access to the wrapper-owned result buffer.
    WiFi.scanDelete();
    scan_stop_requested_ = false;
  } else if (existing_scan == WIFI_SCAN_RUNNING) {
    // No repository owner starts a scan before this one-shot initialization.
    // If the wrapper nevertheless reports one, fail closed: stop it once and
    // do not mutate radio mode until its asynchronous callback has quiesced.
    scan_stop_requested_ = true;
    const auto stop_result = esp_wifi_scan_stop();
    const auto error = network_error(
        "Wi-Fi initialization found an active foreign scan; stop requested and retry required",
        false);
    status_.state = WifiState::fault;
    status_.last_error = error;
    Serial.printf(
        "wifi_scan=stale_at_initialize stop_result=%d\n",
        static_cast<int>(stop_result));
    return core::Result<void>::failure(error);
  }
  // WIFI_SCAN_FAILED is also the wrapper's never-started sentinel. On first
  // initialization it owns no result storage, so neither stop nor delete it.

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
  setup_mode_pending_ = false;
  scan_started_ms_ = 0U;
  scan_stop_requested_ = false;
  active_scan_operation_id_ = 0U;
  pending_reconfigure_operation_id_.store(0U, std::memory_order_relaxed);
  active_reconfigure_operation_id_.store(0U, std::memory_order_relaxed);
  completed_reconfigure_operation_id_.store(0U, std::memory_order_relaxed);
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
  if (provisioning_.active() && !start_setup_ap()) {
    const auto error = network_error(
        "Wi-Fi setup access point could not be started", false);
    status_.state = WifiState::fault;
    status_.last_error = error;
    return core::Result<void>::failure(error);
  }
  initialized_.store(true, std::memory_order_release);
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
  Serial.printf(
      "sta_association=started ap=%s\n",
      setup_ap_running_ ? "retained" : "inactive");
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
  const auto error = network_error(reason);
  status_.last_error = error;
  fail_reconfigure(now_ms, error);
  Serial.printf("sta_association=failed reason=%s ap=%s\n", reason,
                setup_ap_running_ ? "retained" : "inactive");
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
  Serial.printf(
      "sta_association=connected lan_ip=%s ap_grace=%s\n",
      status_.ip_address.c_str(),
      provisioning_.grace_active() ? "active" : "inactive");
  succeed_reconfigure(
      now_ms, "Wi-Fi configuration applied and station connected");
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
  scan_state_.request();
  Serial.println("wifi_scan=requested");
}

bool WifiService::request_scan(std::uint64_t operation_id) {
  if (!initialized_.load(std::memory_order_acquire) ||
      !scan_operation_.try_claim(operation_id)) {
    return false;
  }
  request_scan();
  return true;
}

void WifiService::request_setup_mode() {
  setup_requested_.store(true, std::memory_order_relaxed);
}

bool WifiService::request_setup_mode(std::uint64_t operation_id) {
  if (!initialized_.load(std::memory_order_acquire) ||
      !setup_operation_.try_claim(operation_id)) {
    return false;
  }
  request_setup_mode();
  return true;
}

bool WifiService::request_reconfigure(
    const config::DeviceSettings& device,
    const config::WifiSettings& wifi,
    std::uint64_t operation_id) {
  if (operation_id == 0U ||
      !initialized_.load(std::memory_order_acquire)) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(reconfigure_mutex_);
  if (pending_configuration_.has_value() ||
      active_reconfigure_operation_id_.load(std::memory_order_acquire) != 0U) {
    return false;
  }
  completed_reconfigure_operation_id_.store(0U, std::memory_order_relaxed);
  pending_configuration_ = PendingConfiguration{device, wifi, operation_id};
  pending_reconfigure_operation_id_.store(
      operation_id, std::memory_order_release);
  return true;
}

bool WifiService::reconfigure_operation_finished(
    std::uint64_t operation_id) const {
  return operation_id != 0U &&
      completed_reconfigure_operation_id_.load(std::memory_order_acquire) ==
          operation_id;
}

bool WifiService::abandon_reconfigure_operation(
    std::uint64_t operation_id) {
  if (operation_id == 0U) return false;
  bool detached = false;
  {
    const std::lock_guard<std::mutex> lock(reconfigure_mutex_);
    if (pending_configuration_.has_value() &&
        pending_configuration_->operation_id == operation_id) {
      // Keep the already-persisted configuration queued for eventual runtime
      // application, but detach the browser-visible operation so a late radio
      // result cannot overwrite its bounded timeout failure.
      pending_configuration_->operation_id = 0U;
      pending_reconfigure_operation_id_.store(
          0U, std::memory_order_release);
      detached = true;
    }
  }
  auto expected = operation_id;
  if (active_reconfigure_operation_id_.compare_exchange_strong(
          expected, 0U, std::memory_order_acq_rel)) {
    detached = true;
  }
  return detached;
}

std::vector<WifiNetwork> WifiService::scan_results() const {
  const std::lock_guard<std::mutex> lock(scan_mutex_);
  return scan_results_;
}

void WifiService::poll_scan(
    std::uint32_t now_ms,
    bool start_allowed) {
  if (scan_state_.start_due(start_allowed)) {
    const auto existing = WiFi.scanComplete();
    WifiScanTransition transition;
    if (existing == WIFI_SCAN_RUNNING) {
      // A library-owned scan is already active. Join it without deleting its
      // result storage or attempting a competing AP+STA scan.
      transition = scan_state_.accept_start_result(existing);
    } else {
      if (existing >= 0) WiFi.scanDelete();
      transition = scan_state_.accept_start_result(
          WiFi.scanNetworks(true, true, false, 120U));
    }
    status_.scan_running = scan_state_.running();
    status_.scan_attempt_generation = scan_state_.attempt_generation();
    active_scan_operation_id_ = scan_operation_.operation_id();
    if (active_scan_operation_id_ != 0U) {
      operations_.mark_running(
          active_scan_operation_id_, now_ms, "Wi-Fi scan running");
    }
    if (transition.outcome == WifiScanOutcome::running) {
      scan_started_ms_ = now_ms;
      scan_stop_requested_ = false;
      status_.scan_error.reset();
      Serial.println("wifi_scan=started");
    } else if (transition.outcome == WifiScanOutcome::failed) {
      fail_scan(transition.result_code, false, 0U);
    } else if (transition.outcome == WifiScanOutcome::complete) {
      finish_scan(transition.result_code);
    }
  }
  if (!scan_state_.running()) return;
  const auto elapsed_ms =
      static_cast<std::uint32_t>(now_ms - scan_started_ms_);
  const auto decision = classify_wifi_scan_driver_poll(
      WiFi.scanComplete(),
      elapsed_ms,
      maximum_async_scan_duration_ms,
      scan_stop_requested_);
  if (decision.action == WifiScanDriverPollAction::wait) {
    // This includes the Arduino wrapper's -2 timeout. It is not safe to delete
    // results or change radio mode until scanComplete() becomes nonnegative.
    status_.scan_running = true;
    return;
  }
  if (decision.action == WifiScanDriverPollAction::request_stop) {
    scan_stop_requested_ = true;
    status_.scan_running = true;
    status_.scan_error = network_error(
        "Wi-Fi scan exceeded " +
        std::to_string(maximum_async_scan_duration_ms) +
        " ms; stop requested and waiting for driver cleanup");
    const auto stop_result = esp_wifi_scan_stop();
    Serial.printf(
        "wifi_scan=stop_requested elapsed_ms=%lu wrapper_result=%d stop_result=%d\n",
        static_cast<unsigned long>(elapsed_ms),
        static_cast<int>(decision.result_code),
        static_cast<int>(stop_result));
    return;
  }
  if (decision.action == WifiScanDriverPollAction::collect_results) {
    const auto transition =
        scan_state_.accept_poll_result(decision.result_code);
    status_.scan_running = scan_state_.running();
    finish_scan(transition.result_code);
    return;
  }

  // A nonnegative result after the one-shot stop proves _scanDone() has
  // finished. Discard any partial records, then publish the single terminal
  // timeout failure and release all serialized radio work.
  WiFi.scanDelete();
  const auto transition =
      scan_state_.accept_stopped_result(WIFI_SCAN_FAILED);
  status_.scan_running = scan_state_.running();
  scan_stop_requested_ = false;
  fail_scan(transition.result_code, true, elapsed_ms);
}

void WifiService::finish_scan(std::int16_t result_count) {
  std::vector<WifiNetwork> observed;
  observed.reserve(static_cast<std::size_t>(std::min<std::int16_t>(
      result_count, 64)));
  for (std::int16_t index = 0;
       index < result_count && index < 64;
       ++index) {
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
  status_.scan_generation = scan_state_.generation();
  status_.scan_result_attempt_generation =
      scan_state_.attempt_generation();
  WiFi.scanDelete();
  Serial.printf(
      "wifi_scan=complete count=%u\n",
      static_cast<unsigned>(scan_results_.size()));
  if (active_scan_operation_id_ != 0U) {
    operations_.succeed(
        active_scan_operation_id_, millis(), "Wi-Fi scan completed");
    (void)scan_operation_.release(active_scan_operation_id_);
    active_scan_operation_id_ = 0U;
  }
}

void WifiService::fail_scan(
    std::int16_t result_code,
    bool asynchronous_scan_started,
    std::uint32_t elapsed_ms) {
  status_.scan_running = false;
  status_.scan_result_attempt_generation =
      scan_state_.attempt_generation();
  std::string reason;
  if (result_code == WIFI_SCAN_FAILED) {
    reason = asynchronous_scan_started
        ? "Arduino scan state ended without results after " +
            std::to_string(elapsed_ms) +
            " ms (radio mode changed, wrapper timeout, or scan state was lost)"
        : "Arduino Wi-Fi driver rejected or could not start the scan";
  } else {
    reason = "unexpected Arduino scan result";
  }
  status_.scan_error = network_error(
      "Wi-Fi scan failed (" + std::to_string(result_code) + "): " + reason);
  Serial.printf(
      "wifi_scan=failed code=%d phase=%s elapsed_ms=%lu reason=%s\n",
      static_cast<int>(result_code),
      asynchronous_scan_started ? "poll" : "start",
      static_cast<unsigned long>(elapsed_ms),
      reason.c_str());
  if (active_scan_operation_id_ != 0U) {
    operations_.fail(
        active_scan_operation_id_, millis(), *status_.scan_error);
    (void)scan_operation_.release(active_scan_operation_id_);
    active_scan_operation_id_ = 0U;
  }
}

void WifiService::complete_setup_mode_request(std::uint32_t now_ms) {
  setup_mode_pending_ = false;
  const auto operation_id = setup_operation_.operation_id();
  if (start_setup_ap()) {
    request_scan();
    if (operation_id != 0U) {
      operations_.succeed(
          operation_id, now_ms, "Setup access point is running");
      (void)setup_operation_.release(operation_id);
    }
    return;
  }

  const auto error = network_error(
      "Wi-Fi setup access point could not be started", false);
  status_.last_error = error;
  if (operation_id != 0U) {
    operations_.fail(operation_id, now_ms, error);
    (void)setup_operation_.release(operation_id);
  }
}

void WifiService::succeed_reconfigure(
    std::uint32_t now_ms,
    const char* message) {
  auto operation_id =
      active_reconfigure_operation_id_.load(std::memory_order_acquire);
  if (operation_id == 0U ||
      !active_reconfigure_operation_id_.compare_exchange_strong(
          operation_id, 0U, std::memory_order_acq_rel)) {
    return;
  }
  operations_.succeed(operation_id, now_ms, message);
  completed_reconfigure_operation_id_.store(
      operation_id, std::memory_order_release);
}

void WifiService::fail_reconfigure(
    std::uint32_t now_ms,
    core::Error error) {
  auto operation_id =
      active_reconfigure_operation_id_.load(std::memory_order_acquire);
  if (operation_id == 0U ||
      !active_reconfigure_operation_id_.compare_exchange_strong(
          operation_id, 0U, std::memory_order_acq_rel)) {
    return;
  }
  operations_.fail(operation_id, now_ms, std::move(error));
  completed_reconfigure_operation_id_.store(
      operation_id, std::memory_order_release);
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
  Serial.printf(
      "setup_ap=started ssid=%s ip=%s\n",
      status_.setup_ap_ssid.c_str(),
      status_.setup_ap_ip.c_str());
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
  Serial.println("setup_ap=stopped grace=complete");
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
  if (!initialized_.load(std::memory_order_acquire)) return;

  if (setup_requested_.exchange(false, std::memory_order_acq_rel) &&
      !setup_mode_pending_) {
    const auto operation_id = setup_operation_.operation_id();
    if (operation_id != 0U) {
      operations_.mark_running(
          operation_id, now_ms, "Starting setup access point");
    }
    provisioning_.request_setup();
    if (scan_state_.running()) {
      setup_mode_pending_ = true;
    } else {
      complete_setup_mode_request(now_ms);
    }
  }

  std::optional<PendingConfiguration> pending;
  if (!scan_state_.running()) {
    const std::lock_guard<std::mutex> lock(reconfigure_mutex_);
    if (pending_configuration_.has_value()) {
      pending = std::move(pending_configuration_);
      pending_configuration_.reset();
      active_reconfigure_operation_id_.store(
          pending->operation_id, std::memory_order_release);
      pending_reconfigure_operation_id_.store(
          0U, std::memory_order_release);
    }
  }
  if (pending.has_value()) {
    device_ = pending->device;
    wifi_ = pending->wifi;
    status_.configured = !wifi_.ssid.empty();
    status_.ssid = wifi_.ssid;
    backoff_ = ExponentialReconnectBackoff(
        wifi_.reconnect_initial_ms, wifi_.reconnect_max_ms);
    if (pending->operation_id != 0U) {
      operations_.mark_running(
          pending->operation_id,
          now_ms,
          "Configuration persisted; applying Wi-Fi settings");
    }
    if (!WiFi.setHostname(device_.hostname.c_str())) {
      const auto error = network_error(
          "Persisted hostname could not be applied to the Wi-Fi station",
          false);
      status_.last_error = error;
      fail_reconfigure(now_ms, error);
    }
    if (!status_.configured) {
      provisioning_.initialize(false);
      leave_connected();
      // The persisted configuration no longer authorizes the previous STA
      // association. Drop that live link before treating setup-AP startup as
      // the terminal radio result for this operation.
      WiFi.disconnect(false, false);
      const bool setup_started = start_setup_ap();
      status_.state = setup_started
          ? WifiState::unconfigured
          : WifiState::fault;
      if (setup_started) {
        succeed_reconfigure(
            now_ms,
            "Wi-Fi configuration cleared and setup access point is running");
      } else {
        const auto error = network_error(
            "Persisted Wi-Fi configuration was cleared, but the setup access point could not be started",
            false);
        status_.last_error = error;
        fail_reconfigure(now_ms, error);
      }
      request_scan();
    } else {
      leave_connected();
      start_connection(now_ms);
    }
  }
  provisioning_.poll(now_ms);
  if (!provisioning_.active() && setup_ap_running_ &&
      !scan_state_.running()) {
    stop_setup_ap();
  }
  if (setup_ap_running_) captive_dns_.processNextRequest();
  poll_scan(now_ms, status_.state != WifiState::connecting);
  if (setup_mode_pending_ && !scan_state_.running()) {
    complete_setup_mode_request(now_ms);
  }
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
      static_cast<std::int32_t>(now_ms - status_.next_reconnect_at_ms) >= 0 &&
      !scan_state_.running()) {
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

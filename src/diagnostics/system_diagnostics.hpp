#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "network/wifi_service.hpp"
#include "services/scale_service.hpp"

namespace opentag::hardware::display {
class Wt32Display;
}

namespace opentag::platform::storage {
class StorageService;
}

namespace opentag::diagnostics {

enum class WebsocketDisconnectReason : std::uint8_t {
  none,
  peer_or_network_closed,
  server_stopped,
  send_failure,
  protocol_rejected,
};

[[nodiscard]] constexpr const char* to_string(
    WebsocketDisconnectReason reason) {
  switch (reason) {
    case WebsocketDisconnectReason::none: return "none";
    case WebsocketDisconnectReason::peer_or_network_closed:
      return "peer-or-network-closed";
    case WebsocketDisconnectReason::server_stopped: return "server-stopped";
    case WebsocketDisconnectReason::send_failure: return "send-failure";
    case WebsocketDisconnectReason::protocol_rejected:
      return "protocol-rejected";
  }
  return "unknown";
}

struct TransportDiagnosticSnapshot {
  bool http_server_running{false};
  std::uint32_t active_http_sessions{0U};
  std::uint32_t maximum_observed_http_sessions{0U};
  std::uint32_t http_session_open_count{0U};
  std::uint32_t http_session_close_count{0U};
  std::uint32_t websocket_clients{0U};
  std::uint32_t websocket_open_count{0U};
  std::uint32_t websocket_disconnect_count{0U};
  std::uint32_t websocket_send_failure_count{0U};
  std::uint32_t websocket_dropped_event_count{0U};
  WebsocketDisconnectReason last_websocket_disconnect_reason{
      WebsocketDisconnectReason::none};
};

// Allocation-free counters shared by the HTTPD task, its asynchronous send
// callbacks, and the network owner. Counts are lifetime-since-boot values;
// only the live gauges are cleared when the server stops.
class TransportDiagnosticStore final {
 public:
  void set_http_server_running(bool running) {
    http_server_running_.store(running, std::memory_order_release);
    if (!running) {
      active_http_sessions_.store(0U, std::memory_order_relaxed);
      websocket_clients_.store(0U, std::memory_order_relaxed);
    }
  }

  void http_session_opened() {
    http_session_open_count_.fetch_add(1U, std::memory_order_relaxed);
    const auto active =
        active_http_sessions_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    update_maximum(maximum_observed_http_sessions_, active);
  }

  void http_session_closed() {
    http_session_close_count_.fetch_add(1U, std::memory_order_relaxed);
    decrement_saturating(active_http_sessions_);
  }

  void websocket_opened() {
    websocket_open_count_.fetch_add(1U, std::memory_order_relaxed);
    websocket_clients_.fetch_add(1U, std::memory_order_relaxed);
  }

  void websocket_disconnected(WebsocketDisconnectReason reason) {
    decrement_saturating(websocket_clients_);
    websocket_disconnect_count_.fetch_add(1U, std::memory_order_relaxed);
    last_websocket_disconnect_reason_.store(
        reason == WebsocketDisconnectReason::none
            ? WebsocketDisconnectReason::peer_or_network_closed
            : reason,
        std::memory_order_relaxed);
  }

  // A dropped event is one client-bound frame that could not be queued or
  // completed. Coalesced state is retried and is deliberately not counted.
  void websocket_send_failed() {
    websocket_send_failure_count_.fetch_add(1U, std::memory_order_relaxed);
    websocket_dropped_event_count_.fetch_add(1U, std::memory_order_relaxed);
  }

  void websocket_event_dropped() {
    websocket_dropped_event_count_.fetch_add(1U, std::memory_order_relaxed);
  }

  [[nodiscard]] TransportDiagnosticSnapshot snapshot() const {
    TransportDiagnosticSnapshot result;
    result.http_server_running =
        http_server_running_.load(std::memory_order_acquire);
    result.active_http_sessions =
        active_http_sessions_.load(std::memory_order_relaxed);
    result.maximum_observed_http_sessions =
        maximum_observed_http_sessions_.load(std::memory_order_relaxed);
    result.http_session_open_count =
        http_session_open_count_.load(std::memory_order_relaxed);
    result.http_session_close_count =
        http_session_close_count_.load(std::memory_order_relaxed);
    result.websocket_clients =
        websocket_clients_.load(std::memory_order_relaxed);
    result.websocket_open_count =
        websocket_open_count_.load(std::memory_order_relaxed);
    result.websocket_disconnect_count =
        websocket_disconnect_count_.load(std::memory_order_relaxed);
    result.websocket_send_failure_count =
        websocket_send_failure_count_.load(std::memory_order_relaxed);
    result.websocket_dropped_event_count =
        websocket_dropped_event_count_.load(std::memory_order_relaxed);
    result.last_websocket_disconnect_reason =
        last_websocket_disconnect_reason_.load(std::memory_order_relaxed);
    return result;
  }

 private:
  static void decrement_saturating(std::atomic<std::uint32_t>& value) {
    auto current = value.load(std::memory_order_relaxed);
    while (current != 0U &&
           !value.compare_exchange_weak(
               current,
               current - 1U,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {}
  }

  static void update_maximum(
      std::atomic<std::uint32_t>& maximum,
      std::uint32_t candidate) {
    auto current = maximum.load(std::memory_order_relaxed);
    while (candidate > current &&
           !maximum.compare_exchange_weak(
               current,
               candidate,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {}
  }

  std::atomic_bool http_server_running_{false};
  std::atomic<std::uint32_t> active_http_sessions_{0U};
  std::atomic<std::uint32_t> maximum_observed_http_sessions_{0U};
  std::atomic<std::uint32_t> http_session_open_count_{0U};
  std::atomic<std::uint32_t> http_session_close_count_{0U};
  std::atomic<std::uint32_t> websocket_clients_{0U};
  std::atomic<std::uint32_t> websocket_open_count_{0U};
  std::atomic<std::uint32_t> websocket_disconnect_count_{0U};
  std::atomic<std::uint32_t> websocket_send_failure_count_{0U};
  std::atomic<std::uint32_t> websocket_dropped_event_count_{0U};
  std::atomic<WebsocketDisconnectReason> last_websocket_disconnect_reason_{
      WebsocketDisconnectReason::none};
};

struct TaskStackMargins {
  std::uint32_t loop_free_bytes{0U};
  std::uint32_t network_free_bytes{0U};
  std::uint32_t ui_free_bytes{0U};
  std::uint32_t configuration_free_bytes{0U};
  std::uint32_t backend_free_bytes{0U};
  std::uint32_t scale_free_bytes{0U};
  std::uint32_t device_control_free_bytes{0U};
  std::uint32_t ota_free_bytes{0U};
  std::uint32_t httpd_free_bytes{0U};
};

struct ScaleDiagnosticSnapshot {
  std::uint64_t scale_revision{0U};
  bool scale_task_running{false};
  services::ScaleState scale_state{services::ScaleState::uninitialized};
  bool scale_adc_ready{false};
  bool scale_calibrated{false};
  bool scale_calibration_loaded{false};
  bool scale_calibration_matches_hardware{false};
  bool scale_persistence_available{true};
  bool scale_tare_ready{false};
  std::int32_t scale_tare_zero_offset_counts{0};
  bool scale_weight_available{false};
  bool scale_stable{false};
  bool scale_negative{false};
  bool scale_overload{false};
  bool scale_creep_warning{false};
  std::int32_t scale_raw_counts{0};
  std::int32_t scale_filtered_counts{0};
  std::int32_t scale_gross_milligrams{0};
  std::int32_t scale_zero_offset_counts{0};
  std::int32_t scale_factor_millicounts_per_gram{0};
  std::string scale_load_cell_model{"YZC-133"};
  float scale_rated_capacity_grams{5000.0F};
  float scale_configured_overload_ratio{1.10F};
  float scale_overload_threshold_grams{5500.0F};
  float scale_calibration_reference_grams{0.0F};
  float scale_calibration_capacity_grams{0.0F};
};

class ScaleDiagnosticStore final {
 public:
  void set_task_running(bool running) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.scale_task_running == running) return;
    state_.scale_task_running = running;
    ++state_.scale_revision;
  }

  void update(
      const services::ScaleStatus& status,
      const std::optional<services::ScaleCalibration>& calibration,
      const services::ScaleHardwareSettings& hardware) {
    ScaleDiagnosticSnapshot next;
    next.scale_state = status.state;
    next.scale_adc_ready = status.adc_ready;
    next.scale_persistence_available = status.persistence_available;
    next.scale_tare_ready = status.tare_ready;
    next.scale_tare_zero_offset_counts = status.tare_zero_offset_counts;
    next.scale_stable = status.sample.stable;
    next.scale_negative = status.sample.negative;
    next.scale_overload = status.sample.overload;
    next.scale_creep_warning = status.sample.creep_warning;
    next.scale_raw_counts = status.sample.raw_counts;
    next.scale_filtered_counts = bounded_int32(status.sample.filtered_raw_counts);

    next.scale_weight_available = status.sample.gross_grams.has_value() &&
        std::isfinite(*status.sample.gross_grams);
    next.scale_gross_milligrams = next.scale_weight_available
                                      ? bounded_int32(
                                            *status.sample.gross_grams * 1000.0)
                                      : 0;

    next.scale_load_cell_model = hardware.load_cell_model;
    next.scale_rated_capacity_grams =
        finite_positive(hardware.rated_capacity_grams)
            ? hardware.rated_capacity_grams
            : 0.0F;
    next.scale_configured_overload_ratio =
        finite_positive(hardware.overload_ratio) ? hardware.overload_ratio : 0.0F;
    const double overload_threshold =
        static_cast<double>(next.scale_rated_capacity_grams) *
        next.scale_configured_overload_ratio;
    next.scale_overload_threshold_grams =
        std::isfinite(overload_threshold)
            ? static_cast<float>(overload_threshold)
            : 0.0F;

    next.scale_calibrated = calibration.has_value();
    next.scale_calibration_loaded =
        status.calibration_loaded && calibration.has_value();
    if (calibration.has_value()) {
      next.scale_zero_offset_counts = calibration->zero_offset_counts;
      next.scale_factor_millicounts_per_gram =
          bounded_int32(calibration->counts_per_gram * 1000.0);
      next.scale_calibration_reference_grams =
          finite_positive(calibration->reference_grams)
              ? calibration->reference_grams
              : 0.0F;
      next.scale_calibration_capacity_grams =
          finite_positive(calibration->load_cell_capacity_grams)
              ? calibration->load_cell_capacity_grams
              : 0.0F;
      next.scale_calibration_matches_hardware =
          next.scale_calibration_capacity_grams > 0.0F &&
          next.scale_rated_capacity_grams > 0.0F &&
          std::fabs(
              next.scale_calibration_capacity_grams -
              next.scale_rated_capacity_grams) <= 0.01F;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    next.scale_task_running = state_.scale_task_running;
    next.scale_revision = state_.scale_revision + 1U;
    state_ = std::move(next);
  }

  [[nodiscard]] ScaleDiagnosticSnapshot snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

 private:
  [[nodiscard]] static bool finite_positive(float value) {
    return std::isfinite(value) && value > 0.0F;
  }

  [[nodiscard]] static std::int32_t bounded_int32(double value) {
    if (!std::isfinite(value)) return 0;
    if (value <= static_cast<double>(std::numeric_limits<std::int32_t>::min())) {
      return std::numeric_limits<std::int32_t>::min();
    }
    if (value >= static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
      return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(std::llround(value));
  }

  mutable std::mutex mutex_;
  ScaleDiagnosticSnapshot state_;
};

struct SystemSnapshot : ScaleDiagnosticSnapshot {
  const char* reset_reason{"unknown"};
  std::uint32_t uptime_ms{0};
  std::uint32_t free_heap_bytes{0};
  std::uint32_t minimum_free_heap_bytes{0};
  std::uint32_t largest_free_internal_block_bytes{0};
  std::uint32_t psram_total_bytes{0};
  std::uint32_t psram_free_bytes{0};
  std::uint32_t minimum_free_psram_bytes{0};
  std::uint32_t largest_free_psram_block_bytes{0};
  std::uint32_t boot_count{0};
  std::uint8_t crash_streak{0};
  TaskStackMargins task_stacks;
  TransportDiagnosticSnapshot transport;
  bool display_ready{false};
  bool touch_configured{false};
  bool nvs_ready{false};
  bool filesystem_ready{false};
  bool coredump_partition_present{false};
  bool ui_task_running{false};
  bool network_task_running{false};
  network::WifiState wifi_state{network::WifiState::uninitialized};
  bool wifi_configured{false};
  bool wifi_connected{false};
  bool wifi_scan_running{false};
  bool mdns_ready{false};
  bool ntp_ready{false};
  std::string wifi_ssid;
  std::int32_t wifi_rssi_dbm{0};
  std::string ip_address;
  std::string gateway;
  std::string dns_server;
  std::uint32_t wifi_reconnect_attempts{0U};
  bool provisioning_active{false};
  bool provisioning_grace_active{false};
  network::ProvisioningReason provisioning_reason{
      network::ProvisioningReason::none};
  std::uint32_t provisioning_failures{0U};
  std::uint32_t provisioning_grace_remaining_ms{0U};
  std::string setup_ap_ssid;
  std::string setup_ap_ip;
  std::uint32_t wifi_scan_generation{0U};
  std::uint32_t wifi_scan_attempt_generation{0U};
  std::uint32_t wifi_scan_result_attempt_generation{0U};
  std::vector<network::WifiNetwork> wifi_scan_results;
  std::optional<core::Error> wifi_scan_error;
  std::optional<core::Error> wifi_last_error;
};

class SystemDiagnostics {
 public:
  SystemDiagnostics(
      const hardware::display::Wt32Display& display,
      const platform::storage::StorageService& storage)
      : display_(display), storage_(storage) {}

  [[nodiscard]] SystemSnapshot snapshot(std::uint32_t now_ms) const;
  void set_ui_task_running(bool running) {
    ui_task_running_.store(running, std::memory_order_relaxed);
  }
  void set_scale_task_running(bool running) {
    scale_diagnostics_.set_task_running(running);
  }
  void set_scale_status(
      const services::ScaleStatus& status,
      const std::optional<services::ScaleCalibration>& calibration,
      const services::ScaleHardwareSettings& hardware);
  [[nodiscard]] ScaleDiagnosticSnapshot scale_snapshot() const {
    return scale_diagnostics_.snapshot();
  }
  void set_network_task_running(bool running) {
    network_task_running_.store(running, std::memory_order_relaxed);
  }
  void set_network_status(const network::WifiStatus& status);
  void set_task_stack_margins(const TaskStackMargins& margins) {
    const std::lock_guard<std::mutex> lock(stack_mutex_);
    task_stack_margins_ = margins;
  }
  [[nodiscard]] TaskStackMargins task_stack_margins() const {
    const std::lock_guard<std::mutex> lock(stack_mutex_);
    return task_stack_margins_;
  }
  [[nodiscard]] TransportDiagnosticStore& transport_diagnostics() {
    return transport_diagnostics_;
  }
  [[nodiscard]] static const char* current_reset_reason();

 private:
  const hardware::display::Wt32Display& display_;
  const platform::storage::StorageService& storage_;
  std::atomic_bool ui_task_running_{false};
  ScaleDiagnosticStore scale_diagnostics_;
  std::atomic_bool network_task_running_{false};
  mutable std::mutex network_mutex_;
  network::WifiStatus network_status_;
  mutable std::mutex stack_mutex_;
  TaskStackMargins task_stack_margins_;
  TransportDiagnosticStore transport_diagnostics_;
};

}  // namespace opentag::diagnostics

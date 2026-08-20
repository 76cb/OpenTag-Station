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

struct ScaleDiagnosticSnapshot {
  std::uint64_t scale_revision{0U};
  bool scale_task_running{false};
  services::ScaleState scale_state{services::ScaleState::uninitialized};
  bool scale_adc_ready{false};
  bool scale_calibrated{false};
  bool scale_calibration_loaded{false};
  bool scale_calibration_matches_hardware{false};
  bool scale_persistence_available{true};
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
  std::uint32_t psram_total_bytes{0};
  std::uint32_t psram_free_bytes{0};
  std::uint32_t boot_count{0};
  std::uint8_t crash_streak{0};
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
  void set_network_task_running(bool running) {
    network_task_running_.store(running, std::memory_order_relaxed);
  }
  void set_network_status(const network::WifiStatus& status);
  [[nodiscard]] static const char* current_reset_reason();

 private:
  const hardware::display::Wt32Display& display_;
  const platform::storage::StorageService& storage_;
  std::atomic_bool ui_task_running_{false};
  ScaleDiagnosticStore scale_diagnostics_;
  std::atomic_bool network_task_running_{false};
  mutable std::mutex network_mutex_;
  network::WifiStatus network_status_;
};

}  // namespace opentag::diagnostics

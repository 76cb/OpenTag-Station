#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <Preferences.h>

#include "config/configuration_service.hpp"
#include "services/scale_service.hpp"

namespace opentag::platform::storage {

struct StorageStatus {
  bool nvs_ready{false};
  bool filesystem_ready{false};
  bool coredump_partition_present{false};
  std::uint32_t boot_count{0};
  std::uint8_t crash_streak{0};
  std::uint64_t filesystem_total_bytes{0};
  std::uint64_t filesystem_used_bytes{0};
};

class StorageService final :
    public config::IConfigurationDocumentStore,
    public services::IScaleCalibrationStore {
 public:
  bool initialize(std::uint32_t now_ms);
  void poll(std::uint32_t now_ms);
  [[nodiscard]] const StorageStatus& status() const { return status_; }
  [[nodiscard]] core::Result<void> factory_reset_device_data();
  [[nodiscard]] bool factory_reset_recovery_pending() const {
    return reset_in_progress_.load(std::memory_order_acquire);
  }

  [[nodiscard]] core::Result<std::optional<services::ScaleCalibration>>
  load_scale_calibration() override;
  [[nodiscard]] core::Result<void> save_scale_calibration(
      const services::ScaleCalibration& calibration) override;
  [[nodiscard]] core::Result<void> clear_scale_calibration() override;
  [[nodiscard]] core::Result<std::optional<std::string>>
  load_configuration_document() override;
  [[nodiscard]] core::Result<std::optional<std::string>>
  load_configuration_backup_document() override;
  [[nodiscard]] core::Result<void> save_configuration_document(
      const std::string& document) override;

 private:
  static constexpr std::uint32_t healthy_boot_after_ms = 30000U;

  void mark_boot_healthy();

  Preferences preferences_;
  Preferences control_preferences_;
  std::mutex preferences_mutex_;
  StorageStatus status_;
  std::atomic_bool reset_in_progress_{false};
  std::uint32_t boot_started_ms_{0};
  bool boot_pending_{false};
};

}  // namespace opentag::platform::storage

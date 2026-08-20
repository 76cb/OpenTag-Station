#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "core/result.hpp"

namespace opentag::services {

struct ScaleHardwareSettings {
  std::string load_cell_model{"YZC-133"};
  float rated_capacity_grams{5000.0F};
  float overload_ratio{1.10F};

  [[nodiscard]] core::Result<void> validate() const;
};

struct ScaleCalibration {
  static constexpr std::uint32_t current_schema = 1U;

  std::uint32_t schema_version{current_schema};
  std::int32_t zero_offset_counts{0};
  double counts_per_gram{0.0};
  float reference_grams{0.0F};
  float load_cell_capacity_grams{5000.0F};

  [[nodiscard]] core::Result<void> validate() const;
};

class IScaleCalibrationStore {
 public:
  virtual ~IScaleCalibrationStore() = default;
  [[nodiscard]] virtual core::Result<std::optional<ScaleCalibration>> load_scale_calibration() = 0;
  [[nodiscard]] virtual core::Result<void> save_scale_calibration(
      const ScaleCalibration& calibration) = 0;
  [[nodiscard]] virtual core::Result<void> clear_scale_calibration() = 0;
};

class IScaleAdc {
 public:
  virtual ~IScaleAdc() = default;
  [[nodiscard]] virtual core::Result<void> initialize(std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> internal_calibrate(std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<bool> sample_ready() = 0;
  [[nodiscard]] virtual core::Result<std::int32_t> read_raw() = 0;
};

enum class ScaleState : std::uint8_t {
  uninitialized,
  starting,
  calibration_required,
  sampling,
  disconnected,
  fault,
};

[[nodiscard]] const char* to_string(ScaleState state);

struct ScaleProcessingConfig {
  static constexpr std::size_t maximum_filter_window = 32U;

  std::size_t filter_window{10U};
  float stability_threshold_grams{2.0F};
  std::uint32_t stability_duration_ms{1500U};
  std::uint32_t sample_timeout_ms{1500U};
  std::int32_t tare_stability_counts{500};
  float negative_tolerance_grams{2.0F};
  float overload_ratio{1.10F};
  float adc_overload_ratio{0.98F};
  float creep_warning_grams{5.0F};

  [[nodiscard]] core::Result<void> validate() const;
};

struct ScaleSample {
  std::int32_t raw_counts{0};
  double filtered_raw_counts{0.0};
  std::optional<float> gross_grams;
  std::optional<float> drift_from_stable_grams;
  bool stable{false};
  bool negative{false};
  bool overload{false};
  bool creep_warning{false};
  std::uint32_t sampled_at_ms{0};
};

struct ScaleStatus {
  ScaleState state{ScaleState::uninitialized};
  bool adc_ready{false};
  bool calibration_loaded{false};
  bool persistence_available{true};
  std::size_t samples_in_filter{0};
  ScaleSample sample;
  std::optional<core::Error> last_error;
};

class ScaleService {
 public:
  ScaleService(
      IScaleAdc& adc,
      IScaleCalibrationStore& store,
      ScaleProcessingConfig config = {});

  [[nodiscard]] core::Result<void> initialize(
      std::uint32_t now_ms,
      std::uint32_t operation_timeout_ms);
  [[nodiscard]] core::Result<void> configure_hardware(
      const ScaleHardwareSettings& settings);
  [[nodiscard]] core::Result<void> reconfigure_hardware(
      const ScaleHardwareSettings& settings);
  [[nodiscard]] core::Result<bool> poll(std::uint32_t now_ms);
  [[nodiscard]] core::Result<void> tare();
  [[nodiscard]] core::Result<ScaleCalibration> calibrate(
      float reference_grams,
      float load_cell_capacity_grams);

  [[nodiscard]] const ScaleStatus& status() const { return status_; }
  [[nodiscard]] const std::optional<ScaleCalibration>& calibration() const {
    return calibration_;
  }
  [[nodiscard]] const ScaleHardwareSettings& hardware_settings() const {
    return hardware_settings_;
  }

 private:
  [[nodiscard]] bool raw_filter_stable() const;
  void push_sample(std::int32_t raw_counts, std::uint32_t now_ms);
  void reset_filter();
  void set_error(core::Error error, ScaleState state);

  IScaleAdc& adc_;
  IScaleCalibrationStore& store_;
  ScaleProcessingConfig config_;
  ScaleHardwareSettings hardware_settings_;
  bool hardware_settings_explicit_{false};
  ScaleStatus status_;
  std::optional<ScaleCalibration> calibration_;
  std::optional<std::int32_t> pending_zero_offset_counts_;
  std::array<std::int32_t, ScaleProcessingConfig::maximum_filter_window> samples_{};
  std::size_t next_sample_{0U};
  std::size_t sample_count_{0U};
  std::uint32_t last_sample_ms_{0U};
  std::optional<std::uint32_t> stable_candidate_since_ms_;
  std::optional<float> stable_baseline_grams_;
};

}  // namespace opentag::services

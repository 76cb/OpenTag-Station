#include "services/scale_service.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace opentag::services {
namespace {

core::Error configuration_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

core::Error unavailable(const std::string& message, bool retryable = true) {
  return {core::ErrorCategory::scale_unavailable, message, retryable};
}

core::Error unstable(const std::string& message) {
  return {core::ErrorCategory::scale_unstable, message, true};
}

bool capacities_match(float left, float right) {
  return std::fabs(left - right) <= 0.01F;
}

}  // namespace

const char* to_string(ScaleState state) {
  switch (state) {
    case ScaleState::uninitialized: return "uninitialized";
    case ScaleState::starting: return "starting";
    case ScaleState::calibration_required: return "calibration required";
    case ScaleState::sampling: return "sampling";
    case ScaleState::disconnected: return "disconnected";
    case ScaleState::fault: return "fault";
  }
  return "unknown";
}

core::Result<void> ScaleHardwareSettings::validate() const {
  const bool supported_capacity =
      rated_capacity_grams == 2000.0F || rated_capacity_grams == 5000.0F;
  if (load_cell_model != "YZC-133" ||
      !std::isfinite(rated_capacity_grams) || !supported_capacity ||
      !std::isfinite(overload_ratio) || overload_ratio <= 1.0F ||
      overload_ratio > 2.0F) {
    return core::Result<void>::failure(
        configuration_error("scale hardware settings are invalid"));
  }
  return core::Result<void>::success();
}

core::Result<void> ScaleCalibration::validate() const {
  if (schema_version != current_schema) {
    return core::Result<void>::failure(configuration_error("unsupported scale calibration schema"));
  }
  if (!std::isfinite(counts_per_gram) || std::fabs(counts_per_gram) < 0.001) {
    return core::Result<void>::failure(configuration_error("scale calibration factor is invalid"));
  }
  if (!std::isfinite(reference_grams) || reference_grams <= 0.0F ||
      !std::isfinite(load_cell_capacity_grams) ||
      load_cell_capacity_grams < reference_grams) {
    return core::Result<void>::failure(configuration_error("scale calibration weights are invalid"));
  }
  return core::Result<void>::success();
}

core::Result<void> ScaleProcessingConfig::validate() const {
  if (filter_window < 3U || filter_window > maximum_filter_window ||
      !std::isfinite(stability_threshold_grams) || stability_threshold_grams < 0.0F ||
      stability_duration_ms == 0U || sample_timeout_ms == 0U ||
      tare_stability_counts < 0 || !std::isfinite(negative_tolerance_grams) ||
      negative_tolerance_grams < 0.0F || !std::isfinite(overload_ratio) ||
      overload_ratio <= 1.0F || !std::isfinite(adc_overload_ratio) ||
      adc_overload_ratio <= 0.0F || adc_overload_ratio > 1.0F ||
      !std::isfinite(creep_warning_grams) || creep_warning_grams < 0.0F) {
    return core::Result<void>::failure(configuration_error("scale processing configuration is invalid"));
  }
  return core::Result<void>::success();
}

ScaleService::ScaleService(
    IScaleAdc& adc,
    IScaleCalibrationStore& store,
    ScaleProcessingConfig config)
    : adc_(adc), store_(store), config_(config) {}

core::Result<void> ScaleService::configure_hardware(
    const ScaleHardwareSettings& settings) {
  const auto valid = settings.validate();
  if (!valid.ok()) return valid;
  if (status_.state != ScaleState::uninitialized) {
    return core::Result<void>::failure(
        configuration_error("scale hardware settings cannot change after initialization"));
  }
  auto processing = config_;
  processing.overload_ratio = settings.overload_ratio;
  const auto processing_valid = processing.validate();
  if (!processing_valid.ok()) return processing_valid;
  hardware_settings_ = settings;
  hardware_settings_explicit_ = true;
  config_ = processing;
  return core::Result<void>::success();
}

core::Result<void> ScaleService::reconfigure_hardware(
    const ScaleHardwareSettings& settings) {
  status_ = {};
  calibration_.reset();
  pending_zero_offset_counts_.reset();
  reset_filter();
  hardware_settings_explicit_ = false;
  return configure_hardware(settings);
}

void ScaleService::set_error(core::Error error, ScaleState state) {
  status_.last_error = std::move(error);
  status_.state = state;
  status_.sample.stable = false;
}

core::Result<void> ScaleService::initialize(
    std::uint32_t now_ms,
    std::uint32_t operation_timeout_ms) {
  status_ = {};
  status_.state = ScaleState::starting;
  calibration_.reset();
  pending_zero_offset_counts_.reset();
  reset_filter();
  const auto config_status = config_.validate();
  const auto hardware_status = hardware_settings_.validate();
  if (!config_status.ok() || !hardware_status.ok() || operation_timeout_ms == 0U) {
    const auto error = !config_status.ok()
                           ? config_status.error()
                           : !hardware_status.ok()
                                 ? hardware_status.error()
                                 : configuration_error(
                                       "scale operation timeout must be non-zero");
    set_error(error, ScaleState::fault);
    return core::Result<void>::failure(error);
  }

  const auto stored = store_.load_scale_calibration();
  if (!stored.ok()) {
    status_.persistence_available = false;
    status_.last_error = stored.error();
  } else if (stored.value().has_value()) {
    const auto valid = stored.value()->validate();
    const bool capacity_matches = !hardware_settings_explicit_ ||
        capacities_match(
            stored.value()->load_cell_capacity_grams,
            hardware_settings_.rated_capacity_grams);
    if (valid.ok() && capacity_matches) {
      calibration_ = *stored.value();
      if (!hardware_settings_explicit_) {
        hardware_settings_.rated_capacity_grams =
            calibration_->load_cell_capacity_grams;
      }
      pending_zero_offset_counts_ = calibration_->zero_offset_counts;
      status_.tare_ready = true;
      status_.tare_zero_offset_counts = calibration_->zero_offset_counts;
      status_.calibration_loaded = true;
    } else {
      status_.persistence_available = false;
      status_.last_error = valid.ok()
                               ? configuration_error(
                                     "scale calibration capacity does not match hardware settings")
                               : valid.error();
    }
  }

  const auto initialized = adc_.initialize(operation_timeout_ms);
  if (!initialized.ok()) {
    set_error(initialized.error(), ScaleState::disconnected);
    return core::Result<void>::failure(initialized.error());
  }
  const auto internal = adc_.internal_calibrate(operation_timeout_ms);
  if (!internal.ok()) {
    set_error(internal.error(), ScaleState::fault);
    return core::Result<void>::failure(internal.error());
  }

  status_.adc_ready = true;
  status_.state = calibration_.has_value()
                      ? ScaleState::sampling
                      : ScaleState::calibration_required;
  if (status_.persistence_available) status_.last_error.reset();
  last_sample_ms_ = now_ms;
  return core::Result<void>::success();
}

void ScaleService::reset_filter() {
  samples_.fill(0);
  next_sample_ = 0U;
  sample_count_ = 0U;
  stable_candidate_since_ms_.reset();
  stable_baseline_grams_.reset();
  status_.samples_in_filter = 0U;
  status_.sample = {};
}

bool ScaleService::raw_filter_stable() const {
  if (sample_count_ < config_.filter_window) return false;
  const auto begin = samples_.begin();
  const auto end = begin + static_cast<std::ptrdiff_t>(sample_count_);
  const auto bounds = std::minmax_element(begin, end);
  return static_cast<std::int64_t>(*bounds.second) - *bounds.first <=
      config_.tare_stability_counts;
}

void ScaleService::push_sample(std::int32_t raw_counts, std::uint32_t now_ms) {
  samples_[next_sample_] = raw_counts;
  next_sample_ = (next_sample_ + 1U) % config_.filter_window;
  sample_count_ = std::min(sample_count_ + 1U, config_.filter_window);
  status_.samples_in_filter = sample_count_;

  double total = 0.0;
  std::int32_t minimum = std::numeric_limits<std::int32_t>::max();
  std::int32_t maximum = std::numeric_limits<std::int32_t>::min();
  for (std::size_t index = 0U; index < sample_count_; ++index) {
    total += samples_[index];
    minimum = std::min(minimum, samples_[index]);
    maximum = std::max(maximum, samples_[index]);
  }
  status_.sample.raw_counts = raw_counts;
  status_.sample.filtered_raw_counts = total / static_cast<double>(sample_count_);
  status_.sample.sampled_at_ms = now_ms;
  status_.sample.stable = false;
  status_.sample.negative = false;
  status_.sample.creep_warning = false;
  status_.sample.drift_from_stable_grams.reset();
  const double adc_limit = 8388607.0 * config_.adc_overload_ratio;
  status_.sample.overload = std::fabs(status_.sample.filtered_raw_counts) >= adc_limit;

  if (!calibration_.has_value()) {
    status_.sample.gross_grams.reset();
    stable_candidate_since_ms_.reset();
    stable_baseline_grams_.reset();
    return;
  }

  const double grams =
      (status_.sample.filtered_raw_counts - calibration_->zero_offset_counts) /
      calibration_->counts_per_gram;
  status_.sample.gross_grams = static_cast<float>(grams);
  status_.sample.negative = grams < -config_.negative_tolerance_grams;
  status_.sample.overload = status_.sample.overload ||
      std::fabs(grams) > calibration_->load_cell_capacity_grams * config_.overload_ratio;

  const double spread_grams =
      static_cast<double>(static_cast<std::int64_t>(maximum) - minimum) /
      std::fabs(calibration_->counts_per_gram);
  const bool candidate = sample_count_ == config_.filter_window &&
      spread_grams <= config_.stability_threshold_grams &&
      !status_.sample.negative && !status_.sample.overload;
  if (!candidate) {
    stable_candidate_since_ms_.reset();
    stable_baseline_grams_.reset();
    return;
  }
  if (!stable_candidate_since_ms_.has_value()) stable_candidate_since_ms_ = now_ms;
  status_.sample.stable =
      static_cast<std::uint32_t>(now_ms - *stable_candidate_since_ms_) >=
      config_.stability_duration_ms;
  if (!status_.sample.stable) return;

  if (!stable_baseline_grams_.has_value()) {
    stable_baseline_grams_ = *status_.sample.gross_grams;
  }
  status_.sample.drift_from_stable_grams =
      *status_.sample.gross_grams - *stable_baseline_grams_;
  status_.sample.creep_warning =
      std::fabs(*status_.sample.drift_from_stable_grams) > config_.creep_warning_grams;
}

core::Result<bool> ScaleService::poll(std::uint32_t now_ms) {
  if (!status_.adc_ready) {
    return core::Result<bool>::failure(unavailable("NAU7802 is not initialized"));
  }
  const auto ready = adc_.sample_ready();
  if (!ready.ok()) {
    set_error(ready.error(), ScaleState::disconnected);
    return core::Result<bool>::failure(ready.error());
  }
  if (!ready.value()) {
    if (static_cast<std::uint32_t>(now_ms - last_sample_ms_) > config_.sample_timeout_ms) {
      const auto error = unavailable("NAU7802 sample timeout");
      set_error(error, ScaleState::disconnected);
      return core::Result<bool>::failure(error);
    }
    return core::Result<bool>::success(false);
  }
  const auto raw = adc_.read_raw();
  if (!raw.ok()) {
    set_error(raw.error(), ScaleState::disconnected);
    return core::Result<bool>::failure(raw.error());
  }
  last_sample_ms_ = now_ms;
  status_.adc_ready = true;
  status_.state = calibration_.has_value()
                      ? ScaleState::sampling
                      : ScaleState::calibration_required;
  status_.last_error.reset();
  push_sample(raw.value(), now_ms);
  return core::Result<bool>::success(true);
}

core::Result<void> ScaleService::tare() {
  if (!raw_filter_stable()) {
    return core::Result<void>::failure(unstable("scale must be stable before tare"));
  }
  pending_zero_offset_counts_ =
      static_cast<std::int32_t>(std::llround(status_.sample.filtered_raw_counts));
  status_.tare_ready = true;
  status_.tare_zero_offset_counts = *pending_zero_offset_counts_;
  if (calibration_.has_value()) {
    calibration_->zero_offset_counts = *pending_zero_offset_counts_;
    const auto saved = store_.save_scale_calibration(*calibration_);
    if (!saved.ok()) {
      status_.persistence_available = false;
      status_.last_error = saved.error();
      return saved;
    }
    status_.persistence_available = true;
  }
  reset_filter();
  return core::Result<void>::success();
}

core::Result<ScaleCalibration> ScaleService::calibrate(
    float reference_grams,
    float load_cell_capacity_grams) {
  if (!pending_zero_offset_counts_.has_value()) {
    return core::Result<ScaleCalibration>::failure(
        configuration_error("tare is required before scale calibration"));
  }
  if (!raw_filter_stable()) {
    return core::Result<ScaleCalibration>::failure(
        unstable("reference weight must be stable before calibration"));
  }
  if (!std::isfinite(reference_grams) || reference_grams <= 0.0F ||
      !std::isfinite(load_cell_capacity_grams) ||
      load_cell_capacity_grams < reference_grams) {
    return core::Result<ScaleCalibration>::failure(
        configuration_error("calibration reference/capacity is invalid"));
  }
  if (hardware_settings_explicit_ && !capacities_match(
          load_cell_capacity_grams, hardware_settings_.rated_capacity_grams)) {
    return core::Result<ScaleCalibration>::failure(configuration_error(
        "calibration capacity does not match configured scale hardware"));
  }
  const double delta =
      status_.sample.filtered_raw_counts - *pending_zero_offset_counts_;
  if (std::fabs(delta) < 100.0) {
    return core::Result<ScaleCalibration>::failure(
        configuration_error("calibration reference produced too few ADC counts"));
  }
  ScaleCalibration proposed;
  proposed.zero_offset_counts = *pending_zero_offset_counts_;
  status_.tare_ready = true;
  status_.tare_zero_offset_counts = *pending_zero_offset_counts_;
  proposed.counts_per_gram = delta / reference_grams;
  proposed.reference_grams = reference_grams;
  proposed.load_cell_capacity_grams = load_cell_capacity_grams;
  const auto valid = proposed.validate();
  if (!valid.ok()) return core::Result<ScaleCalibration>::failure(valid.error());

  calibration_ = proposed;
  if (!hardware_settings_explicit_) {
    hardware_settings_.rated_capacity_grams = load_cell_capacity_grams;
  }
  status_.calibration_loaded = true;
  status_.state = ScaleState::sampling;
  const auto saved = store_.save_scale_calibration(proposed);
  if (!saved.ok()) {
    status_.persistence_available = false;
    status_.last_error = saved.error();
    return core::Result<ScaleCalibration>::failure(saved.error());
  }
  status_.persistence_available = true;
  status_.last_error.reset();
  reset_filter();
  return core::Result<ScaleCalibration>::success(proposed);
}

}  // namespace opentag::services

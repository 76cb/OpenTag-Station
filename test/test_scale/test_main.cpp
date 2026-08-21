#include <unity.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "core/error.hpp"
#include "core/result.hpp"
#include "hardware/scale/i2c_diagnostics.hpp"
#include "services/scale_service.hpp"

namespace {

using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::services::IScaleAdc;
using opentag::services::IScaleCalibrationStore;
using opentag::services::ScaleCalibration;
using opentag::services::ScaleHardwareSettings;
using opentag::services::ScaleProcessingConfig;
using opentag::services::ScaleService;
using opentag::services::ScaleState;

opentag::core::Error injected_error(const char* message) {
  return {ErrorCategory::scale_unavailable, message, true};
}

class FakeAdc final : public IScaleAdc {
 public:
  bool initialize_fails{false};
  bool calibration_fails{false};
  bool ready_fails{false};
  bool read_fails{false};
  std::size_t initialize_calls{0U};
  std::size_t calibration_calls{0U};
  std::vector<std::int32_t> samples;

  Result<void> initialize(std::uint32_t) override {
    ++initialize_calls;
    if (initialize_fails) return Result<void>::failure(injected_error("init failed"));
    return Result<void>::success();
  }

  Result<void> internal_calibrate(std::uint32_t) override {
    ++calibration_calls;
    if (calibration_fails) {
      return Result<void>::failure(injected_error("internal calibration failed"));
    }
    return Result<void>::success();
  }

  Result<bool> sample_ready() override {
    if (ready_fails) return Result<bool>::failure(injected_error("ready failed"));
    return Result<bool>::success(!samples.empty());
  }

  Result<std::int32_t> read_raw() override {
    if (read_fails) {
      return Result<std::int32_t>::failure(injected_error("read failed"));
    }
    if (samples.empty()) {
      return Result<std::int32_t>::failure(injected_error("no sample"));
    }
    const auto result = samples.front();
    samples.erase(samples.begin());
    return Result<std::int32_t>::success(result);
  }

  void push(std::int32_t raw) { samples.push_back(raw); }
};

class FakeStore final : public IScaleCalibrationStore {
 public:
  std::optional<ScaleCalibration> stored;
  bool load_fails{false};
  bool save_fails{false};
  std::size_t save_calls{0U};

  Result<std::optional<ScaleCalibration>> load_scale_calibration() override {
    if (load_fails) {
      return Result<std::optional<ScaleCalibration>>::failure(
          {ErrorCategory::storage, "load failed", false});
    }
    return Result<std::optional<ScaleCalibration>>::success(stored);
  }

  Result<void> save_scale_calibration(const ScaleCalibration& calibration) override {
    ++save_calls;
    if (save_fails) {
      return Result<void>::failure({ErrorCategory::storage, "save failed", false});
    }
    stored = calibration;
    return Result<void>::success();
  }

  Result<void> clear_scale_calibration() override {
    stored.reset();
    return Result<void>::success();
  }
};

ScaleCalibration calibration(
    double counts_per_gram = 10.0,
    float capacity_grams = 1000.0F) {
  ScaleCalibration result;
  result.zero_offset_counts = 1000;
  result.counts_per_gram = counts_per_gram;
  result.reference_grams = 100.0F;
  result.load_cell_capacity_grams = capacity_grams;
  return result;
}

ScaleProcessingConfig test_config() {
  ScaleProcessingConfig result;
  result.filter_window = 3U;
  result.stability_threshold_grams = 0.5F;
  result.stability_duration_ms = 100U;
  result.sample_timeout_ms = 50U;
  result.tare_stability_counts = 5;
  result.negative_tolerance_grams = 1.0F;
  result.overload_ratio = 1.10F;
  result.adc_overload_ratio = 0.98F;
  result.creep_warning_grams = 5.0F;
  return result;
}

void sample(ScaleService& service, FakeAdc& adc, std::int32_t raw, std::uint32_t at_ms) {
  adc.push(raw);
  const auto result = service.poll(at_ms);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_TRUE(result.value());
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_hardware_profile_defaults_to_yzc133_5kg_and_validates_bounds() {
  ScaleHardwareSettings settings;
  TEST_ASSERT_EQUAL_STRING("YZC-133", settings.load_cell_model.c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 5000.0F, settings.rated_capacity_grams);
  TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.10F, settings.overload_ratio);
  TEST_ASSERT_TRUE(settings.validate().ok());

  ScaleCalibration calibration_defaults;
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F, calibration_defaults.load_cell_capacity_grams);

  settings.load_cell_model = "unsupported";
  TEST_ASSERT_FALSE(settings.validate().ok());
  settings = {};
  settings.rated_capacity_grams = 0.0F;
  TEST_ASSERT_FALSE(settings.validate().ok());
  settings.rated_capacity_grams = 2000.0F;
  TEST_ASSERT_TRUE(settings.validate().ok());
  settings.rated_capacity_grams = 2000.01F;
  TEST_ASSERT_FALSE(settings.validate().ok());
  settings.rated_capacity_grams = 5000.0F;
  TEST_ASSERT_TRUE(settings.validate().ok());
  settings = {};
  settings.overload_ratio = 2.01F;
  TEST_ASSERT_FALSE(settings.validate().ok());
}

void test_invalid_calibration_and_processing_configuration_are_rejected() {
  ScaleCalibration invalid;
  TEST_ASSERT_FALSE(invalid.validate().ok());

  FakeAdc adc;
  FakeStore store;
  auto config = test_config();
  config.filter_window = 2U;
  ScaleService service(adc, store, config);
  const auto result = service.initialize(0U, 1000U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ScaleState::fault),
      static_cast<int>(service.status().state));
  TEST_ASSERT_EQUAL_UINT(0U, adc.initialize_calls);
}

void test_initialization_loads_calibration_and_runs_internal_calibration() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration();
  ScaleService service(adc, store, test_config());

  TEST_ASSERT_TRUE(service.initialize(10U, 1000U).ok());
  TEST_ASSERT_EQUAL_UINT(1U, adc.initialize_calls);
  TEST_ASSERT_EQUAL_UINT(1U, adc.calibration_calls);
  TEST_ASSERT_TRUE(service.status().adc_ready);
  TEST_ASSERT_TRUE(service.status().calibration_loaded);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 1000.0F, service.hardware_settings().rated_capacity_grams);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ScaleState::sampling),
      static_cast<int>(service.status().state));
}

void test_5kg_profile_has_no_hidden_2kg_limit_and_honors_overload_ratio() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration(1000.0, 5000.0F);
  auto processing = test_config();
  processing.overload_ratio = 1.50F;
  ScaleService service(adc, store, processing);
  ScaleHardwareSettings settings;
  settings.overload_ratio = 1.05F;
  TEST_ASSERT_TRUE(service.configure_hardware(settings).ok());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 3001000, 0U);
  sample(service, adc, 3001000, 10U);
  sample(service, adc, 3001000, 20U);
  TEST_ASSERT_FLOAT_WITHIN(
      0.1F, 3000.0F, *service.status().sample.gross_grams);
  TEST_ASSERT_FALSE(service.status().sample.overload);

  sample(service, adc, 5301000, 30U);
  sample(service, adc, 5301000, 40U);
  sample(service, adc, 5301000, 50U);
  TEST_ASSERT_FLOAT_WITHIN(
      0.1F, 5300.0F, *service.status().sample.gross_grams);
  TEST_ASSERT_TRUE(service.status().sample.overload);
}

void test_2kg_profile_remains_supported() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration(1000.0, 2000.0F);
  ScaleService service(adc, store, test_config());
  ScaleHardwareSettings settings;
  settings.rated_capacity_grams = 2000.0F;
  TEST_ASSERT_TRUE(service.configure_hardware(settings).ok());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 2001000, 0U);
  sample(service, adc, 2001000, 10U);
  sample(service, adc, 2001000, 20U);
  TEST_ASSERT_FLOAT_WITHIN(
      0.1F, 2000.0F, *service.status().sample.gross_grams);
  TEST_ASSERT_FALSE(service.status().sample.overload);

  sample(service, adc, 2301000, 30U);
  sample(service, adc, 2301000, 40U);
  sample(service, adc, 2301000, 50U);
  TEST_ASSERT_TRUE(service.status().sample.overload);
}

void test_explicit_profile_rejects_stale_calibration_and_late_reconfiguration() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration(1000.0, 2000.0F);
  ScaleService service(adc, store, test_config());
  ScaleHardwareSettings settings;
  TEST_ASSERT_TRUE(service.configure_hardware(settings).ok());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());
  TEST_ASSERT_FALSE(service.status().calibration_loaded);
  TEST_ASSERT_FALSE(service.calibration().has_value());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ScaleState::calibration_required),
      static_cast<int>(service.status().state));
  TEST_ASSERT_TRUE(service.status().last_error.has_value());
  TEST_ASSERT_FALSE(service.configure_hardware(settings).ok());
}

void test_calibration_capacity_must_match_explicit_profile() {
  FakeAdc adc;
  FakeStore store;
  ScaleService service(adc, store, test_config());
  ScaleHardwareSettings settings;
  TEST_ASSERT_TRUE(service.configure_hardware(settings).ok());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 1000, 0U);
  sample(service, adc, 1000, 10U);
  sample(service, adc, 1000, 20U);
  TEST_ASSERT_TRUE(service.tare().ok());
  sample(service, adc, 101000, 30U);
  sample(service, adc, 101000, 40U);
  sample(service, adc, 101000, 50U);
  const auto result = service.calibrate(100.0F, 2000.0F);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_UINT(0U, store.save_calls);
}

void test_stable_weight_requires_full_filter_and_duration() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration();
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 2000, 0U);
  sample(service, adc, 2000, 10U);
  sample(service, adc, 2000, 20U);
  TEST_ASSERT_FALSE(service.status().sample.stable);
  sample(service, adc, 2000, 120U);
  TEST_ASSERT_TRUE(service.status().sample.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 100.0F, *service.status().sample.gross_grams);
}

void test_noise_prevents_stability_and_tare() {
  FakeAdc adc;
  FakeStore store;
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 1000, 0U);
  sample(service, adc, 1100, 10U);
  sample(service, adc, 900, 20U);
  TEST_ASSERT_FALSE(service.status().sample.stable);
  const auto result = service.tare();
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::scale_unstable),
      static_cast<int>(result.error().category));
}

void test_tare_and_reference_calibration_persist_and_compute_grams() {
  FakeAdc adc;
  FakeStore store;
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 1000, 0U);
  sample(service, adc, 1001, 10U);
  sample(service, adc, 999, 20U);
  TEST_ASSERT_TRUE(service.tare().ok());

  sample(service, adc, 2000, 30U);
  sample(service, adc, 2001, 40U);
  sample(service, adc, 1999, 50U);
  const auto result = service.calibrate(100.0F, 2000.0F);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, store.save_calls);
  TEST_ASSERT_EQUAL_INT32(1000, result.value().zero_offset_counts);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 10.0F, static_cast<float>(result.value().counts_per_gram));

  sample(service, adc, 2500, 60U);
  sample(service, adc, 2500, 70U);
  sample(service, adc, 2500, 80U);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 150.0F, *service.status().sample.gross_grams);
}

void test_negative_load_cell_orientation_is_supported() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration(-10.0);
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());
  sample(service, adc, 500, 10U);
  sample(service, adc, 500, 20U);
  sample(service, adc, 500, 30U);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, *service.status().sample.gross_grams);
  TEST_ASSERT_FALSE(service.status().sample.negative);
}

void test_negative_and_both_overload_paths_are_reported() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration(10.0, 100.0F);
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  sample(service, adc, 900, 10U);
  TEST_ASSERT_TRUE(service.status().sample.negative);
  sample(service, adc, 2200, 20U);
  sample(service, adc, 2200, 30U);
  sample(service, adc, 2200, 40U);
  TEST_ASSERT_TRUE(service.status().sample.overload);

  FakeAdc adc_saturation;
  FakeStore wide_capacity_store;
  wide_capacity_store.stored = calibration(10.0, 1000000000.0F);
  ScaleService saturation(adc_saturation, wide_capacity_store, test_config());
  TEST_ASSERT_TRUE(saturation.initialize(0U, 1000U).ok());
  sample(saturation, adc_saturation, 8300000, 10U);
  sample(saturation, adc_saturation, 8300000, 20U);
  sample(saturation, adc_saturation, 8300000, 30U);
  TEST_ASSERT_TRUE(saturation.status().sample.overload);
}

void test_sample_timeout_disconnects_and_next_sample_recovers() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration();
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());

  const auto timeout = service.poll(51U);
  TEST_ASSERT_FALSE(timeout.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ScaleState::disconnected),
      static_cast<int>(service.status().state));
  sample(service, adc, 1000, 52U);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ScaleState::sampling),
      static_cast<int>(service.status().state));
  TEST_ASSERT_FALSE(service.status().last_error.has_value());
}

void test_adc_and_store_failures_remain_structured_and_degraded() {
  FakeAdc failing_adc;
  failing_adc.initialize_fails = true;
  FakeStore store;
  ScaleService failed(failing_adc, store, test_config());
  const auto init = failed.initialize(0U, 1000U);
  TEST_ASSERT_FALSE(init.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::scale_unavailable),
      static_cast<int>(init.error().category));

  FakeAdc adc;
  FakeStore unavailable_store;
  unavailable_store.load_fails = true;
  ScaleService degraded(adc, unavailable_store, test_config());
  TEST_ASSERT_TRUE(degraded.initialize(0U, 1000U).ok());
  TEST_ASSERT_FALSE(degraded.status().persistence_available);
  TEST_ASSERT_TRUE(degraded.status().last_error.has_value());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::storage),
      static_cast<int>(degraded.status().last_error->category));
}

void test_failed_calibration_save_is_reported_without_losing_runtime_factor() {
  FakeAdc adc;
  FakeStore store;
  store.save_fails = true;
  ScaleService service(adc, store, test_config());
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());
  sample(service, adc, 1000, 0U);
  sample(service, adc, 1000, 10U);
  sample(service, adc, 1000, 20U);
  TEST_ASSERT_TRUE(service.tare().ok());
  sample(service, adc, 2000, 30U);
  sample(service, adc, 2000, 40U);
  sample(service, adc, 2000, 50U);

  const auto result = service.calibrate(100.0F, 2000.0F);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::storage),
      static_cast<int>(result.error().category));
  TEST_ASSERT_TRUE(service.calibration().has_value());
  TEST_ASSERT_FALSE(service.status().persistence_available);
}

void test_slow_stable_drift_sets_creep_warning() {
  FakeAdc adc;
  FakeStore store;
  store.stored = calibration();
  auto config = test_config();
  config.stability_threshold_grams = 10.0F;
  config.stability_duration_ms = 10U;
  config.creep_warning_grams = 2.0F;
  ScaleService service(adc, store, config);
  TEST_ASSERT_TRUE(service.initialize(0U, 1000U).ok());
  sample(service, adc, 2000, 0U);
  sample(service, adc, 2000, 1U);
  sample(service, adc, 2000, 2U);
  sample(service, adc, 2000, 12U);
  TEST_ASSERT_TRUE(service.status().sample.stable);
  sample(service, adc, 2010, 13U);
  sample(service, adc, 2020, 14U);
  sample(service, adc, 2030, 15U);
  sample(service, adc, 2040, 16U);
  TEST_ASSERT_TRUE(service.status().sample.stable);
  TEST_ASSERT_TRUE(service.status().sample.creep_warning);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 3.0F, *service.status().sample.drift_from_stable_grams);
}

void test_i2c_scan_result_is_bounded_and_tracks_target() {
  opentag::hardware::scale::I2cScanResult scan;
  scan.bus_started = true;
  for (std::uint8_t address = 0x08U; address <= 0x18U; ++address) {
    scan.record(address, 0x2AU);
  }
  scan.record(0x2AU, 0x2AU);

  TEST_ASSERT_EQUAL_UINT8(18U, scan.device_count);
  TEST_ASSERT_EQUAL_UINT8(16U, scan.reported_count);
  TEST_ASSERT_TRUE(scan.target_present);
  TEST_ASSERT_TRUE(scan.truncated());
}

void test_i2c_diagnostic_prefers_expected_bus_target() {
  opentag::hardware::scale::ScaleI2cDiagnosticResult diagnostic;
  diagnostic.expected.record(0x2AU, 0x2AU);
  diagnostic.reversed_scanned = true;
  diagnostic.reversed.record(0x2AU, 0x2AU);

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(opentag::hardware::scale::ScaleI2cOutcome::present_on_expected_bus),
      static_cast<int>(diagnostic.outcome()));
}

void test_i2c_diagnostic_reports_reversed_target() {
  opentag::hardware::scale::ScaleI2cDiagnosticResult diagnostic;
  diagnostic.reversed_scanned = true;
  diagnostic.reversed.record(0x2AU, 0x2AU);

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(opentag::hardware::scale::ScaleI2cOutcome::present_on_reversed_bus),
      static_cast<int>(diagnostic.outcome()));
}

void test_i2c_diagnostic_distinguishes_other_devices_from_empty_buses() {
  opentag::hardware::scale::ScaleI2cDiagnosticResult diagnostic;
  diagnostic.expected.record(0x3CU, 0x2AU);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(opentag::hardware::scale::ScaleI2cOutcome::target_missing_with_other_devices),
      static_cast<int>(diagnostic.outcome()));

  diagnostic = {};
  diagnostic.reversed_scanned = true;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(opentag::hardware::scale::ScaleI2cOutcome::no_devices),
      static_cast<int>(diagnostic.outcome()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_i2c_scan_result_is_bounded_and_tracks_target);
  RUN_TEST(test_i2c_diagnostic_prefers_expected_bus_target);
  RUN_TEST(test_i2c_diagnostic_reports_reversed_target);
  RUN_TEST(test_i2c_diagnostic_distinguishes_other_devices_from_empty_buses);
  RUN_TEST(test_hardware_profile_defaults_to_yzc133_5kg_and_validates_bounds);
  RUN_TEST(test_invalid_calibration_and_processing_configuration_are_rejected);
  RUN_TEST(test_initialization_loads_calibration_and_runs_internal_calibration);
  RUN_TEST(test_5kg_profile_has_no_hidden_2kg_limit_and_honors_overload_ratio);
  RUN_TEST(test_2kg_profile_remains_supported);
  RUN_TEST(test_explicit_profile_rejects_stale_calibration_and_late_reconfiguration);
  RUN_TEST(test_calibration_capacity_must_match_explicit_profile);
  RUN_TEST(test_stable_weight_requires_full_filter_and_duration);
  RUN_TEST(test_noise_prevents_stability_and_tare);
  RUN_TEST(test_tare_and_reference_calibration_persist_and_compute_grams);
  RUN_TEST(test_negative_load_cell_orientation_is_supported);
  RUN_TEST(test_negative_and_both_overload_paths_are_reported);
  RUN_TEST(test_sample_timeout_disconnects_and_next_sample_recovers);
  RUN_TEST(test_adc_and_store_failures_remain_structured_and_degraded);
  RUN_TEST(test_failed_calibration_save_is_reported_without_losing_runtime_factor);
  RUN_TEST(test_slow_stable_drift_sets_creep_warning);
  return UNITY_END();
}

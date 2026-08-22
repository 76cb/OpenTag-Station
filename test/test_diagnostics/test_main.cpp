#include <unity.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <thread>

#include "diagnostics/system_diagnostics.hpp"

using opentag::diagnostics::ScaleDiagnosticStore;
using opentag::diagnostics::TransportDiagnosticStore;
using opentag::diagnostics::WebsocketDisconnectReason;
using opentag::services::ScaleCalibration;
using opentag::services::ScaleHardwareSettings;
using opentag::services::ScaleState;
using opentag::services::ScaleStatus;

void setUp() {}
void tearDown() {}

ScaleStatus status_for(
    std::int32_t raw_counts,
    float gross_grams,
    bool stable) {
  ScaleStatus status;
  status.state = ScaleState::sampling;
  status.adc_ready = true;
  status.calibration_loaded = true;
  status.persistence_available = true;
  status.samples_in_filter = 3U;
  status.sample.raw_counts = raw_counts;
  status.sample.filtered_raw_counts = raw_counts + 0.6;
  status.sample.gross_grams = gross_grams;
  status.sample.raw_stable = stable;
  status.sample.stable = stable;
  return status;
}

ScaleCalibration calibration_for(float reference, float capacity) {
  ScaleCalibration calibration;
  calibration.zero_offset_counts = 321;
  calibration.counts_per_gram = 1234.5;
  calibration.reference_grams = reference;
  calibration.load_cell_capacity_grams = capacity;
  return calibration;
}

void test_scale_diagnostics_expose_coherent_hardware_and_calibration_state() {
  ScaleDiagnosticStore store;
  store.set_task_running(true);
  ScaleHardwareSettings hardware;
  hardware.rated_capacity_grams = 5000.0F;
  hardware.overload_ratio = 1.10F;
  const auto status = status_for(1000, 42.125F, true);
  const auto calibration = calibration_for(500.0F, 5000.0F);

  store.update(status, calibration, hardware);
  const auto snapshot = store.snapshot();
  TEST_ASSERT_TRUE(snapshot.scale_task_running);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ScaleState::sampling),
      static_cast<int>(snapshot.scale_state));
  TEST_ASSERT_TRUE(snapshot.scale_adc_ready);
  TEST_ASSERT_TRUE(snapshot.scale_calibrated);
  TEST_ASSERT_TRUE(snapshot.scale_calibration_loaded);
  TEST_ASSERT_TRUE(snapshot.scale_calibration_matches_hardware);
  TEST_ASSERT_TRUE(snapshot.scale_weight_available);
  TEST_ASSERT_TRUE(snapshot.scale_raw_stable);
  TEST_ASSERT_TRUE(snapshot.scale_stable);
  TEST_ASSERT_EQUAL_UINT(3U, snapshot.scale_samples_in_filter);
  TEST_ASSERT_EQUAL_INT32(1000, snapshot.scale_raw_counts);
  TEST_ASSERT_EQUAL_INT32(1001, snapshot.scale_filtered_counts);
  TEST_ASSERT_EQUAL_INT32(42125, snapshot.scale_gross_milligrams);
  TEST_ASSERT_EQUAL_INT32(321, snapshot.scale_zero_offset_counts);
  TEST_ASSERT_EQUAL_INT32(
      1234500, snapshot.scale_factor_millicounts_per_gram);
  TEST_ASSERT_EQUAL_STRING(
      "YZC-133", snapshot.scale_load_cell_model.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F, snapshot.scale_rated_capacity_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1.10F, snapshot.scale_configured_overload_ratio);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5500.0F, snapshot.scale_overload_threshold_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 500.0F, snapshot.scale_calibration_reference_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F, snapshot.scale_calibration_capacity_grams);
  TEST_ASSERT_GREATER_THAN_UINT64(1U, snapshot.scale_revision);
}

void test_missing_calibration_clears_every_calibration_derived_field() {
  ScaleDiagnosticStore store;
  ScaleHardwareSettings hardware;
  auto status = status_for(500, 10.0F, false);
  store.update(status, calibration_for(100.0F, 5000.0F), hardware);

  status.calibration_loaded = false;
  status.sample.gross_grams.reset();
  store.update(status, std::nullopt, hardware);
  const auto snapshot = store.snapshot();
  TEST_ASSERT_FALSE(snapshot.scale_calibrated);
  TEST_ASSERT_FALSE(snapshot.scale_calibration_loaded);
  TEST_ASSERT_FALSE(snapshot.scale_calibration_matches_hardware);
  TEST_ASSERT_FALSE(snapshot.scale_weight_available);
  TEST_ASSERT_EQUAL_INT32(0, snapshot.scale_gross_milligrams);
  TEST_ASSERT_EQUAL_INT32(0, snapshot.scale_zero_offset_counts);
  TEST_ASSERT_EQUAL_INT32(
      0, snapshot.scale_factor_millicounts_per_gram);
  TEST_ASSERT_FLOAT_WITHIN(
      0.0F, 0.0F, snapshot.scale_calibration_reference_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.0F, 0.0F, snapshot.scale_calibration_capacity_grams);
}

bool matches_profile(
    const opentag::diagnostics::ScaleDiagnosticSnapshot& snapshot,
    float capacity,
    float ratio,
    std::int32_t raw,
    float reference) {
  return std::fabs(snapshot.scale_rated_capacity_grams - capacity) < 0.01F &&
      std::fabs(snapshot.scale_configured_overload_ratio - ratio) < 0.001F &&
      std::fabs(
          snapshot.scale_overload_threshold_grams - capacity * ratio) < 0.1F &&
      snapshot.scale_raw_counts == raw &&
      std::fabs(snapshot.scale_calibration_reference_grams - reference) < 0.01F &&
      std::fabs(snapshot.scale_calibration_capacity_grams - capacity) < 0.01F;
}

void test_concurrent_snapshots_never_mix_two_scale_updates() {
  ScaleDiagnosticStore store;
  ScaleHardwareSettings two_kg;
  two_kg.rated_capacity_grams = 2000.0F;
  two_kg.overload_ratio = 1.10F;
  ScaleHardwareSettings five_kg;
  five_kg.rated_capacity_grams = 5000.0F;
  five_kg.overload_ratio = 1.20F;
  const auto two_kg_status = status_for(200, 200.0F, true);
  const auto five_kg_status = status_for(500, 500.0F, false);
  const auto two_kg_calibration = calibration_for(100.0F, 2000.0F);
  const auto five_kg_calibration = calibration_for(250.0F, 5000.0F);
  store.update(two_kg_status, two_kg_calibration, two_kg);

  std::atomic_bool coherent{true};
  std::thread writer([&]() {
    for (std::size_t index = 0U; index < 10000U; ++index) {
      store.update(five_kg_status, five_kg_calibration, five_kg);
      store.update(two_kg_status, two_kg_calibration, two_kg);
    }
  });
  for (std::size_t index = 0U; index < 20000U; ++index) {
    const auto snapshot = store.snapshot();
    const bool two_kg_match =
        matches_profile(snapshot, 2000.0F, 1.10F, 200, 100.0F);
    const bool five_kg_match =
        matches_profile(snapshot, 5000.0F, 1.20F, 500, 250.0F);
    if (!two_kg_match && !five_kg_match) {
      coherent.store(false, std::memory_order_relaxed);
      break;
    }
  }
  writer.join();
  TEST_ASSERT_TRUE(coherent.load(std::memory_order_relaxed));
}

void test_transport_diagnostics_track_lifecycle_failures_and_live_gauges() {
  TransportDiagnosticStore store;
  auto snapshot = store.snapshot();
  TEST_ASSERT_FALSE(snapshot.http_server_running);
  TEST_ASSERT_EQUAL_UINT32(0U, snapshot.active_http_sessions);
  TEST_ASSERT_EQUAL_STRING(
      "none",
      opentag::diagnostics::to_string(
          snapshot.last_websocket_disconnect_reason));

  store.set_http_server_running(true);
  store.http_session_opened();
  store.http_session_opened();
  store.http_session_opened();
  store.websocket_opened();
  store.websocket_opened();
  store.websocket_send_failed();
  store.websocket_event_dropped();
  store.websocket_disconnected(WebsocketDisconnectReason::send_failure);
  store.http_session_closed();

  snapshot = store.snapshot();
  TEST_ASSERT_TRUE(snapshot.http_server_running);
  TEST_ASSERT_EQUAL_UINT32(2U, snapshot.active_http_sessions);
  TEST_ASSERT_EQUAL_UINT32(3U, snapshot.maximum_observed_http_sessions);
  TEST_ASSERT_EQUAL_UINT32(3U, snapshot.http_session_open_count);
  TEST_ASSERT_EQUAL_UINT32(1U, snapshot.http_session_close_count);
  TEST_ASSERT_EQUAL_UINT32(1U, snapshot.websocket_clients);
  TEST_ASSERT_EQUAL_UINT32(2U, snapshot.websocket_open_count);
  TEST_ASSERT_EQUAL_UINT32(1U, snapshot.websocket_disconnect_count);
  TEST_ASSERT_EQUAL_UINT32(1U, snapshot.websocket_send_failure_count);
  TEST_ASSERT_EQUAL_UINT32(2U, snapshot.websocket_dropped_event_count);
  TEST_ASSERT_EQUAL_STRING(
      "send-failure",
      opentag::diagnostics::to_string(
          snapshot.last_websocket_disconnect_reason));

  store.websocket_disconnected(WebsocketDisconnectReason::server_stopped);
  store.http_session_closed();
  store.http_session_closed();
  store.set_http_server_running(false);
  snapshot = store.snapshot();
  TEST_ASSERT_FALSE(snapshot.http_server_running);
  TEST_ASSERT_EQUAL_UINT32(0U, snapshot.active_http_sessions);
  TEST_ASSERT_EQUAL_UINT32(0U, snapshot.websocket_clients);
  TEST_ASSERT_EQUAL_UINT32(3U, snapshot.http_session_close_count);
  TEST_ASSERT_EQUAL_UINT32(2U, snapshot.websocket_disconnect_count);
  TEST_ASSERT_EQUAL_STRING(
      "server-stopped",
      opentag::diagnostics::to_string(
          snapshot.last_websocket_disconnect_reason));
}

void test_transport_diagnostics_are_atomic_under_parallel_session_churn() {
  TransportDiagnosticStore store;
  constexpr std::size_t worker_count = 4U;
  constexpr std::size_t cycles_per_worker = 2500U;
  std::array<std::thread, worker_count> workers;
  store.set_http_server_running(true);
  for (auto& worker : workers) {
    worker = std::thread([&store]() {
      for (std::size_t cycle = 0U; cycle < cycles_per_worker; ++cycle) {
        store.http_session_opened();
        store.http_session_closed();
      }
    });
  }
  for (auto& worker : workers) worker.join();

  const auto snapshot = store.snapshot();
  TEST_ASSERT_EQUAL_UINT32(0U, snapshot.active_http_sessions);
  TEST_ASSERT_EQUAL_UINT32(
      worker_count * cycles_per_worker,
      snapshot.http_session_open_count);
  TEST_ASSERT_EQUAL_UINT32(
      worker_count * cycles_per_worker,
      snapshot.http_session_close_count);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
      1U, snapshot.maximum_observed_http_sessions);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(
      worker_count, snapshot.maximum_observed_http_sessions);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(
      test_scale_diagnostics_expose_coherent_hardware_and_calibration_state);
  RUN_TEST(test_missing_calibration_clears_every_calibration_derived_field);
  RUN_TEST(test_concurrent_snapshots_never_mix_two_scale_updates);
  RUN_TEST(
      test_transport_diagnostics_track_lifecycle_failures_and_live_gauges);
  RUN_TEST(
      test_transport_diagnostics_are_atomic_under_parallel_session_churn);
  return UNITY_END();
}

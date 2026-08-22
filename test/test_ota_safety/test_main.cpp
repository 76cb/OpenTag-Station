#include <unity.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "application/boot_health_policy.hpp"
#include "application/device_lifecycle_gate.hpp"
#include "application/operation_registry.hpp"
#include "ota/upload_operation_policy.hpp"

using opentag::application::BootHealthPolicy;
using opentag::application::BootHealthRequirement;
using opentag::application::BootHealthSignals;
using opentag::application::BootHealthState;
using opentag::application::DeviceLifecycleGate;
using opentag::application::DeviceLifecycleOwner;
using opentag::application::OperationKind;
using opentag::application::OperationRegistry;
using opentag::application::OperationState;
using opentag::ota::OperationPrecondition;
using opentag::ota::UpdateSnapshot;
using opentag::ota::UpdateState;
using opentag::ota::UploadCleanupDisposition;
using opentag::ota::UploadOperationResolution;
using opentag::ota::classify_upload_cleanup;
using opentag::ota::resolve_upload_operation;

namespace {

BootHealthSignals healthy_signals() {
  BootHealthSignals result;
  result.storage_ready = true;
  result.configuration_initialized = true;
  result.application_ready = true;
  result.display_ready = true;
  result.ui_task_running = true;
  result.configuration_task_running = true;
  result.backend_task_running = true;
  result.scale_commands_ready = true;
  result.scale_task_running = true;
  result.network_task_running = true;
  result.device_control_task_running = true;
  result.web_server_running = true;
  result.ota_task_running = true;
  return result;
}

void test_lifecycle_gate_excludes_every_other_owner() {
  const std::vector<DeviceLifecycleOwner> owners = {
      DeviceLifecycleOwner::reboot,
      DeviceLifecycleOwner::factory_reset,
      DeviceLifecycleOwner::ota_update,
      DeviceLifecycleOwner::candidate_validation,
  };
  for (const auto held_owner : owners) {
    DeviceLifecycleGate gate;
    const auto held = gate.try_acquire(held_owner);
    TEST_ASSERT_TRUE(static_cast<bool>(held));
    TEST_ASSERT_TRUE(gate.owns(held));
    for (const auto contender : owners) {
      TEST_ASSERT_FALSE(static_cast<bool>(gate.try_acquire(contender)));
    }
    TEST_ASSERT_TRUE(gate.release(held));
    TEST_ASSERT_FALSE(gate.snapshot().busy());
  }
}

void test_lifecycle_gate_rejects_invalid_and_stale_leases() {
  DeviceLifecycleGate gate;
  TEST_ASSERT_FALSE(static_cast<bool>(
      gate.try_acquire(DeviceLifecycleOwner::none)));
  TEST_ASSERT_FALSE(gate.release({}));

  const auto first = gate.try_acquire(DeviceLifecycleOwner::reboot);
  TEST_ASSERT_TRUE(gate.release(first));
  const auto second = gate.try_acquire(DeviceLifecycleOwner::factory_reset);
  TEST_ASSERT_TRUE(static_cast<bool>(second));
  TEST_ASSERT_NOT_EQUAL(first.generation, second.generation);
  TEST_ASSERT_FALSE(gate.release(first));
  TEST_ASSERT_TRUE(gate.owns(second));
  TEST_ASSERT_TRUE(gate.release(second));
}

void test_lifecycle_gate_allows_only_one_concurrent_acquisition() {
  DeviceLifecycleGate gate;
  std::atomic<unsigned int> ready{0U};
  std::atomic_bool go{false};
  std::atomic<unsigned int> acquired{0U};
  std::vector<std::thread> contenders;
  for (unsigned int index = 0U; index < 12U; ++index) {
    contenders.emplace_back([&, index]() {
      ready.fetch_add(1U, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      const auto owner = index % 2U == 0U
          ? DeviceLifecycleOwner::ota_update
          : DeviceLifecycleOwner::factory_reset;
      if (gate.try_acquire(owner)) {
        acquired.fetch_add(1U, std::memory_order_relaxed);
      }
    });
  }
  while (ready.load(std::memory_order_acquire) != contenders.size()) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);
  for (auto& contender : contenders) contender.join();
  TEST_ASSERT_EQUAL_UINT(1U, acquired.load(std::memory_order_relaxed));
  TEST_ASSERT_TRUE(gate.snapshot().busy());
}

void test_health_waits_for_complete_confirmation_window() {
  const BootHealthPolicy policy(1000U);
  const auto signals = healthy_signals();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::stabilizing),
      static_cast<int>(policy.evaluate(30999U, signals).state));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::healthy),
      static_cast<int>(policy.evaluate(31000U, signals).state));
}

void test_health_window_is_wrap_safe() {
  constexpr std::uint32_t started =
      std::numeric_limits<std::uint32_t>::max() - 999U;
  const BootHealthPolicy policy(started);
  const auto signals = healthy_signals();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::stabilizing),
      static_cast<int>(policy.evaluate(28999U, signals).state));
  const auto due = policy.evaluate(29000U, signals);
  TEST_ASSERT_EQUAL_UINT32(30000U, due.elapsed_ms);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::healthy),
      static_cast<int>(due.state));
}

void test_health_fails_closed_when_a_local_requirement_is_missing() {
  const BootHealthPolicy policy(0U);
  auto signals = healthy_signals();
  signals.web_server_running = false;
  const auto result = policy.evaluate(30000U, signals);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::unhealthy),
      static_cast<int>(result.state));
  TEST_ASSERT_TRUE(result.missing(BootHealthRequirement::web_server));
  TEST_ASSERT_FALSE(result.missing(BootHealthRequirement::storage));
}

void test_health_requires_every_declared_local_signal() {
  struct RequiredSignal {
    bool BootHealthSignals::*field;
    BootHealthRequirement requirement;
  };
  const std::vector<RequiredSignal> requirements = {
      {&BootHealthSignals::storage_ready, BootHealthRequirement::storage},
      {&BootHealthSignals::configuration_initialized,
       BootHealthRequirement::configuration},
      {&BootHealthSignals::application_ready, BootHealthRequirement::application},
      {&BootHealthSignals::display_ready, BootHealthRequirement::display},
      {&BootHealthSignals::ui_task_running, BootHealthRequirement::ui_task},
      {&BootHealthSignals::configuration_task_running,
       BootHealthRequirement::configuration_task},
      {&BootHealthSignals::backend_task_running,
       BootHealthRequirement::backend_task},
      {&BootHealthSignals::scale_commands_ready,
       BootHealthRequirement::scale_commands},
      {&BootHealthSignals::scale_task_running, BootHealthRequirement::scale_task},
      {&BootHealthSignals::network_task_running,
       BootHealthRequirement::network_task},
      {&BootHealthSignals::device_control_task_running,
       BootHealthRequirement::device_control_task},
      {&BootHealthSignals::web_server_running, BootHealthRequirement::web_server},
      {&BootHealthSignals::ota_task_running, BootHealthRequirement::ota_task},
  };
  const BootHealthPolicy policy(0U);
  for (const auto& requirement : requirements) {
    auto signals = healthy_signals();
    signals.*(requirement.field) = false;
    const auto result = policy.evaluate(30000U, signals);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(BootHealthState::unhealthy),
        static_cast<int>(result.state));
    TEST_ASSERT_TRUE(result.missing(requirement.requirement));
  }
}

void test_health_accepts_explicit_safe_configuration_degradation() {
  const BootHealthPolicy policy(0U);
  auto signals = healthy_signals();
  signals.configuration_initialized = false;
  signals.configuration_safely_degraded = true;
  const auto result = policy.evaluate(30000U, signals);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::healthy),
      static_cast<int>(result.state));
  TEST_ASSERT_FALSE(result.missing(BootHealthRequirement::configuration));
}

void test_health_does_not_depend_on_backends_network_link_or_nfc() {
  // The policy intentionally has no backend-connectivity, Wi-Fi-link, or NFC
  // input. A running local owner is sufficient even when those are unavailable.
  const BootHealthPolicy policy(0U);
  const auto result = policy.evaluate(30000U, healthy_signals());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::healthy),
      static_cast<int>(result.state));
}

void test_fatal_initialization_fails_before_the_window() {
  const BootHealthPolicy policy(100U);
  auto signals = healthy_signals();
  signals.fatal_initialization_error = true;
  const auto result = policy.evaluate(101U, signals);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::unhealthy),
      static_cast<int>(result.state));
}

void test_factory_reset_recovery_is_not_classified_as_ota_failure() {
  const BootHealthPolicy policy(0U);
  auto signals = healthy_signals();
  signals.factory_reset_recovery_pending = true;
  signals.storage_ready = false;
  signals.fatal_initialization_error = true;
  const auto result = policy.evaluate(60000U, signals);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BootHealthState::factory_reset_recovery),
      static_cast<int>(result.state));
  TEST_ASSERT_TRUE(result.missing(BootHealthRequirement::storage));
}


void test_upload_cleanup_requires_authoritative_terminal_state() {
  const OperationPrecondition owner{41U, 7U};
  UpdateSnapshot snapshot;
  snapshot.operation_id = owner.operation_id;
  snapshot.generation = owner.generation;
  snapshot.state = UpdateState::writing;

  const auto ambiguous_timeout = classify_upload_cleanup(
      owner, snapshot, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::unresolved),
      static_cast<int>(ambiguous_timeout));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadOperationResolution::running),
      static_cast<int>(resolve_upload_operation(
          ambiguous_timeout, true, false)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::aborted),
      static_cast<int>(classify_upload_cleanup(owner, snapshot, true)));
}

void test_upload_cleanup_recognizes_exact_failed_cleared_and_validated_states() {
  const OperationPrecondition owner{41U, 7U};
  UpdateSnapshot snapshot;
  snapshot.operation_id = owner.operation_id;
  snapshot.generation = owner.generation;
  snapshot.state = UpdateState::failed;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::failed),
      static_cast<int>(classify_upload_cleanup(owner, snapshot, false)));

  snapshot.operation_id = 0U;
  snapshot.state = UpdateState::idle;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::aborted),
      static_cast<int>(classify_upload_cleanup(owner, snapshot, false)));

  snapshot.operation_id = owner.operation_id;
  snapshot.state = UpdateState::ready_to_reboot;
  snapshot.validation_passed = true;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::validated),
      static_cast<int>(classify_upload_cleanup(owner, snapshot, false)));
}

void test_upload_cleanup_rejects_stale_or_mismatched_generation_evidence() {
  const OperationPrecondition owner{41U, 7U};
  UpdateSnapshot snapshot;
  snapshot.operation_id = owner.operation_id;
  snapshot.generation = owner.generation + 1U;
  snapshot.state = UpdateState::failed;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::unresolved),
      static_cast<int>(classify_upload_cleanup(owner, snapshot, false)));

  snapshot.operation_id = 0U;
  snapshot.state = UpdateState::idle;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::unresolved),
      static_cast<int>(classify_upload_cleanup(owner, snapshot, false)));
}

void test_cleared_generation_persist_failure_is_terminal_and_monotonic() {
  OperationRegistry operations;
  const auto operation_id = operations.begin(
      OperationKind::firmware_upload, 1U, "Stopping firmware upload");
  TEST_ASSERT_NOT_EQUAL_UINT64(0U, operation_id);
  operations.mark_running(operation_id, 2U, "Persisting OTA cleanup");
  const OperationPrecondition owner{operation_id, 9U};

  UpdateSnapshot persist_failed;
  persist_failed.operation_id = 0U;
  persist_failed.generation = owner.generation;
  persist_failed.state = UpdateState::failed;
  TEST_ASSERT_TRUE(persist_failed.last_error.assign(
      "Failed to persist cleared OTA state"));
  const auto disposition = classify_upload_cleanup(
      owner, persist_failed, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::failed),
      static_cast<int>(disposition));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadOperationResolution::failed),
      static_cast<int>(resolve_upload_operation(
          disposition, true, true)));

  opentag::core::Error error{
      opentag::core::ErrorCategory::firmware_update,
      "Fallback cleanup failure",
      true,
  };
  if (!persist_failed.last_error.empty()) {
    error.message = std::string(persist_failed.last_error.view());
  }
  operations.fail(operation_id, 3U, std::move(error));
  const auto terminal_revision = operations.revision();
  operations.mark_running(operation_id, 4U, "Stale cleanup callback");
  operations.succeed(operation_id, 5U, "Stale finish callback");

  const auto terminal = operations.get(operation_id);
  TEST_ASSERT_TRUE(terminal.has_value());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(OperationState::failed),
      static_cast<int>(terminal->state));
  TEST_ASSERT_EQUAL_STRING(
      "Failed to persist cleared OTA state", terminal->message.c_str());
  TEST_ASSERT_EQUAL_UINT64(terminal_revision, operations.revision());

  persist_failed.generation = owner.generation + 1U;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::unresolved),
      static_cast<int>(classify_upload_cleanup(
          owner, persist_failed, false)));
}

void test_finish_timeout_late_validation_then_queued_abort_is_monotonic() {
  OperationRegistry operations;
  const auto operation_id = operations.begin(
      OperationKind::firmware_upload, 10U, "Firmware upload accepted");
  TEST_ASSERT_NOT_EQUAL_UINT64(0U, operation_id);
  operations.mark_running(operation_id, 11U, "Validating firmware");
  const OperationPrecondition owner{operation_id, 7U};

  UpdateSnapshot validated;
  validated.operation_id = owner.operation_id;
  validated.generation = owner.generation;
  validated.state = UpdateState::ready_to_reboot;
  validated.validation_passed = true;
  const auto late_finish = classify_upload_cleanup(owner, validated, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadCleanupDisposition::validated),
      static_cast<int>(late_finish));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadOperationResolution::running),
      static_cast<int>(resolve_upload_operation(
          late_finish, true, false)));

  // No observer can see success while the finish-timeout cleanup is pending.
  operations.mark_running(operation_id, 12U, "Canceling late candidate");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(OperationState::running),
      static_cast<int>(operations.get(operation_id)->state));

  UpdateSnapshot canceled;
  canceled.operation_id = 0U;
  canceled.generation = owner.generation;
  canceled.state = UpdateState::idle;
  const auto owner_cleanup = classify_upload_cleanup(owner, canceled, true);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UploadOperationResolution::failed),
      static_cast<int>(resolve_upload_operation(
          owner_cleanup, true, true)));
  operations.fail(
      operation_id,
      13U,
      {opentag::core::ErrorCategory::firmware_update,
       "Late candidate canceled after finish timeout",
       true});

  // The already-queued HTTP abort and any stale late-finish callback cannot
  // replace the authoritative failed outcome.
  const auto terminal_revision = operations.revision();
  operations.fail(
      operation_id,
      14U,
      {opentag::core::ErrorCategory::firmware_update,
       "Redundant queued abort",
       true});
  operations.succeed(operation_id, 15U, "Stale late validation");
  const auto terminal = operations.get(operation_id);
  TEST_ASSERT_TRUE(terminal.has_value());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(OperationState::failed),
      static_cast<int>(terminal->state));
  TEST_ASSERT_EQUAL_STRING(
      "Late candidate canceled after finish timeout",
      terminal->message.c_str());
  TEST_ASSERT_EQUAL_UINT64(terminal_revision, operations.revision());
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lifecycle_gate_excludes_every_other_owner);
  RUN_TEST(test_lifecycle_gate_rejects_invalid_and_stale_leases);
  RUN_TEST(test_lifecycle_gate_allows_only_one_concurrent_acquisition);
  RUN_TEST(test_health_waits_for_complete_confirmation_window);
  RUN_TEST(test_health_window_is_wrap_safe);
  RUN_TEST(test_health_fails_closed_when_a_local_requirement_is_missing);
  RUN_TEST(test_health_requires_every_declared_local_signal);
  RUN_TEST(test_health_accepts_explicit_safe_configuration_degradation);
  RUN_TEST(test_health_does_not_depend_on_backends_network_link_or_nfc);
  RUN_TEST(test_fatal_initialization_fails_before_the_window);
  RUN_TEST(test_factory_reset_recovery_is_not_classified_as_ota_failure);
  RUN_TEST(test_upload_cleanup_requires_authoritative_terminal_state);
  RUN_TEST(
      test_upload_cleanup_recognizes_exact_failed_cleared_and_validated_states);
  RUN_TEST(
      test_upload_cleanup_rejects_stale_or_mismatched_generation_evidence);
  RUN_TEST(
      test_cleared_generation_persist_failure_is_terminal_and_monotonic);
  RUN_TEST(
      test_finish_timeout_late_validation_then_queued_abort_is_monotonic);
  return UNITY_END();
}

#include <unity.h>

#include <array>
#include <string>

#include "application/operation_registry.hpp"

namespace {
using opentag::application::OperationKind;
using opentag::application::OperationRegistry;
using opentag::application::OperationState;
using opentag::core::ErrorCategory;
}  // namespace

void setUp() {}
void tearDown() {}

void test_operations_are_correlated_and_newest_first() {
  OperationRegistry registry;
  const auto first = registry.begin(OperationKind::configuration, 10U);
  const auto second = registry.begin(OperationKind::scale_tare, 20U);
  registry.mark_running(first, 30U);
  registry.succeed(first, 40U, "saved");

  const auto records = registry.snapshot();

  TEST_ASSERT_EQUAL_UINT64(second, records[0].id);
  TEST_ASSERT_EQUAL_UINT64(first, records[1].id);
  TEST_ASSERT_EQUAL(
      static_cast<int>(OperationState::succeeded),
      static_cast<int>(registry.get(first)->state));
  TEST_ASSERT_EQUAL_STRING("saved", registry.get(first)->message.c_str());
}

void test_statistics_are_coherent_across_every_operation_state() {
  OperationRegistry registry;
  const auto empty = registry.statistics();
  TEST_ASSERT_EQUAL_UINT(0U, empty.used);
  TEST_ASSERT_EQUAL_UINT64(0U, empty.revision);

  const auto queued = registry.begin(OperationKind::configuration, 1U);
  const auto running = registry.begin(OperationKind::scale_tare, 2U);
  const auto succeeded = registry.begin(OperationKind::backend_probe, 3U);
  const auto failed = registry.begin(OperationKind::nfc_read, 4U);
  const auto confirmation = registry.begin(
      OperationKind::toolhead_assignment, 5U);
  TEST_ASSERT_NOT_EQUAL_UINT64(0U, queued);
  registry.mark_running(running, 6U);
  registry.succeed(succeeded, 7U, "complete");
  registry.fail(
      failed,
      8U,
      {ErrorCategory::nfc_communication, "reader unavailable", true});
  registry.require_confirmation(confirmation, 9U, "replace mapping");

  const auto statistics = registry.statistics();
  TEST_ASSERT_EQUAL_UINT(5U, statistics.used);
  TEST_ASSERT_EQUAL_UINT(1U, statistics.queued);
  TEST_ASSERT_EQUAL_UINT(1U, statistics.running);
  TEST_ASSERT_EQUAL_UINT(3U, statistics.terminal);
  TEST_ASSERT_EQUAL_UINT(1U, statistics.confirmation_required);
  TEST_ASSERT_EQUAL_UINT(
      statistics.used,
      statistics.queued + statistics.running + statistics.terminal);
  TEST_ASSERT_EQUAL_UINT64(9U, statistics.revision);
  TEST_ASSERT_EQUAL_UINT64(registry.revision(), statistics.revision);
}

void test_capacity_is_bounded_and_old_terminal_records_expire() {
  OperationRegistry registry;
  std::uint64_t first = 0U;
  for (std::size_t index = 0U; index < OperationRegistry::capacity + 3U;
       ++index) {
    const auto id = registry.begin(OperationKind::backend_probe, index);
    TEST_ASSERT_NOT_EQUAL_UINT64(0U, id);
    registry.succeed(id, index, "complete");
    if (index == 0U) first = id;
  }

  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity, registry.snapshot().size());
  TEST_ASSERT_FALSE(registry.get(first).has_value());
}

void test_capacity_rejects_without_overwriting_nonterminal_records() {
  OperationRegistry registry;
  std::array<std::uint64_t, OperationRegistry::capacity> active{};
  for (std::size_t index = 0U; index < active.size(); ++index) {
    active[index] = registry.begin(OperationKind::backend_probe, index);
    TEST_ASSERT_NOT_EQUAL_UINT64(0U, active[index]);
    if ((index % 2U) != 0U) registry.mark_running(active[index], index);
  }

  const auto revision_before_rejection = registry.revision();
  TEST_ASSERT_EQUAL_UINT64(0U, registry.begin(OperationKind::scale_tare, 100U));
  TEST_ASSERT_EQUAL_UINT64(revision_before_rejection, registry.revision());
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity, registry.snapshot().size());
  const auto full = registry.statistics();
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity, full.used);
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity / 2U, full.queued);
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity / 2U, full.running);
  TEST_ASSERT_EQUAL_UINT(0U, full.terminal);
  TEST_ASSERT_EQUAL_UINT64(revision_before_rejection, full.revision);
  for (const auto id : active) TEST_ASSERT_TRUE(registry.get(id).has_value());

  registry.require_confirmation(active[4], 101U, "confirm");
  const auto confirmation = registry.statistics();
  TEST_ASSERT_EQUAL_UINT(1U, confirmation.terminal);
  TEST_ASSERT_EQUAL_UINT(1U, confirmation.confirmation_required);
  const auto replacement = registry.begin(OperationKind::scale_tare, 102U);
  TEST_ASSERT_EQUAL_UINT64(active.back() + 1U, replacement);
  TEST_ASSERT_FALSE(registry.get(active[4]).has_value());
  for (std::size_t index = 0U; index < active.size(); ++index) {
    if (index != 4U) TEST_ASSERT_TRUE(registry.get(active[index]).has_value());
  }
  const auto replaced = registry.statistics();
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity, replaced.used);
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity / 2U, replaced.queued);
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity / 2U, replaced.running);
  TEST_ASSERT_EQUAL_UINT(0U, replaced.terminal);
  TEST_ASSERT_EQUAL_UINT(0U, replaced.confirmation_required);

  const auto newest = registry.snapshot(5U);
  TEST_ASSERT_EQUAL_UINT(5U, newest.size());
  TEST_ASSERT_EQUAL_UINT64(replacement, newest.front().id);
  for (std::size_t index = 1U; index < newest.size(); ++index) {
    TEST_ASSERT_TRUE(newest[index - 1U].id > newest[index].id);
  }
}

void test_thousand_terminal_operations_remain_bounded_and_newest_first() {
  OperationRegistry registry;
  std::uint64_t newest = 0U;
  for (std::uint32_t index = 0U; index < 1000U; ++index) {
    newest = registry.begin(OperationKind::configuration, index);
    TEST_ASSERT_NOT_EQUAL_UINT64(0U, newest);
    registry.succeed(newest, index, "complete");
  }
  const auto records = registry.snapshot();
  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity, records.size());
  TEST_ASSERT_EQUAL_UINT64(newest, records.front().id);
  for (std::size_t index = 1U; index < records.size(); ++index) {
    TEST_ASSERT_TRUE(records[index - 1U].id > records[index].id);
  }
}

void test_errors_and_messages_are_bounded() {
  OperationRegistry registry;
  const auto id = registry.begin(OperationKind::nfc_read, 1U);
  registry.fail(
      id,
      2U,
      {ErrorCategory::configuration, std::string(400U, 'x'), false});

  const auto record = registry.get(id);

  TEST_ASSERT_TRUE(record.has_value());
  TEST_ASSERT_EQUAL(
      static_cast<int>(OperationState::failed),
      static_cast<int>(record->state));
  TEST_ASSERT_TRUE(record->error.has_value());
  TEST_ASSERT_EQUAL_UINT(
      OperationRegistry::maximum_message_bytes,
      record->error->message.size());
}

void test_terminal_operation_outcomes_are_immutable() {
  OperationRegistry registry;
  const auto id = registry.begin(OperationKind::firmware_upload, 1U);
  registry.mark_running(id, 2U, "validating");
  registry.succeed(id, 3U, "validated");
  const auto terminal_revision = registry.revision();

  registry.fail(
      id,
      4U,
      {ErrorCategory::firmware_update, "late cleanup", true});
  registry.mark_running(id, 5U, "late callback");
  registry.require_confirmation(id, 6U, "late confirmation");

  const auto record = registry.get(id);
  TEST_ASSERT_TRUE(record.has_value());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(OperationState::succeeded),
      static_cast<int>(record->state));
  TEST_ASSERT_EQUAL_STRING("validated", record->message.c_str());
  TEST_ASSERT_EQUAL_UINT64(terminal_revision, registry.revision());
}

void test_operation_ids_can_advance_past_a_durable_ota_operation() {
  OperationRegistry registry;
  registry.reserve_ids_above(900U);
  TEST_ASSERT_EQUAL_UINT64(
      901U, registry.begin(OperationKind::firmware_upload, 1U));
  registry.reserve_ids_above(12U);
  TEST_ASSERT_EQUAL_UINT64(
      902U, registry.begin(OperationKind::firmware_reboot, 2U));
  TEST_ASSERT_EQUAL_STRING(
      "firmware_cancel",
      opentag::application::to_string(OperationKind::firmware_cancel));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_operations_are_correlated_and_newest_first);
  RUN_TEST(test_statistics_are_coherent_across_every_operation_state);
  RUN_TEST(test_capacity_is_bounded_and_old_terminal_records_expire);
  RUN_TEST(test_capacity_rejects_without_overwriting_nonterminal_records);
  RUN_TEST(test_thousand_terminal_operations_remain_bounded_and_newest_first);
  RUN_TEST(test_errors_and_messages_are_bounded);
  RUN_TEST(test_terminal_operation_outcomes_are_immutable);
  RUN_TEST(test_operation_ids_can_advance_past_a_durable_ota_operation);
  return UNITY_END();
}

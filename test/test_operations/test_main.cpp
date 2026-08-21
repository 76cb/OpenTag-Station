#include <unity.h>

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

void test_capacity_is_bounded_and_old_records_expire() {
  OperationRegistry registry;
  std::uint64_t first = 0U;
  for (std::size_t index = 0U; index < OperationRegistry::capacity + 3U;
       ++index) {
    const auto id = registry.begin(OperationKind::backend_probe, index);
    if (index == 0U) first = id;
  }

  TEST_ASSERT_EQUAL_UINT(OperationRegistry::capacity, registry.snapshot().size());
  TEST_ASSERT_FALSE(registry.get(first).has_value());
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
  RUN_TEST(test_capacity_is_bounded_and_old_records_expire);
  RUN_TEST(test_errors_and_messages_are_bounded);
  RUN_TEST(test_operation_ids_can_advance_past_a_durable_ota_operation);
  return UNITY_END();
}

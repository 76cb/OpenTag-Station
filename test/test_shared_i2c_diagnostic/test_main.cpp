#include <unity.h>

#include <cstdint>

#include "diagnostics/shared_i2c_diagnostic.hpp"

using namespace opentag::diagnostics::shared_i2c;

void setUp() {}
void tearDown() {}

void test_wire_status_distinguishes_ack_nack_and_bus_error() {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProbeResult::ack),
      static_cast<int>(classify_wire_status(0U)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProbeResult::nack),
      static_cast<int>(classify_wire_status(2U)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProbeResult::nack),
      static_cast<int>(classify_wire_status(3U)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProbeResult::bus_error),
      static_cast<int>(classify_wire_status(4U)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProbeResult::bus_error),
      static_cast<int>(classify_wire_status(5U)));
}

void test_dual_bus_contract_is_fixed_and_independent() {
  TEST_ASSERT_EQUAL_UINT32(100000U, scale_clock_hz);
  TEST_ASSERT_EQUAL_UINT32(100000U, nfc_clock_hz);
  TEST_ASSERT_EQUAL_INT8(10, scale_sda_gpio);
  TEST_ASSERT_EQUAL_INT8(11, scale_scl_gpio);
  TEST_ASSERT_EQUAL_INT8(13, nfc_sda_gpio);
  TEST_ASSERT_EQUAL_INT8(14, nfc_scl_gpio);
  TEST_ASSERT_EQUAL_HEX8(0x2AU, nau7802_address);
  TEST_ASSERT_EQUAL_HEX8(0x50U, st25r3916b_address);
}

void test_snapshot_keeps_bus_state_and_errors_independent() {
  Snapshot snapshot;
  snapshot.scale_sda_idle = LineState::high;
  snapshot.nfc_sda_idle = LineState::low;
  snapshot.scale_bus_error_count = 2U;
  snapshot.nfc_bus_error_count = 3U;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineState::high), static_cast<int>(snapshot.scale_sda_idle));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineState::low), static_cast<int>(snapshot.nfc_sda_idle));
  TEST_ASSERT_EQUAL_UINT32(2U, snapshot.scale_bus_error_count);
  TEST_ASSERT_EQUAL_UINT32(3U, snapshot.nfc_bus_error_count);
}

void test_st25r3916b_identity_and_read_framing_are_decoded() {
  TEST_ASSERT_EQUAL_HEX8(0x7FU, st25r3916b_read_mode(st25r3916b_identity_register));
  const auto identity = decode_st25r3916b_identity(0x31U);
  TEST_ASSERT_EQUAL_HEX8(0x06U, identity.product);
  TEST_ASSERT_EQUAL_HEX8(0x01U, identity.revision);
  TEST_ASSERT_TRUE(identity.is_st25r3916b());
  TEST_ASSERT_FALSE(decode_st25r3916b_identity(0x18U).is_st25r3916b());
}

void test_diagnostic_uid_formatter_preserves_canonical_order() {
  const std::array<std::uint8_t, 8U> uid{
      0xE0U, 0x04U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U};
  const auto formatted = format_diagnostic_uid(uid);
  TEST_ASSERT_EQUAL_STRING("E0:04:01:02:03:04:05:06", formatted.data());
}

void test_zero_device_inventory_is_representable_as_a_pass() {
  Snapshot snapshot;
  snapshot.iso15693_inventory_result = CheckResult::pass;
  snapshot.tag_detected = TagDetected::no;
  snapshot.devices_found = 0U;
  snapshot.failure_stage = FailureStage::none;

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CheckResult::pass),
      static_cast<int>(snapshot.iso15693_inventory_result));
  TEST_ASSERT_EQUAL_STRING("NO", to_string(snapshot.tag_detected));
  TEST_ASSERT_EQUAL_UINT8(0U, snapshot.devices_found);
  TEST_ASSERT_EQUAL_STRING("NONE", to_string(snapshot.failure_stage));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wire_status_distinguishes_ack_nack_and_bus_error);
  RUN_TEST(test_dual_bus_contract_is_fixed_and_independent);
  RUN_TEST(test_snapshot_keeps_bus_state_and_errors_independent);
  RUN_TEST(test_st25r3916b_identity_and_read_framing_are_decoded);
  RUN_TEST(test_diagnostic_uid_formatter_preserves_canonical_order);
  RUN_TEST(test_zero_device_inventory_is_representable_as_a_pass);
  return UNITY_END();
}

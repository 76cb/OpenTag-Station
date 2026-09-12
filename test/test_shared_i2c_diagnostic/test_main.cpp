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

void test_standard_system_information_parses_geometry_and_wire_uid() {
  const std::uint8_t response[]{
      0x00U, 0x0FU,
      0xD4U, 0xD8U, 0x27U, 0x66U, 0x08U, 0x01U, 0x04U, 0xE0U,
      0x12U, 0x34U, 0x4FU, 0x03U, 0x01U};
  NfcvSystemInformation information;
  TEST_ASSERT_TRUE(parse_nfcv_system_information(
      response, sizeof(response), false, information));
  TEST_ASSERT_TRUE(information.memory_size_present);
  TEST_ASSERT_FALSE(information.used_extended_command);
  TEST_ASSERT_EQUAL_UINT32(80U, information.block_count);
  TEST_ASSERT_EQUAL_UINT16(4U, information.block_size);
  TEST_ASSERT_EQUAL_HEX8(0xD4U, information.wire_uid[0]);
  TEST_ASSERT_EQUAL_HEX8(0xE0U, information.wire_uid[7]);
}

void test_extended_system_information_parses_two_byte_block_count() {
  const std::uint8_t response[]{
      0x00U, 0x04U,
      0xD4U, 0xD8U, 0x27U, 0x66U, 0x08U, 0x01U, 0x04U, 0xE0U,
      0xFFU, 0x01U, 0x07U};
  NfcvSystemInformation information;
  TEST_ASSERT_TRUE(parse_nfcv_system_information(
      response, sizeof(response), true, information));
  TEST_ASSERT_TRUE(information.used_extended_command);
  TEST_ASSERT_EQUAL_UINT32(512U, information.block_count);
  TEST_ASSERT_EQUAL_UINT16(8U, information.block_size);
}

void test_system_information_rejects_error_and_truncated_memory_size() {
  const std::uint8_t tag_error[]{
      0x01U, 0x04U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  const std::uint8_t truncated[]{
      0x00U, 0x04U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x4FU};
  NfcvSystemInformation information;
  TEST_ASSERT_FALSE(parse_nfcv_system_information(
      tag_error, sizeof(tag_error), false, information));
  TEST_ASSERT_FALSE(parse_nfcv_system_information(
      truncated, sizeof(truncated), false, information));
}

void test_read_response_checks_status_and_exact_length_before_copy() {
  const std::uint8_t response[]{0x00U, 0x10U, 0x20U, 0x30U, 0x40U};
  std::uint8_t destination[4]{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReadResponseResult::pass),
      static_cast<int>(copy_nfcv_read_response(
          response, sizeof(response), destination, sizeof(destination))));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(response + 1U, destination, sizeof(destination));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReadResponseResult::wrong_length),
      static_cast<int>(copy_nfcv_read_response(
          response, sizeof(response) - 1U, destination, sizeof(destination))));
  const std::uint8_t tag_error[]{0x01U, 0x10U, 0x20U, 0x30U, 0x40U};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReadResponseResult::tag_error),
      static_cast<int>(copy_nfcv_read_response(
          tag_error, sizeof(tag_error), destination, sizeof(destination))));
}

void test_diagnostic_checksum_is_stable_and_formatted() {
  const std::uint8_t bytes[]{0x00U, 0x01U, 0x02U, 0x03U};
  const auto checksum = diagnostic_checksum(bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_HEX32(0xC3AA51B1U, checksum);
  TEST_ASSERT_EQUAL_STRING("C3AA51B1", format_diagnostic_checksum(checksum).data());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wire_status_distinguishes_ack_nack_and_bus_error);
  RUN_TEST(test_dual_bus_contract_is_fixed_and_independent);
  RUN_TEST(test_snapshot_keeps_bus_state_and_errors_independent);
  RUN_TEST(test_st25r3916b_identity_and_read_framing_are_decoded);
  RUN_TEST(test_diagnostic_uid_formatter_preserves_canonical_order);
  RUN_TEST(test_zero_device_inventory_is_representable_as_a_pass);
  RUN_TEST(test_standard_system_information_parses_geometry_and_wire_uid);
  RUN_TEST(test_extended_system_information_parses_two_byte_block_count);
  RUN_TEST(test_system_information_rejects_error_and_truncated_memory_size);
  RUN_TEST(test_read_response_checks_status_and_exact_length_before_copy);
  RUN_TEST(test_diagnostic_checksum_is_stable_and_formatted);
  return UNITY_END();
}

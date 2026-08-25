#include <unity.h>

#include <array>
#include <cstdint>

#include "diagnostics/shared_i2c_diagnostic.hpp"

namespace {

using namespace opentag::diagnostics::shared_i2c;

void record_all(ScanSummary& scan, const std::initializer_list<std::uint8_t> addresses) {
  for (const auto address : addresses) scan.record(address);
}

}  // namespace

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

void test_expected_shared_bus_scan_is_valid() {
  ScanSummary scan;
  record_all(scan, {nau7802_address, st25r3916b_address});
  TEST_ASSERT_FALSE(scan_is_implausible(scan));
}

void test_previous_multi_address_alias_pattern_is_invalid() {
  ScanSummary scan;
  record_all(
      scan,
      {0x08U, 0x10U, 0x11U, 0x20U, 0x24U, 0x25U, 0x28U,
       0x29U, 0x40U, 0x44U, 0x45U, 0x48U, 0x49U});
  TEST_ASSERT_TRUE(scan_is_implausible(scan));
}

void test_one_or_two_unexpected_addresses_are_reported_without_false_contention() {
  ScanSummary scan;
  record_all(scan, {nau7802_address, st25r3916b_address, 0x30U, 0x31U});
  TEST_ASSERT_FALSE(scan_is_implausible(scan));
  scan.record(0x32U);
  TEST_ASSERT_TRUE(scan_is_implausible(scan));
}

void test_st25r3916b_identity_and_read_framing_are_decoded() {
  TEST_ASSERT_EQUAL_HEX8(0x7FU, st25r3916b_read_mode(st25r3916b_identity_register));
  const auto identity = decode_st25r3916b_identity(0x31U);
  TEST_ASSERT_EQUAL_HEX8(0x06U, identity.product);
  TEST_ASSERT_EQUAL_HEX8(0x01U, identity.revision);
  TEST_ASSERT_TRUE(identity.is_st25r3916b());
  TEST_ASSERT_FALSE(decode_st25r3916b_identity(0x18U).is_st25r3916b());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wire_status_distinguishes_ack_nack_and_bus_error);
  RUN_TEST(test_expected_shared_bus_scan_is_valid);
  RUN_TEST(test_previous_multi_address_alias_pattern_is_invalid);
  RUN_TEST(test_one_or_two_unexpected_addresses_are_reported_without_false_contention);
  RUN_TEST(test_st25r3916b_identity_and_read_framing_are_decoded);
  return UNITY_END();
}

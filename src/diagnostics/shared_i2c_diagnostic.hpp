#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/byte_view.hpp"
#include "core/result.hpp"

namespace opentag::diagnostics::shared_i2c {

inline constexpr std::uint32_t scale_clock_hz = 100000U;
inline constexpr std::uint32_t nfc_clock_hz = 100000U;
inline constexpr std::int8_t scale_sda_gpio = 10;
inline constexpr std::int8_t scale_scl_gpio = 11;
inline constexpr std::int8_t nfc_sda_gpio = 13;
inline constexpr std::int8_t nfc_scl_gpio = 14;
inline constexpr std::uint8_t nau7802_address = 0x2AU;
inline constexpr std::uint8_t st25r3916b_address = 0x50U;
inline constexpr std::uint8_t st25r3916b_identity_register = 0x3FU;
inline constexpr std::uint8_t st25r3916b_set_default_command = 0xC1U;

enum class LineState : std::uint8_t {
  unknown,
  low,
  high,
};

enum class ProbeResult : std::uint8_t {
  pending,
  ack,
  nack,
  bus_error,
};

enum class CheckResult : std::uint8_t {
  pending,
  pass,
  fail,
  skipped,
};

enum class TagDetected : std::uint8_t {
  pending,
  no,
  yes,
  multiple,
};

enum class Phase : std::uint8_t {
  starting,
  line_state,
  scale_probe,
  nfc_probe,
  nfc_register_test,
  nau_test,
  rfal_initialize,
  nfcv_poller,
  coexistence,
  memory_read,
  initialization,
  complete,
};

enum class FailureStage : std::uint8_t {
  none,
  scale_sda_stuck_low,
  scale_scl_stuck_low,
  nfc_sda_stuck_low,
  nfc_scl_stuck_low,
  scale_wire_begin,
  nfc_wire_begin,
  nau_probe,
  nfc_probe,
  nfc_set_default,
  nfc_identity_read,
  nfc_identity_mismatch,
  nfc_irq_not_clear,
  nfc_irq_start_oscillator,
  nfc_irq_timeout,
  nfc_irq_status,
  nfc_irq_not_cleared,
  nau_initialize,
  rfal_initialize,
  nfcv_poller_initialize,
  rf_field_on,
  nfcv_presence,
  nfcv_inventory,
  nfcv_uid,
  nfc_uid_changed,
  nfcv_system_information,
  nfcv_invalid_geometry,
  nfcv_memory_read,
  nfcv_response_length,
  nfcv_uid_changed_during_read,
  nfcv_read_consistency,
  nfc_post_read_transport,
  nfc_post_inventory_probe,
  nfc_post_inventory_identity,
  rf_field_off,
  scale_sample,
  nfc_coexistence_probe,
  insufficient_scale_samples,
  image_generation,
  reference_vector_mismatch,
  tag_not_blank,
  uid_changed_before_write,
  geometry_changed,
  tag_locked_write_protected,
  write_authorization,
  block_write,
  block_verify,
  full_image_verify,
  post_write_decode,
  post_write_transport,
};

enum class InitializationAuthorizationResult : std::uint8_t {
  pass,
  uid_mismatch,
  checksum_mismatch,
  confirmation_mismatch,
};

struct NfcvSystemInformation {
  std::array<std::uint8_t, 8U> wire_uid{};
  std::uint32_t block_count{0U};
  std::uint16_t block_size{0U};
  bool memory_size_present{false};
  bool used_extended_command{false};
};

enum class ReadResponseResult : std::uint8_t {
  pass,
  invalid_argument,
  tag_error,
  wrong_length,
};

struct ChipIdentity {
  std::uint8_t raw{0U};
  std::uint8_t product{0U};
  std::uint8_t revision{0U};

  [[nodiscard]] bool is_st25r3916b() const { return product == 0x06U; }
};

struct Snapshot {
  std::uint32_t scale_clock_hz{shared_i2c::scale_clock_hz};
  LineState scale_sda_idle{LineState::unknown};
  LineState scale_scl_idle{LineState::unknown};
  ProbeResult nau_probe_result{ProbeResult::pending};
  std::uint32_t scale_bus_error_count{0U};
  std::uint32_t nfc_clock_hz{shared_i2c::nfc_clock_hz};
  LineState nfc_sda_idle{LineState::unknown};
  LineState nfc_scl_idle{LineState::unknown};
  ProbeResult nfc_probe_result{ProbeResult::pending};
  CheckResult nfc_identity_result{CheckResult::pending};
  CheckResult nfc_irq_result{CheckResult::pending};
  CheckResult rfal_initialize_result{CheckResult::pending};
  CheckResult nfcv_poller_result{CheckResult::pending};
  CheckResult rf_field_result{CheckResult::pending};
  CheckResult iso15693_inventory_result{CheckResult::pending};
  TagDetected tag_detected{TagDetected::pending};
  std::uint8_t devices_found{0U};
  std::array<char, 24U> uid{};
  std::uint32_t inventory_round_count{0U};
  std::uint32_t matching_uid_round_count{0U};
  bool uid_consistent{true};
  bool tag_removal_seen{false};
  bool tag_reinsertion_seen{false};
  CheckResult system_information_result{CheckResult::pending};
  CheckResult geometry_result{CheckResult::pending};
  std::uint32_t block_count{0U};
  std::uint16_t block_size{0U};
  std::uint32_t memory_capacity_bytes{0U};
  CheckResult first_memory_read_result{CheckResult::pending};
  CheckResult second_memory_read_result{CheckResult::pending};
  CheckResult memory_read_consistency_result{CheckResult::pending};
  std::uint32_t memory_bytes_read{0U};
  std::array<char, 9U> memory_checksum{};
  std::array<char, 24U> memory_uid{};
  bool memory_dump_available{false};
  CheckResult nau_communication_result{CheckResult::pending};
  CheckResult scale_after_test{CheckResult::pending};
  CheckResult nfc_after_scale{CheckResult::pending};
  ChipIdentity nfc_identity;
  std::uint32_t nfc_bus_error_count{0U};
  std::uint32_t scale_sample_count{0U};
  std::int32_t last_raw{0};
  std::uint32_t coexistence_elapsed_ms{0U};
  Phase phase{Phase::starting};
  FailureStage failure_stage{FailureStage::none};
};

[[nodiscard]] ProbeResult classify_wire_status(std::uint8_t status);
[[nodiscard]] constexpr std::uint8_t st25r3916b_read_mode(
    std::uint8_t register_address) {
  return static_cast<std::uint8_t>(0x40U | (register_address & 0x3FU));
}
[[nodiscard]] ChipIdentity decode_st25r3916b_identity(std::uint8_t raw);

[[nodiscard]] const char* to_string(LineState value);
[[nodiscard]] const char* to_string(ProbeResult value);
[[nodiscard]] const char* to_string(CheckResult value);
[[nodiscard]] const char* to_string(TagDetected value);
[[nodiscard]] const char* to_string(Phase value);
[[nodiscard]] const char* to_string(FailureStage value);

[[nodiscard]] std::array<char, 24U> format_diagnostic_uid(
    const std::array<std::uint8_t, 8U>& canonical_uid);

[[nodiscard]] bool parse_nfcv_system_information(
    const std::uint8_t* response,
    std::size_t response_length,
    bool extended,
    NfcvSystemInformation& information);

[[nodiscard]] ReadResponseResult copy_nfcv_read_response(
    const std::uint8_t* response,
    std::size_t response_length,
    std::uint8_t* destination,
    std::size_t expected_data_length);

[[nodiscard]] std::uint32_t diagnostic_checksum(
    const std::uint8_t* data,
    std::size_t length);

[[nodiscard]] std::array<char, 9U> format_diagnostic_checksum(
    std::uint32_t checksum);

[[nodiscard]] InitializationAuthorizationResult validate_initialization_authorization(
    const char* supplied_uid,
    const char* supplied_checksum,
    const char* supplied_confirmation,
    const char* expected_uid,
    const char* expected_checksum);

[[nodiscard]] bool is_zero_filled(core::ByteView bytes);

[[nodiscard]] bool matches_initialization_geometry(
    const NfcvSystemInformation& information,
    std::uint32_t expected_block_count,
    std::uint16_t expected_block_size);

[[nodiscard]] core::Result<std::vector<std::uint8_t>> build_initialization_target(
    core::ByteView current_image,
    core::ByteView initialization_image);

}  // namespace opentag::diagnostics::shared_i2c

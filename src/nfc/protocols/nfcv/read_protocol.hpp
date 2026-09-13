#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace opentag::nfc::nfcv {
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
}  // namespace opentag::nfc::nfcv


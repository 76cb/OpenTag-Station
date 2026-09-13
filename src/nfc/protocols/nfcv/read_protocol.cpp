#include "nfc/protocols/nfcv/read_protocol.hpp"
#include <cstring>

// Promoted unchanged from the physically validated dual-I2C diagnostic.
namespace opentag::nfc::nfcv {
std::array<char, 24U> format_diagnostic_uid(
    const std::array<std::uint8_t, 8U>& canonical_uid) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::array<char, 24U> output{};
  std::size_t offset = 0U;
  for (std::size_t index = 0U; index < canonical_uid.size(); ++index) {
    const auto byte = canonical_uid[index];
    output[offset++] = hex[(byte >> 4U) & 0x0FU];
    output[offset++] = hex[byte & 0x0FU];
    if (index + 1U < canonical_uid.size()) output[offset++] = ':';
  }
  output[offset] = '\0';
  return output;
}

bool parse_nfcv_system_information(
    const std::uint8_t* response,
    std::size_t response_length,
    bool extended,
    NfcvSystemInformation& information) {
  constexpr std::uint8_t response_error_flag = 0x01U;
  constexpr std::uint8_t dsfid_present = 0x01U;
  constexpr std::uint8_t afi_present = 0x02U;
  constexpr std::uint8_t memory_size_present = 0x04U;
  constexpr std::uint8_t ic_reference_present = 0x08U;

  information = {};
  information.used_extended_command = extended;
  if (response == nullptr || response_length < 10U ||
      (response[0] & response_error_flag) != 0U) {
    return false;
  }

  std::size_t offset = 1U;
  const auto information_flags = response[offset++];
  if (offset + information.wire_uid.size() > response_length) return false;
  std::memcpy(
      information.wire_uid.data(),
      response + offset,
      information.wire_uid.size());
  offset += information.wire_uid.size();

  if ((information_flags & dsfid_present) != 0U) {
    if (offset >= response_length) return false;
    ++offset;
  }
  if ((information_flags & afi_present) != 0U) {
    if (offset >= response_length) return false;
    ++offset;
  }
  if ((information_flags & memory_size_present) != 0U) {
    const std::size_t encoded_count_bytes = extended ? 2U : 1U;
    if (offset + encoded_count_bytes + 1U > response_length) return false;
    std::uint32_t encoded_count = response[offset++];
    if (extended) {
      encoded_count |= static_cast<std::uint32_t>(response[offset++]) << 8U;
    }
    information.block_count = encoded_count + 1U;
    information.block_size = static_cast<std::uint16_t>(response[offset++]) + 1U;
    information.memory_size_present = true;
  }
  if ((information_flags & ic_reference_present) != 0U) {
    if (offset >= response_length) return false;
    ++offset;
  }
  return true;
}

ReadResponseResult copy_nfcv_read_response(
    const std::uint8_t* response,
    std::size_t response_length,
    std::uint8_t* destination,
    std::size_t expected_data_length) {
  constexpr std::uint8_t response_error_flag = 0x01U;
  if (response == nullptr || destination == nullptr || expected_data_length == 0U) {
    return ReadResponseResult::invalid_argument;
  }
  if (response_length == 0U || (response[0] & response_error_flag) != 0U) {
    return ReadResponseResult::tag_error;
  }
  if (response_length != expected_data_length + 1U) {
    return ReadResponseResult::wrong_length;
  }
  std::memcpy(destination, response + 1U, expected_data_length);
  return ReadResponseResult::pass;
}

std::uint32_t diagnostic_checksum(const std::uint8_t* data, std::size_t length) {
  constexpr std::uint32_t fnv_offset_basis = 2166136261U;
  constexpr std::uint32_t fnv_prime = 16777619U;
  std::uint32_t checksum = fnv_offset_basis;
  if (data == nullptr) return checksum;
  for (std::size_t index = 0U; index < length; ++index) {
    checksum ^= data[index];
    checksum *= fnv_prime;
  }
  return checksum;
}

std::array<char, 9U> format_diagnostic_checksum(std::uint32_t checksum) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::array<char, 9U> output{};
  for (std::size_t index = 0U; index < 8U; ++index) {
    const auto shift = static_cast<unsigned>((7U - index) * 4U);
    output[index] = hex[(checksum >> shift) & 0x0FU];
  }
  output[8] = '\0';
  return output;
}
}  // namespace opentag::nfc::nfcv


#include "nfc/formats/openprinttag/initializer.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "nfc/formats/openprinttag/codec.hpp"

namespace opentag::nfc::openprinttag {
namespace {

core::Error invalid(const std::string& message) {
  return {core::ErrorCategory::invalid_openprinttag, message, false};
}

void append_unsigned(std::vector<std::uint8_t>& output, std::size_t value) {
  if (value < 24U) {
    output.push_back(static_cast<std::uint8_t>(value));
  } else if (value <= 0xFFU) {
    output.push_back(0x18U);
    output.push_back(static_cast<std::uint8_t>(value));
  } else if (value <= 0xFFFFU) {
    output.push_back(0x19U);
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  } else {
    output.push_back(0x1AU);
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  }
}

std::vector<std::uint8_t> encode_metadata(
    const std::optional<std::size_t>& main_offset,
    const std::optional<std::size_t>& auxiliary_offset) {
  const std::size_t entry_count =
      (main_offset.has_value() ? 1U : 0U) +
      (auxiliary_offset.has_value() ? 1U : 0U);
  std::vector<std::uint8_t> result;
  result.reserve(1U + entry_count * 4U);
  result.push_back(static_cast<std::uint8_t>(0xA0U | entry_count));
  if (main_offset.has_value()) {
    append_unsigned(result, 0U);
    append_unsigned(result, *main_offset);
  }
  if (auxiliary_offset.has_value()) {
    append_unsigned(result, 2U);
    append_unsigned(result, *auxiliary_offset);
  }
  return result;
}

}  // namespace

core::Result<NfcvInitializationImage> Initializer::generate(
    const NfcvInitializationConfig& config) {
  constexpr std::size_t capability_container_size = 4U;
  constexpr std::size_t terminator_size = 1U;
  constexpr std::size_t initial_tlv_header_size = 2U;
  constexpr std::size_t maximum_metadata_size = 8U;
  constexpr std::size_t mime_length = 28U;

  if (config.usable_bytes < 8U || config.usable_bytes > maximum_usable_bytes ||
      (config.usable_bytes % 8U) != 0U) {
    return core::Result<NfcvInitializationImage>::failure(invalid(
        "NFC-V usable capacity must be a non-zero multiple of 8 bytes no larger than 2040"));
  }
  if (config.block_size == 0U || config.block_size > config.usable_bytes) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("NFC-V initialization block size is invalid"));
  }
  if (config.auxiliary_region_bytes.has_value() &&
      *config.auxiliary_region_bytes <= 4U) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("OpenPrintTag auxiliary region must exceed 4 bytes"));
  }

  std::size_t tlv_header_size = initial_tlv_header_size;
  std::size_t ndef_message_size = config.usable_bytes -
      capability_container_size - terminator_size - tlv_header_size;
  if (ndef_message_size > 0xFEU) {
    tlv_header_size += 2U;
    ndef_message_size -= 2U;
  }

  // One MIME NDEF record: flags, type length, payload length, MIME type.
  std::size_t ndef_header_size = 3U + mime_length;
  if (ndef_message_size <= ndef_header_size) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("NFC-V capacity cannot contain an OpenPrintTag NDEF record"));
  }
  std::size_t payload_size = ndef_message_size - ndef_header_size;
  if (payload_size > 0xFFU) {
    ndef_header_size += 3U;
    if (payload_size <= 3U) {
      return core::Result<NfcvInitializationImage>::failure(
          invalid("OpenPrintTag NDEF payload length is invalid"));
    }
    payload_size -= 3U;
    if (payload_size <= 0xFFU) {
      return core::Result<NfcvInitializationImage>::failure(invalid(
          "NFC-V capacity falls into the unsupported NDEF short-record transition"));
    }
  }
  if (payload_size <= maximum_metadata_size) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("NFC-V capacity cannot contain the OpenPrintTag metadata and main regions"));
  }

  const std::size_t payload_offset =
      capability_container_size + tlv_header_size + ndef_header_size;
  std::optional<std::size_t> auxiliary_offset;
  if (config.auxiliary_region_bytes.has_value()) {
    if (*config.auxiliary_region_bytes > payload_size) {
      return core::Result<NfcvInitializationImage>::failure(
          invalid("OpenPrintTag auxiliary region exceeds the NDEF payload"));
    }
    const auto candidate = payload_size - *config.auxiliary_region_bytes;
    const auto alignment_adjustment =
        (payload_offset + candidate) % config.block_size;
    if (alignment_adjustment > candidate) {
      return core::Result<NfcvInitializationImage>::failure(
          invalid("OpenPrintTag auxiliary alignment leaves no metadata/main region"));
    }
    auxiliary_offset = candidate - alignment_adjustment;
  }

  const auto metadata = encode_metadata(
      config.metadata_region_bytes,
      auxiliary_offset);
  const std::size_t main_offset =
      config.metadata_region_bytes.value_or(metadata.size());
  if (main_offset < metadata.size() || main_offset >= payload_size) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("OpenPrintTag metadata allocation is too small or outside the payload"));
  }
  const std::size_t main_end = auxiliary_offset.value_or(payload_size);
  if (main_end < main_offset || main_end - main_offset <
          (auxiliary_offset.has_value() ? 4U : 8U)) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("OpenPrintTag main region is too small"));
  }

  std::vector<std::uint8_t> payload(payload_size, 0U);
  std::copy(metadata.begin(), metadata.end(), payload.begin());
  payload[main_offset] = 0xA0U;
  if (auxiliary_offset.has_value()) payload[*auxiliary_offset] = 0xA0U;

  NfcvInitializationImage result;
  result.bytes.reserve(config.usable_bytes);
  result.bytes.push_back(0xE1U);
  result.bytes.push_back(0x40U);
  result.bytes.push_back(static_cast<std::uint8_t>(config.usable_bytes / 8U));
  result.bytes.push_back(0x01U);  // SLIX2 supports Read Multiple Blocks.
  result.bytes.push_back(0x03U);
  if (ndef_message_size <= 0xFEU) {
    result.bytes.push_back(static_cast<std::uint8_t>(ndef_message_size));
  } else {
    result.bytes.push_back(0xFFU);
    result.bytes.push_back(static_cast<std::uint8_t>((ndef_message_size >> 8U) & 0xFFU));
    result.bytes.push_back(static_cast<std::uint8_t>(ndef_message_size & 0xFFU));
  }

  const bool short_record = payload_size <= 0xFFU;
  result.bytes.push_back(static_cast<std::uint8_t>(
      0x80U | 0x40U | (short_record ? 0x10U : 0U) | 0x02U));
  result.bytes.push_back(static_cast<std::uint8_t>(mime_length));
  if (short_record) {
    result.bytes.push_back(static_cast<std::uint8_t>(payload_size));
  } else {
    result.bytes.push_back(static_cast<std::uint8_t>((payload_size >> 24U) & 0xFFU));
    result.bytes.push_back(static_cast<std::uint8_t>((payload_size >> 16U) & 0xFFU));
    result.bytes.push_back(static_cast<std::uint8_t>((payload_size >> 8U) & 0xFFU));
    result.bytes.push_back(static_cast<std::uint8_t>(payload_size & 0xFFU));
  }
  result.bytes.insert(
      result.bytes.end(),
      reinterpret_cast<const std::uint8_t*>(mime_type),
      reinterpret_cast<const std::uint8_t*>(mime_type) + std::strlen(mime_type));
  result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());
  result.bytes.push_back(0xFEU);

  if (result.bytes.size() != config.usable_bytes) {
    return core::Result<NfcvInitializationImage>::failure(
        invalid("OpenPrintTag image size does not match the upstream initializer"));
  }
  result.payload_offset = payload_offset;
  result.payload_size = payload_size;
  result.metadata_size = metadata.size();
  result.main_region_offset = main_offset;
  result.auxiliary_region_offset = auxiliary_offset;
  return core::Result<NfcvInitializationImage>::success(std::move(result));
}

}  // namespace opentag::nfc::openprinttag

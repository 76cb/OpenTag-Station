#include "nfc/protocols/nfcv/tag.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace opentag::nfc::nfcv {
namespace {

core::Error nfc_error(const std::string& message, bool retryable = false) {
  return {core::ErrorCategory::nfc_communication, message, retryable};
}

core::Error unsupported(const std::string& message) {
  return {core::ErrorCategory::unsupported_tag, message, false};
}

core::Error removed(const std::string& message) {
  return {core::ErrorCategory::tag_removed, message, true};
}

core::Error multiple(const std::string& message) {
  return {core::ErrorCategory::multiple_tags, message, true};
}

}  // namespace

core::Result<Uid> Uid::from_network_order(core::ByteView value) {
  if (value.size != size) {
    return core::Result<Uid>::failure(unsupported("NFC-V UID must contain exactly 8 bytes"));
  }
  Uid result;
  std::copy(value.data, value.data + value.size, result.bytes.begin());
  if (result.bytes.front() != 0xE0U) {
    return core::Result<Uid>::failure(unsupported("NFC-V UID does not use the ISO 15693 E0 prefix"));
  }
  return core::Result<Uid>::success(result);
}

core::Result<Uid> Uid::from_wire_lsb_first(core::ByteView value) {
  if (value.size != size) {
    return core::Result<Uid>::failure(unsupported("NFC-V UID must contain exactly 8 bytes"));
  }
  std::array<std::uint8_t, size> normalized{};
  std::reverse_copy(value.data, value.data + value.size, normalized.begin());
  return from_network_order(core::ByteView(normalized.data(), normalized.size()));
}

std::string Uid::hex() const {
  std::ostringstream output;
  output << std::hex << std::uppercase << std::setfill('0');
  for (const auto byte : bytes) {
    output << std::setw(2) << static_cast<unsigned>(byte);
  }
  return output.str();
}

core::Result<void> TagGeometry::validate() const {
  if (block_size == 0U || block_count == 0U || block_size > WritePlan::maximum_image_size ||
      block_count > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U ||
      block_count > WritePlan::maximum_image_size / block_size) {
    return core::Result<void>::failure(unsupported("NFC-V tag geometry is invalid or too large"));
  }
  return core::Result<void>::success();
}

core::Result<WritePlan> WritePlan::build(
    core::ByteView original,
    core::ByteView updated,
    TagGeometry geometry,
    const std::vector<bool>& locked_blocks) {
  const auto geometry_status = geometry.validate();
  if (!geometry_status.ok()) {
    return core::Result<WritePlan>::failure(geometry_status.error());
  }
  if (original.size != geometry.capacity() || updated.size != geometry.capacity()) {
    return core::Result<WritePlan>::failure(
        unsupported("NFC-V images do not match the detected tag geometry"));
  }
  if (!locked_blocks.empty() && locked_blocks.size() != geometry.block_count) {
    return core::Result<WritePlan>::failure(
        unsupported("NFC-V lock map does not match the detected tag geometry"));
  }

  WritePlan result;
  result.geometry_ = geometry;
  for (std::size_t block = 0U; block < geometry.block_count; ++block) {
    const std::size_t offset = block * geometry.block_size;
    if (std::equal(
            original.data + offset,
            original.data + offset + geometry.block_size,
            updated.data + offset)) {
      continue;
    }
    if (!locked_blocks.empty() && locked_blocks[block]) {
      return core::Result<WritePlan>::failure({
          core::ErrorCategory::tag_write_protected,
          "OpenPrintTag update touches locked NFC-V block " + std::to_string(block),
          false,
      });
    }
    result.blocks_.push_back({
        static_cast<std::uint16_t>(block),
        std::vector<std::uint8_t>(
            updated.data + offset,
            updated.data + offset + geometry.block_size),
    });
  }
  return core::Result<WritePlan>::success(std::move(result));
}

core::Result<DetectedTag> Reader::detect_one(std::uint32_t timeout_ms) {
  if (timeout_ms == 0U) {
    return core::Result<DetectedTag>::failure(nfc_error("NFC-V operation timeout must be non-zero"));
  }
  const auto inventory = transport_.inventory(timeout_ms);
  if (!inventory.ok()) {
    (void)transport_.reset_rf_field(timeout_ms);
    return core::Result<DetectedTag>::failure(inventory.error());
  }
  if (inventory.value().empty()) {
    return core::Result<DetectedTag>::failure(removed("no NFC-V tag is present"));
  }
  if (inventory.value().size() != 1U) {
    return core::Result<DetectedTag>::failure(
        multiple("multiple NFC-V tags detected; remove extra tags and retry"));
  }

  const auto geometry = transport_.read_geometry(inventory.value().front(), timeout_ms);
  if (!geometry.ok()) {
    (void)transport_.reset_rf_field(timeout_ms);
    return core::Result<DetectedTag>::failure(geometry.error());
  }
  const auto geometry_status = geometry.value().validate();
  if (!geometry_status.ok()) {
    return core::Result<DetectedTag>::failure(geometry_status.error());
  }
  const auto locks = transport_.read_block_locks(
      inventory.value().front(), geometry.value(), timeout_ms);
  if (!locks.ok()) {
    (void)transport_.reset_rf_field(timeout_ms);
    return core::Result<DetectedTag>::failure(locks.error());
  }
  if (locks.value().size() != geometry.value().block_count) {
    return core::Result<DetectedTag>::failure(
        nfc_error("NFC-V block security status does not match tag geometry"));
  }
  return core::Result<DetectedTag>::success({
      inventory.value().front(), geometry.value(), locks.value()});
}

core::Result<std::vector<std::uint8_t>> Reader::read_image(
    const DetectedTag& tag,
    std::uint32_t timeout_ms) {
  const auto geometry_status = tag.geometry.validate();
  if (!geometry_status.ok()) {
    return core::Result<std::vector<std::uint8_t>>::failure(geometry_status.error());
  }
  if (timeout_ms == 0U || tag.locked_blocks.size() != tag.geometry.block_count) {
    return core::Result<std::vector<std::uint8_t>>::failure(
        nfc_error("NFC-V read request is internally inconsistent"));
  }
  const auto image = transport_.read_multiple_blocks(
      tag.uid,
      0U,
      static_cast<std::uint16_t>(tag.geometry.block_count),
      tag.geometry.block_size,
      timeout_ms);
  if (!image.ok()) {
    (void)transport_.reset_rf_field(timeout_ms);
    return core::Result<std::vector<std::uint8_t>>::failure(image.error());
  }
  if (image.value().size() != tag.geometry.capacity()) {
    return core::Result<std::vector<std::uint8_t>>::failure(
        nfc_error("NFC-V multi-block read returned an unexpected byte count", true));
  }
  return image;
}

core::Result<void> VerifiedWriter::execute(
    const Uid& expected_uid,
    const WritePlan& plan,
    std::uint32_t operation_timeout_ms) {
  if (operation_timeout_ms == 0U) {
    return core::Result<void>::failure(nfc_error("NFC-V operation timeout must be non-zero"));
  }
  const auto geometry_status = plan.geometry().validate();
  if (!geometry_status.ok()) {
    return core::Result<void>::failure(geometry_status.error());
  }

  for (const auto& block : plan.blocks()) {
    if (block.block_index >= plan.geometry().block_count ||
        block.data.size() != plan.geometry().block_size) {
      return core::Result<void>::failure(nfc_error("NFC-V write plan is internally inconsistent"));
    }

    const auto present = transport_.inventory(operation_timeout_ms);
    if (!present.ok()) {
      return core::Result<void>::failure(present.error());
    }
    if (present.value().empty()) {
      return core::Result<void>::failure(removed("expected NFC-V tag is no longer present"));
    }
    if (present.value().size() != 1U) {
      return core::Result<void>::failure(
          multiple("multiple NFC-V tags detected during update"));
    }
    if (present.value().front() != expected_uid) {
      return core::Result<void>::failure(removed(
          "NFC-V tag changed during update; expected " + expected_uid.hex() +
          " but found " + present.value().front().hex()));
    }

    const auto written = transport_.write_single_block(
        expected_uid, block.block_index, core::ByteView(block.data), operation_timeout_ms);
    if (!written.ok()) {
      (void)transport_.reset_rf_field(operation_timeout_ms);
      return written;
    }
    const auto verified = transport_.read_single_block(
        expected_uid, block.block_index, plan.geometry().block_size, operation_timeout_ms);
    if (!verified.ok()) {
      (void)transport_.reset_rf_field(operation_timeout_ms);
      return core::Result<void>::failure(verified.error());
    }
    if (verified.value() != block.data) {
      (void)transport_.reset_rf_field(operation_timeout_ms);
      return core::Result<void>::failure(nfc_error(
          "NFC-V readback mismatch for block " + std::to_string(block.block_index), true));
    }
  }
  return core::Result<void>::success();
}

}  // namespace opentag::nfc::nfcv

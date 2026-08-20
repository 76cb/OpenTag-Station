#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/byte_view.hpp"
#include "core/result.hpp"

namespace opentag::nfc::nfcv {

struct Uid {
  static constexpr std::size_t size = 8U;
  std::array<std::uint8_t, size> bytes{};

  [[nodiscard]] static core::Result<Uid> from_network_order(core::ByteView value);
  [[nodiscard]] static core::Result<Uid> from_wire_lsb_first(core::ByteView value);
  [[nodiscard]] std::string hex() const;

  [[nodiscard]] bool operator==(const Uid& other) const { return bytes == other.bytes; }
  [[nodiscard]] bool operator!=(const Uid& other) const { return !(*this == other); }
};

struct TagGeometry {
  std::size_t block_size{0};
  std::size_t block_count{0};

  [[nodiscard]] std::size_t capacity() const { return block_size * block_count; }
  [[nodiscard]] core::Result<void> validate() const;
};

struct BlockWrite {
  std::uint16_t block_index{0};
  std::vector<std::uint8_t> data;
};

class WritePlan {
 public:
  static constexpr std::size_t maximum_image_size = 4096U;

  [[nodiscard]] static core::Result<WritePlan> build(
      core::ByteView original,
      core::ByteView updated,
      TagGeometry geometry,
      const std::vector<bool>& locked_blocks = {});

  [[nodiscard]] const TagGeometry& geometry() const { return geometry_; }
  [[nodiscard]] const std::vector<BlockWrite>& blocks() const { return blocks_; }
  [[nodiscard]] bool empty() const { return blocks_.empty(); }

 private:
  TagGeometry geometry_;
  std::vector<BlockWrite> blocks_;
};

class ITagTransport {
 public:
  virtual ~ITagTransport() = default;

  // Inventory returns every tag observed in one anticollision round. Callers
  // must reject zero or multiple tags rather than choosing one implicitly.
  [[nodiscard]] virtual core::Result<std::vector<Uid>> inventory(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<TagGeometry> read_geometry(
      const Uid& uid,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>> read_single_block(
      const Uid& uid,
      std::uint16_t block_index,
      std::size_t expected_block_size,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>> read_multiple_blocks(
      const Uid& uid,
      std::uint16_t first_block,
      std::uint16_t block_count,
      std::size_t expected_block_size,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<std::vector<bool>> read_block_locks(
      const Uid& uid,
      const TagGeometry& geometry,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> write_single_block(
      const Uid& uid,
      std::uint16_t block_index,
      core::ByteView data,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> reset_rf_field(
      std::uint32_t timeout_ms) = 0;
};

struct DetectedTag {
  Uid uid;
  TagGeometry geometry;
  std::vector<bool> locked_blocks;
};

class Reader {
 public:
  explicit Reader(ITagTransport& transport) : transport_(transport) {}

  [[nodiscard]] core::Result<DetectedTag> detect_one(std::uint32_t timeout_ms);
  [[nodiscard]] core::Result<std::vector<std::uint8_t>> read_image(
      const DetectedTag& tag,
      std::uint32_t timeout_ms);

 private:
  ITagTransport& transport_;
};

class VerifiedWriter {
 public:
  explicit VerifiedWriter(ITagTransport& transport) : transport_(transport) {}

  [[nodiscard]] core::Result<void> execute(
      const Uid& expected_uid,
      const WritePlan& plan,
      std::uint32_t operation_timeout_ms);

 private:
  ITagTransport& transport_;
};

}  // namespace opentag::nfc::nfcv

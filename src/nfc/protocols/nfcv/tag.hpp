#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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
  static constexpr std::size_t maximum_image_size = 4096U;

  std::size_t block_size{0};
  std::size_t block_count{0};

  [[nodiscard]] std::size_t capacity() const { return block_size * block_count; }
  [[nodiscard]] core::Result<void> validate() const;
};

}  // namespace opentag::nfc::nfcv

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/byte_view.hpp"
#include "core/result.hpp"

namespace opentag::nfc::openprinttag {

struct CborSlice {
  std::size_t offset{0};
  std::size_t size{0};
};

struct CborMapEntry {
  std::uint64_t key{0};
  CborSlice encoded_key;
  CborSlice encoded_value;
};

class CborMapView {
 public:
  static constexpr std::size_t maximum_region_size = 512U;
  static constexpr std::size_t maximum_entries = 128U;
  static constexpr std::size_t maximum_nesting_depth = 12U;

  static core::Result<CborMapView> parse(core::ByteView region);

  [[nodiscard]] core::ByteView bytes() const { return bytes_; }
  [[nodiscard]] const std::vector<CborMapEntry>& entries() const {
    return entries_;
  }
  [[nodiscard]] std::size_t used_size() const { return used_size_; }
  [[nodiscard]] const CborMapEntry* find(std::uint64_t key) const;

  core::Result<std::uint64_t> read_unsigned(std::uint64_t key) const;
  core::Result<std::int64_t> read_integer(std::uint64_t key) const;
  core::Result<double> read_number(std::uint64_t key) const;
  core::Result<std::string> read_text(std::uint64_t key) const;
  core::Result<std::vector<std::uint8_t>> read_bytes(std::uint64_t key) const;
  core::Result<std::vector<std::uint64_t>> read_unsigned_array(
      std::uint64_t key,
      std::size_t maximum_items) const;
  core::Result<std::vector<double>> read_number_array(
      std::uint64_t key,
      std::size_t maximum_items) const;
  core::Result<bool> is_null(std::uint64_t key) const;

  // Rebuilds an indefinite map, retaining every untouched encoded key/value
  // byte-for-byte. The returned vector is padded to region_capacity with zeroes.
  core::Result<std::vector<std::uint8_t>> replace(
      std::uint64_t key,
      core::ByteView encoded_value,
      std::size_t region_capacity) const;

  static std::vector<std::uint8_t> encode_unsigned(std::uint64_t value);
  static std::vector<std::uint8_t> encode_number(double value);

 private:
  core::ByteView bytes_;
  std::vector<CborMapEntry> entries_;
  std::size_t used_size_{0};
};

}  // namespace opentag::nfc::openprinttag

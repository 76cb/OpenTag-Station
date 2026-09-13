#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/result.hpp"

namespace opentag::nfc::openprinttag {

// The encoder is pinned separately from the read codec because the latter still
// intentionally documents the older revision against which all decoded fields
// have been checked.
inline constexpr const char* initializer_reference_revision =
    "7e09cc38df1c8e7824a67f5b1ae93071f52519ad";

struct NfcvInitializationConfig {
  std::size_t usable_bytes{0U};
  std::size_t block_size{4U};
  std::optional<std::size_t> auxiliary_region_bytes;
  std::optional<std::size_t> metadata_region_bytes;
};

struct NfcvInitializationImage {
  std::vector<std::uint8_t> bytes;
  std::size_t payload_offset{0U};
  std::size_t payload_size{0U};
  std::size_t metadata_size{0U};
  std::size_t main_region_offset{0U};
  std::optional<std::size_t> auxiliary_region_offset;
};

class Initializer {
 public:
  static constexpr std::size_t maximum_usable_bytes = 2040U;

  // Produces the same no-URI NFC-V image as the pinned upstream
  // utils/nfc_initialize.py implementation. Empty main/auxiliary CBOR maps are
  // intentional: this diagnostic proves safe initialization, not tag content.
  [[nodiscard]] static core::Result<NfcvInitializationImage> generate(
      const NfcvInitializationConfig& config);
};

}  // namespace opentag::nfc::openprinttag

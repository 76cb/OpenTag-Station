#include "nfc/protocols/nfcv/tag.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace opentag::nfc::nfcv {
namespace {

core::Error unsupported(const std::string& message) {
  return {core::ErrorCategory::unsupported_tag, message, false};
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
  if (block_size == 0U || block_count == 0U || block_size > maximum_image_size ||
      block_count > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U ||
      block_count > maximum_image_size / block_size) {
    return core::Result<void>::failure(unsupported("NFC-V tag geometry is invalid or too large"));
  }
  return core::Result<void>::success();
}

}  // namespace opentag::nfc::nfcv

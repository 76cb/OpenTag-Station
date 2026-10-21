#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/byte_view.hpp"
#include "core/result.hpp"

namespace opentag::nfc::nfca {
// What the anticollision answer says about an ISO 14443A tag. Derived from
// SAK (SEL_RES) bits as NXP AN10833 "MIFARE type identification procedure"
// describes: bit 6 set means ISO 14443-4 (Type 4), bit 4 set means MIFARE
// Classic, SAK 0x00 is a Type 2 tag (NTAG, MIFARE Ultralight). Nothing here
// is read from the tag's memory.
enum class Kind { type2, mifare_classic, type4, other };
const char* to_string(Kind kind);
// Plain words for the touchscreen and the browser.
const char* describe(Kind kind);

struct Identity {
  static constexpr std::size_t maximum_length = 10U;
  std::array<std::uint8_t, maximum_length> nfcid1{};
  std::uint8_t length{0};  // 4, 7 or 10 bytes (cascade level 1, 2 or 3)
  std::uint8_t sak{0};
  // SENS_RES (ATQA) as received: anticollision information, platform information.
  std::array<std::uint8_t, 2> atqa{};
  // A Type 1 (Topaz) tag answers anticollision differently and has no SAK.
  bool type1{false};

  [[nodiscard]] static core::Result<Identity> from_wire(core::ByteView nfcid1, std::uint8_t sak,
                                                        std::array<std::uint8_t, 2> atqa,
                                                        bool type1 = false);
  [[nodiscard]] Kind kind() const;
  // Upper-case hex without separators, e.g. "04A1B2C3D4E5F6".
  [[nodiscard]] std::string hex() const;
  // Upper-case hex with colons, e.g. "04:A1:B2:C3:D4:E5:F6", like the NFC-V UID.
  [[nodiscard]] std::string diagnostic() const;
  [[nodiscard]] bool operator==(const Identity& other) const {
    return length == other.length && sak == other.sak && type1 == other.type1 &&
           std::equal(nfcid1.begin(), nfcid1.begin() + length, other.nfcid1.begin());
  }
  [[nodiscard]] bool operator!=(const Identity& other) const { return !(*this == other); }
};
}  // namespace opentag::nfc::nfca

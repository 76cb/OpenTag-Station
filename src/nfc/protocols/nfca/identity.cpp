#include "nfc/protocols/nfca/identity.hpp"

#include <algorithm>

namespace opentag::nfc::nfca {
namespace {
constexpr char hex_digits[] = "0123456789ABCDEF";
// SAK bits, NXP AN10833 table 1 and ISO/IEC 14443-3 table 9.
constexpr std::uint8_t sak_iso14443_4 = 0x20U;   // bit 6: ISO-DEP (Type 4)
constexpr std::uint8_t sak_uid_incomplete = 0x04U;  // bit 3: cascade not finished
constexpr std::uint8_t sak_mifare_classic = 0x08U;  // bit 4: MIFARE Classic 1K/4K/Mini

std::string format(const Identity& identity, bool colons) {
  std::string out;
  for (std::uint8_t i = 0; i < identity.length; ++i) {
    if (colons && i != 0U) out += ':';
    out += hex_digits[(identity.nfcid1[i] >> 4U) & 0x0FU];
    out += hex_digits[identity.nfcid1[i] & 0x0FU];
  }
  return out;
}
}  // namespace

const char* to_string(Kind kind) {
  switch (kind) {
    case Kind::type2: return "type2";
    case Kind::mifare_classic: return "mifare_classic";
    case Kind::type4: return "type4";
    case Kind::other: break;
  }
  return "other";
}

const char* describe(Kind kind) {
  switch (kind) {
    case Kind::type2: return "NTAG or other Type 2 tag";
    case Kind::mifare_classic: return "MIFARE Classic tag";
    case Kind::type4: return "Type 4 (ISO 14443-4) tag";
    case Kind::other: break;
  }
  return "other NFC-A tag";
}

core::Result<Identity> Identity::from_wire(core::ByteView id, std::uint8_t sak,
                                           std::array<std::uint8_t, 2> atqa, bool type1) {
  if (id.size != 4U && id.size != 7U && id.size != 10U)
    return core::Result<Identity>::failure(
        {core::ErrorCategory::unsupported_tag, "NFC-A NFCID1 must be 4, 7 or 10 bytes", false});
  Identity result;
  std::copy(id.data, id.data + id.size, result.nfcid1.begin());
  result.length = static_cast<std::uint8_t>(id.size);
  result.sak = sak;
  result.atqa = atqa;
  result.type1 = type1;
  return core::Result<Identity>::success(result);
}

Kind Identity::kind() const {
  if (type1 || (sak & sak_uid_incomplete) != 0U) return Kind::other;
  if ((sak & sak_iso14443_4) != 0U) return Kind::type4;
  if ((sak & sak_mifare_classic) != 0U) return Kind::mifare_classic;
  if (sak == 0x00U) return Kind::type2;
  return Kind::other;
}

std::string Identity::hex() const { return format(*this, false); }
std::string Identity::diagnostic() const { return format(*this, true); }
}  // namespace opentag::nfc::nfca

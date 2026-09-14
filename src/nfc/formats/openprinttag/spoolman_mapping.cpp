#include "nfc/formats/openprinttag/spoolman_mapping.hpp"
#include "nfc/formats/openprinttag/cbor.hpp"
#include "nfc/formats/openprinttag/initializer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace opentag::nfc::openprinttag {
namespace {
core::Result<void> invalid(const char *text) {
  return core::Result<void>::failure(
      {core::ErrorCategory::invalid_openprinttag, text, false});
}
int hex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace
core::Result<std::array<std::uint8_t, 16>>
parse_instance_uuid(const std::string &s) {
  std::array<std::uint8_t, 16> value{};
  std::size_t cursor = 0;
  if (s.size() == 36) {
    bool valid = true;
    for (std::size_t i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10) {
        if (s[cursor++] != '-')
          valid = false;
      }
      const auto a = hex(s[cursor++]), b = hex(s[cursor++]);
      if (a < 0 || b < 0)
        valid = false;
      if (a >= 0 && b >= 0)
        value[i] = (a << 4) | b;
    }
    if (valid && (value[8] & 0xc0) == 0x80 && (value[6] >> 4) >= 1 &&
        (value[6] >> 4) <= 8)
      return core::Result<std::array<std::uint8_t, 16>>::success(value);
  }
  return core::Result<std::array<std::uint8_t, 16>>::failure(
      {core::ErrorCategory::configuration, "Spool instance UUID is invalid",
       false});
}
std::string instance_uuid_text(const std::array<std::uint8_t, 16> &uuid) {
  const char *digits = "0123456789abcdef";
  std::string text;
  for (std::size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      text += '-';
    text += digits[uuid[i] >> 4];
    text += digits[uuid[i] & 15];
  }
  return text;
}
static __attribute__((noinline)) core::Result<void>
encode_main(JsonObjectConst spool, const std::array<std::uint8_t, 16> &uuid,
            WriterPlan &p) {
  auto filament = spool["filament"].as<JsonObjectConst>();
  auto initialized =
      Initializer::generate({WriterPlan::usable_bytes, 4, 32, std::nullopt});
  if (!initialized.ok())
    return core::Result<void>::failure(initialized.error());
  std::copy(initialized.value().bytes.begin(), initialized.value().bytes.end(),
            p.target.begin());
  auto decoded =
      Codec::decode({p.target.data(), WriterPlan::usable_bytes}, p.proposed);
  if (!decoded.ok())
    return decoded;
  // Bounded map written straight into the PSRAM-owned target, no large vector.
  auto region = p.proposed.envelope.main;
  std::size_t cursor = region.absolute_offset;
  const auto end = cursor + region.size;
  bool fits = true;
  auto byte = [&](std::uint8_t value) {
    if (cursor < end)
      p.target[cursor++] = value;
    else
      fits = false;
  };
  auto head = [&](std::uint8_t major, std::size_t n) {
    if (n < 24)
      byte(major | n);
    else if (n <= 255) {
      byte(major | 24);
      byte(n);
    } else {
      fits = false;
    }
  };
  auto number = [&](int key, JsonVariantConst v) {
    if (v.isNull())
      return;
    if (!v.is<double>() || !std::isfinite(v.as<double>()) ||
        v.as<double>() < 0) {
      fits = false;
      return;
    }
    head(0, key);
    for (auto b : CborMapView::encode_number(v.as<double>()))
      byte(b);
  };
  auto text = [&](int key, JsonVariantConst v, std::size_t maximum) {
    if (v.isNull())
      return;
    if (!v.is<const char *>()) {
      fits = false;
      return;
    }
    const auto *s = v.as<const char *>();
    const auto n = std::strlen(s);
    // Omit unrepresentable strings without truncating identifiers or names.
    if (n > maximum || n == 0)
      return;
    head(0, key);
    head(0x60, n);
    for (std::size_t i = 0; i < n; ++i)
      byte(s[i]);
  };
  byte(0xbf);
  head(0, 0);
  head(0x40, 16);
  for (auto b : uuid)
    byte(b);
  head(0, 8);
  byte(0); // pinned material_class 0 = FFF filament
  text(10, filament["name"], 63);
  text(11, filament["vendor"]["name"], 31);
  text(52, filament["material"], 7);
  text(6, filament["article_number"], 16);
  const char *common_types[] = {"PLA",  "PETG", "TPU", "ABS",  "ASA", "PC",
                                "PCTG", "PP",   "PA6", "PA11", "PA12"};
  if (filament["material"].is<const char *>())
    for (unsigned type = 0; type < 11; ++type)
      if (std::strcmp(filament["material"].as<const char *>(),
                      common_types[type]) == 0) {
        head(0, 9);
        head(0, type);
        break;
      }
  number(16, filament["weight"]);
  number(17, spool["initial_weight"]);
  auto tare = spool["spool_weight"];
  if (tare.isNull())
    tare = filament["spool_weight"];
  if (tare.isNull())
    tare = filament["vendor"]["empty_spool_weight"];
  number(18, tare);
  number(29, filament["density"]);
  if (!filament["diameter"].isNull()) {
    const auto micrometres = filament["diameter"].as<double>() * 1000.0;
    if (!filament["diameter"].is<double>() || !std::isfinite(micrometres) ||
        micrometres <= 0 || micrometres > 65535 ||
        std::fabs(micrometres - std::round(micrometres)) > 0.000001)
      return invalid(
          "Spoolman diameter is not representable in integer micrometres");
    head(0, 61);
    for (auto b : CborMapView::encode_unsigned(
             static_cast<std::uint64_t>(std::round(micrometres))))
      byte(b);
  }
  // Printing setpoints are not preheat/leveling temperatures or safe ranges.
  // Retain those values in Spoolman and the preview, without inventing bounds.
  auto color = filament["color_hex"];
  if (color.is<const char *>()) {
    const std::string s = color.as<const char *>();
    if (s.size() != 6 && s.size() != 8)
      return invalid("Canonical filament color is malformed");
    head(0, 19);
    head(0x40, s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
      if (hex(s[i]) < 0 || hex(s[i + 1]) < 0)
        return invalid("Canonical filament color is malformed");
      byte((hex(s[i]) << 4) | hex(s[i + 1]));
    }
  }
  byte(0xff);
  if (!fits)
    return invalid(
        "Canonical metadata does not fit this tag; no bytes written");
  return core::Result<void>::success();
}
core::Result<void> map_spoolman(JsonObjectConst spool,
                                const std::array<std::uint8_t, 16> &uuid,
                                WriterPlan &p, bool mutable_only) {
  auto filament = spool["filament"].as<JsonObjectConst>();
  if (!spool["id"].is<int>() || spool["id"].as<int>() <= 0 ||
      filament.isNull() || !filament["id"].is<int>() ||
      filament["id"].as<int>() <= 0 || !spool["archived"].is<bool>() ||
      spool["archived"].as<bool>())
    return invalid("Canonical active Spoolman spool required");
  if (!spool["used_weight"].is<double>() ||
      !std::isfinite(spool["used_weight"].as<double>()) ||
      spool["used_weight"].as<double>() < 0)
    return invalid("Canonical used_weight is missing or invalid");
  p.mutable_only = mutable_only;
  if (mutable_only) {
    if (p.blank || p.current.material.instance_uuid != uuid ||
        !p.current.envelope.auxiliary)
      return invalid("Mutable update requires the same spool UUID and an "
                     "existing auxiliary region");
    auto image = Codec::update_consumed_weight(
        {p.original.data(), WriterPlan::usable_bytes},
        spool["used_weight"].as<double>(), p.proposed);
    if (!image.ok())
      return core::Result<void>::failure(image.error());
    std::copy(image.value().begin(), image.value().end(), p.target.begin());
    return core::Result<void>::success();
  }
  auto encoded = encode_main(spool, uuid, p);
  if (!encoded.ok())
    return encoded;
  auto consumed = Codec::update_consumed_weight(
      {p.target.data(), WriterPlan::usable_bytes},
      spool["used_weight"].as<double>(), p.proposed);
  if (!consumed.ok())
    return core::Result<void>::failure(consumed.error());
  std::copy(consumed.value().begin(), consumed.value().end(), p.target.begin());
  return core::Result<void>::success();
}
} // namespace opentag::nfc::openprinttag

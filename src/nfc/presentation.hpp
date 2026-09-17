#pragma once
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include "nfc/read_only_service.hpp"

namespace opentag::nfc {
inline std::string describe(const ReadSnapshot& s,
                            std::optional<float> measured = std::nullopt) {
  std::string out = s.state == ReadState::deferred
                        ? "NFC deferred: provisioning\nFinish Wi-Fi setup to start NFC"
                    : s.blank_compatible ? "Blank compatible NFC tag - ready to write"
                    : s.tag ? "OpenPrintTag recognized"
                    : s.state == ReadState::unsupported
                        ? "NFC-V tag: unsupported OpenPrintTag"
                    : s.state == ReadState::multiple
                        ? "Multiple tags: present only one"
                    : s.state == ReadState::error ? "NFC reader error"
                    : s.present                   ? "Reading NFC-V tag"
                                                  : "Ready for an NFC-V tag";
  if (s.uid)
    out += std::string("\nUID ") +
           nfcv::format_diagnostic_uid(s.uid->bytes).data();
  if (s.checksum)
    out += std::string("\nChecksum ") +
           nfcv::format_diagnostic_checksum(*s.checksum).data();
  if (s.tag) {
    const auto& m = s.tag->decoded.material;
    out += "\nMaterial: " + m.material_name.value_or("unavailable");
    out += "\nType: " + m.material_abbreviation.value_or(
                            m.material_type ? std::to_string(*m.material_type)
                                            : "unavailable");
    out += "\nBrand: " + m.brand_name.value_or("unavailable");
    const auto grams = [&](const char* name, std::optional<double> value) {
      out += std::string("\n") + name + ": " +
             (value ? std::to_string(*value) + " g" : "unavailable");
    };
    grams("Nominal full", m.nominal_netto_full_weight);
    grams("Actual full", m.actual_netto_full_weight);
    grams("Consumed", m.consumed_weight);
    grams("Remaining", m.remaining_weight());
    if (m.primary_color)
      out += "\nColor RGB: " + std::to_string(m.primary_color->red) + "," +
             std::to_string(m.primary_color->green) + "," +
             std::to_string(m.primary_color->blue);
    else out += "\nColor: unavailable";
  }
  if (measured) out += "\nMeasured: " + std::to_string(*measured) + " g";
  if (s.error) out += "\n" + s.error->message;
  return out;
}
}  // namespace opentag::nfc

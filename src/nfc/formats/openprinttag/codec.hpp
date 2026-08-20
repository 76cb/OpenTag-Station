#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/byte_view.hpp"
#include "core/result.hpp"

namespace opentag::nfc::openprinttag {

inline constexpr const char* mime_type = "application/vnd.openprinttag";
inline constexpr const char* specification_revision =
    "e0dab1ae16838d2c342e7cfc509455441b7d8eba";

struct RegionLayout {
  std::size_t payload_offset{0};
  std::size_t absolute_offset{0};
  std::size_t size{0};
  std::size_t used_size{0};
};

struct Envelope {
  std::size_t capability_capacity{0};
  std::uint8_t capability_access{0};
  bool multiple_block_read_supported{false};
  std::size_t ndef_tlv_offset{0};
  std::size_t ndef_message_offset{0};
  std::size_t ndef_message_size{0};
  std::size_t payload_offset{0};
  std::size_t payload_size{0};
  RegionLayout meta;
  RegionLayout main;
  std::optional<RegionLayout> auxiliary;
};

struct ColorRgba {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
  std::uint8_t alpha{255};
};

struct ValidationReport {
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
  [[nodiscard]] bool valid() const { return errors.empty(); }
};

struct MaterialRecord {
  std::optional<std::array<std::uint8_t, 16>> instance_uuid;
  std::optional<std::array<std::uint8_t, 16>> package_uuid;
  std::optional<std::array<std::uint8_t, 16>> material_uuid;
  std::optional<std::array<std::uint8_t, 16>> brand_uuid;
  std::optional<std::uint64_t> gtin;
  std::optional<std::string> brand_specific_instance_id;
  std::optional<std::string> brand_specific_package_id;
  std::optional<std::string> brand_specific_material_id;
  std::optional<std::uint64_t> material_class;
  std::optional<std::uint64_t> material_type;
  std::optional<std::string> material_name;
  std::optional<std::string> material_abbreviation;
  std::optional<std::string> brand_name;
  std::optional<std::uint64_t> write_protection;
  std::optional<std::int64_t> manufactured_date;
  std::optional<std::string> country_of_origin;
  std::optional<std::int64_t> expiration_date;
  std::optional<double> nominal_netto_full_weight;
  std::optional<double> actual_netto_full_weight;
  std::optional<double> nominal_full_length;
  std::optional<double> actual_full_length;
  std::optional<double> empty_container_weight;
  std::optional<ColorRgba> primary_color;
  std::array<std::optional<ColorRgba>, 5> secondary_colors;
  std::optional<std::array<double, 3>> primary_color_lab;
  std::optional<std::string> primary_color_ral;
  std::optional<double> transmission_distance;
  std::vector<std::uint64_t> tags;
  std::vector<std::uint64_t> certifications;
  std::optional<double> density;
  std::optional<double> filament_diameter;
  std::optional<std::int64_t> shore_hardness_a;
  std::optional<std::int64_t> shore_hardness_d;
  std::optional<double> min_nozzle_diameter;
  std::optional<std::int64_t> min_print_temperature;
  std::optional<std::int64_t> max_print_temperature;
  std::optional<std::int64_t> preheat_temperature;
  std::optional<std::int64_t> min_bed_temperature;
  std::optional<std::int64_t> max_bed_temperature;
  std::optional<std::int64_t> min_chamber_temperature;
  std::optional<std::int64_t> max_chamber_temperature;
  std::optional<std::int64_t> chamber_temperature;
  std::optional<std::int64_t> container_width;
  std::optional<std::int64_t> container_outer_diameter;
  std::optional<std::int64_t> container_inner_diameter;
  std::optional<std::int64_t> container_hole_diameter;
  std::optional<double> viscosity_18c;
  std::optional<double> viscosity_25c;
  std::optional<double> viscosity_40c;
  std::optional<double> viscosity_60c;
  std::optional<double> container_volumetric_capacity;
  std::optional<std::int64_t> cure_wavelength;
  std::optional<std::int64_t> drying_temperature;
  std::optional<std::int64_t> drying_time;
  std::optional<double> consumed_weight;
  std::optional<std::string> workgroup;
  std::optional<std::string> general_purpose_range_user;
  std::optional<std::int64_t> last_stir_time;
  std::optional<std::string> storage_location;
  std::optional<std::int64_t> purchase_time;
  std::optional<double> purchase_price;
  std::optional<std::string> purchase_currency;
  std::size_t unknown_main_fields{0};
  std::size_t unknown_auxiliary_fields{0};
  ValidationReport validation;

  [[nodiscard]] std::optional<double> full_weight() const {
    return actual_netto_full_weight.has_value()
               ? actual_netto_full_weight
               : nominal_netto_full_weight;
  }
  [[nodiscard]] std::optional<double> remaining_weight() const {
    const auto full = full_weight();
    if (!full.has_value()) {
      return std::nullopt;
    }
    return *full - consumed_weight.value_or(0.0);
  }
};

struct DecodedTag {
  Envelope envelope;
  MaterialRecord material;
};

class Codec {
 public:
  static constexpr std::size_t maximum_tag_image_size = 4096U;

  static core::Result<DecodedTag> decode(core::ByteView tag_image);
  static core::Result<std::vector<std::uint8_t>> update_consumed_weight(
      core::ByteView tag_image,
      double consumed_grams);
};

}  // namespace opentag::nfc::openprinttag

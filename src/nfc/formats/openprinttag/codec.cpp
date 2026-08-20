#include "nfc/formats/openprinttag/codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "nfc/formats/openprinttag/cbor.hpp"

namespace opentag::nfc::openprinttag {
namespace {

using core::ByteView;

core::Error malformed(const std::string& message) {
  return {core::ErrorCategory::invalid_openprinttag, message, false};
}

core::Result<std::uint32_t> read_big_endian(
    ByteView bytes,
    std::size_t offset,
    std::size_t count) {
  if ((count != 2U && count != 4U) || !bytes.contains(offset, count)) {
    return core::Result<std::uint32_t>::failure(malformed("truncated NDEF length"));
  }
  std::uint32_t value = 0U;
  for (std::size_t index = 0; index < count; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return core::Result<std::uint32_t>::success(value);
}

core::Result<Envelope> parse_envelope(ByteView image) {
  if (image.size < 8U || image.size > Codec::maximum_tag_image_size) {
    return core::Result<Envelope>::failure(malformed("Type 5 tag image size is invalid"));
  }
  if (image[0] != 0xE1U) {
    return core::Result<Envelope>::failure(malformed("Type 5 capability-container magic mismatch"));
  }

  Envelope result;
  result.capability_capacity = static_cast<std::size_t>(image[2]) * 8U;
  result.capability_access = image[1] & 0x0FU;
  result.multiple_block_read_supported = (image[3] & 0x01U) != 0U;
  if (result.capability_capacity < 8U || result.capability_capacity > image.size) {
    return core::Result<Envelope>::failure(malformed("capability-container capacity exceeds read image"));
  }

  std::size_t cursor = 4U;
  bool found_ndef = false;
  while (cursor < result.capability_capacity) {
    const std::size_t tlv_offset = cursor;
    const auto tag = image[cursor++];
    if (tag == 0x00U) {
      continue;
    }
    if (tag == 0xFEU) {
      break;
    }
    if (!image.contains(cursor, 1U)) {
      return core::Result<Envelope>::failure(malformed("truncated Type 5 TLV"));
    }
    std::size_t length = image[cursor++];
    if (length == 0xFFU) {
      const auto extended = read_big_endian(image, cursor, 2U);
      if (!extended.ok()) {
        return core::Result<Envelope>::failure(extended.error());
      }
      length = extended.value();
      cursor += 2U;
    }
    if (!image.contains(cursor, length) || cursor + length > result.capability_capacity) {
      return core::Result<Envelope>::failure(malformed("Type 5 TLV exceeds declared capacity"));
    }
    if (tag == 0x03U) {
      result.ndef_tlv_offset = tlv_offset;
      result.ndef_message_offset = cursor;
      result.ndef_message_size = length;
      found_ndef = true;
      break;
    }
    cursor += length;
  }
  if (!found_ndef || result.ndef_message_size == 0U) {
    return core::Result<Envelope>::failure(malformed("NDEF TLV is absent"));
  }

  const auto ndef = image.subview(result.ndef_message_offset, result.ndef_message_size);
  cursor = 0U;
  bool saw_message_begin = false;
  bool saw_message_end = false;
  bool found_openprinttag = false;
  while (cursor < ndef.size && !saw_message_end) {
    if (!ndef.contains(cursor, 2U)) {
      return core::Result<Envelope>::failure(malformed("truncated NDEF record header"));
    }
    const auto flags = ndef[cursor++];
    const bool message_begin = (flags & 0x80U) != 0U;
    const bool message_end = (flags & 0x40U) != 0U;
    const bool chunked = (flags & 0x20U) != 0U;
    const bool short_record = (flags & 0x10U) != 0U;
    const bool has_id = (flags & 0x08U) != 0U;
    const auto tnf = flags & 0x07U;
    if (chunked || (!saw_message_begin && !message_begin) ||
        (saw_message_begin && message_begin)) {
      return core::Result<Envelope>::failure(malformed("unsupported or malformed NDEF record sequence"));
    }
    saw_message_begin = true;

    const std::size_t type_length = ndef[cursor++];
    std::size_t payload_length = 0U;
    if (short_record) {
      if (!ndef.contains(cursor, 1U)) {
        return core::Result<Envelope>::failure(malformed("truncated short NDEF payload length"));
      }
      payload_length = ndef[cursor++];
    } else {
      const auto length = read_big_endian(ndef, cursor, 4U);
      if (!length.ok()) {
        return core::Result<Envelope>::failure(length.error());
      }
      payload_length = length.value();
      cursor += 4U;
    }
    std::size_t id_length = 0U;
    if (has_id) {
      if (!ndef.contains(cursor, 1U)) {
        return core::Result<Envelope>::failure(malformed("truncated NDEF ID length"));
      }
      id_length = ndef[cursor++];
    }
    if (!ndef.contains(cursor, type_length) ||
        !ndef.contains(cursor + type_length, id_length) ||
        !ndef.contains(cursor + type_length + id_length, payload_length)) {
      return core::Result<Envelope>::failure(malformed("NDEF record exceeds message bounds"));
    }

    const auto type = ndef.subview(cursor, type_length);
    cursor += type_length + id_length;
    const auto record_payload_offset = cursor;
    cursor += payload_length;

    const bool mime_matches = tnf == 0x02U &&
        type.size == std::strlen(mime_type) &&
        std::memcmp(type.data, mime_type, type.size) == 0;
    if (mime_matches) {
      if (found_openprinttag) {
        return core::Result<Envelope>::failure(malformed("multiple OpenPrintTag MIME records"));
      }
      found_openprinttag = true;
      result.payload_offset = result.ndef_message_offset + record_payload_offset;
      result.payload_size = payload_length;
    }
    saw_message_end = message_end;
  }
  if (!saw_message_end || cursor != ndef.size || !found_openprinttag) {
    return core::Result<Envelope>::failure(malformed("OpenPrintTag MIME record is absent or NDEF is incomplete"));
  }
  if (result.payload_size == 0U) {
    return core::Result<Envelope>::failure(malformed("OpenPrintTag payload is empty"));
  }

  const auto payload = image.subview(result.payload_offset, result.payload_size);
  const auto meta = CborMapView::parse(payload.subview(
      0U, std::min(payload.size, CborMapView::maximum_region_size)));
  if (!meta.ok()) {
    return core::Result<Envelope>::failure(meta.error());
  }
  result.meta = {0U, result.payload_offset, meta.value().used_size(), meta.value().used_size()};

  std::size_t main_offset = meta.value().used_size();
  std::optional<std::size_t> main_size;
  std::optional<std::size_t> aux_offset;
  std::optional<std::size_t> aux_size;
  const auto checked_size = [](std::uint64_t value) -> core::Result<std::size_t> {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      return core::Result<std::size_t>::failure(malformed(
          "OpenPrintTag region value exceeds platform address range"));
    }
    return core::Result<std::size_t>::success(static_cast<std::size_t>(value));
  };
  if (meta.value().find(0U) != nullptr) {
    const auto value = meta.value().read_unsigned(0U);
    if (!value.ok()) return core::Result<Envelope>::failure(value.error());
    const auto converted = checked_size(value.value());
    if (!converted.ok()) return core::Result<Envelope>::failure(converted.error());
    main_offset = converted.value();
  }
  if (meta.value().find(1U) != nullptr) {
    const auto value = meta.value().read_unsigned(1U);
    if (!value.ok()) return core::Result<Envelope>::failure(value.error());
    const auto converted = checked_size(value.value());
    if (!converted.ok()) return core::Result<Envelope>::failure(converted.error());
    main_size = converted.value();
  }
  if (meta.value().find(2U) != nullptr) {
    const auto value = meta.value().read_unsigned(2U);
    if (!value.ok()) return core::Result<Envelope>::failure(value.error());
    const auto converted = checked_size(value.value());
    if (!converted.ok()) return core::Result<Envelope>::failure(converted.error());
    aux_offset = converted.value();
  }
  if (meta.value().find(3U) != nullptr) {
    const auto value = meta.value().read_unsigned(3U);
    if (!value.ok()) return core::Result<Envelope>::failure(value.error());
    const auto converted = checked_size(value.value());
    if (!converted.ok()) return core::Result<Envelope>::failure(converted.error());
    aux_size = converted.value();
  }
  if (aux_size.has_value() && !aux_offset.has_value()) {
    return core::Result<Envelope>::failure(malformed("auxiliary size exists without offset"));
  }
  if (main_offset > payload.size ||
      (aux_offset.has_value() && *aux_offset > payload.size)) {
    return core::Result<Envelope>::failure(malformed("OpenPrintTag region offset exceeds payload"));
  }

  const auto next_after = [&](std::size_t offset) {
    std::size_t next = payload.size;
    if (main_offset > offset) next = std::min(next, main_offset);
    if (aux_offset.has_value() && *aux_offset > offset) next = std::min(next, *aux_offset);
    return next;
  };
  const std::size_t resolved_main_size = main_size.has_value()
                                             ? *main_size
                                             : next_after(main_offset) - main_offset;
  const std::size_t resolved_aux_size = !aux_offset.has_value()
                                             ? 0U
                                             : aux_size.has_value()
                                                   ? *aux_size
                                                   : next_after(*aux_offset) - *aux_offset;
  const auto region_valid = [&](std::size_t offset, std::size_t size) {
    return offset >= result.meta.size && size > 0U &&
           size <= CborMapView::maximum_region_size && payload.contains(offset, size);
  };
  if (!region_valid(main_offset, resolved_main_size)) {
    return core::Result<Envelope>::failure(malformed("main region bounds are invalid"));
  }
  if (aux_offset.has_value() && !region_valid(*aux_offset, resolved_aux_size)) {
    return core::Result<Envelope>::failure(malformed("auxiliary region bounds are invalid"));
  }
  if (aux_offset.has_value() && resolved_aux_size < 16U) {
    return core::Result<Envelope>::failure(malformed("auxiliary region is smaller than 16 bytes"));
  }
  if (aux_offset.has_value()) {
    const auto main_end = main_offset + resolved_main_size;
    const auto aux_end = *aux_offset + resolved_aux_size;
    if (!(main_end <= *aux_offset || aux_end <= main_offset)) {
      return core::Result<Envelope>::failure(malformed("OpenPrintTag regions overlap"));
    }
  }

  const auto main_map = CborMapView::parse(payload.subview(main_offset, resolved_main_size));
  if (!main_map.ok()) return core::Result<Envelope>::failure(main_map.error());
  result.main = {
      main_offset,
      result.payload_offset + main_offset,
      resolved_main_size,
      main_map.value().used_size(),
  };
  if (aux_offset.has_value()) {
    const auto aux_map = CborMapView::parse(payload.subview(*aux_offset, resolved_aux_size));
    if (!aux_map.ok()) return core::Result<Envelope>::failure(aux_map.error());
    result.auxiliary = RegionLayout{
        *aux_offset,
        result.payload_offset + *aux_offset,
        resolved_aux_size,
        aux_map.value().used_size(),
    };
  }
  return core::Result<Envelope>::success(result);
}

template <typename T, typename Reader>
core::Result<void> read_optional(
    const CborMapView& map,
    std::uint64_t key,
    std::optional<T>& output,
    Reader reader) {
  if (map.find(key) == nullptr) {
    return core::Result<void>::success();
  }
  const auto value = (map.*reader)(key);
  if (!value.ok()) {
    return core::Result<void>::failure(value.error());
  }
  output = value.value();
  return core::Result<void>::success();
}

core::Result<void> read_uuid(
    const CborMapView& map,
    std::uint64_t key,
    std::optional<std::array<std::uint8_t, 16>>& output) {
  if (map.find(key) == nullptr) return core::Result<void>::success();
  const auto bytes = map.read_bytes(key);
  if (!bytes.ok() || bytes.value().size() != 16U) {
    return core::Result<void>::failure(malformed("OpenPrintTag UUID is not 16 bytes"));
  }
  std::array<std::uint8_t, 16> uuid{};
  std::copy(bytes.value().begin(), bytes.value().end(), uuid.begin());
  output = uuid;
  return core::Result<void>::success();
}

core::Result<void> read_color(
    const CborMapView& map,
    std::uint64_t key,
    std::optional<ColorRgba>& output) {
  if (map.find(key) == nullptr) return core::Result<void>::success();
  const auto null_value = map.is_null(key);
  if (!null_value.ok()) return core::Result<void>::failure(null_value.error());
  if (null_value.value()) return core::Result<void>::success();
  const auto bytes = map.read_bytes(key);
  if (!bytes.ok() || (bytes.value().size() != 3U && bytes.value().size() != 4U)) {
    return core::Result<void>::failure(malformed("OpenPrintTag RGBA color must contain 3 or 4 bytes"));
  }
  output = ColorRgba{
      bytes.value()[0], bytes.value()[1], bytes.value()[2],
      bytes.value().size() == 4U ? bytes.value()[3] : static_cast<std::uint8_t>(255U)};
  return core::Result<void>::success();
}

core::Result<void> read_lab_color(
    const CborMapView& map,
    std::uint64_t key,
    std::optional<std::array<double, 3>>& output) {
  if (map.find(key) == nullptr) return core::Result<void>::success();
  const auto values = map.read_number_array(key, 3U);
  if (!values.ok() || values.value().size() != 3U) {
    return core::Result<void>::failure(malformed("OpenPrintTag LAB color must contain 3 numbers"));
  }
  output = std::array<double, 3>{values.value()[0], values.value()[1], values.value()[2]};
  return core::Result<void>::success();
}

bool known_main_key(std::uint64_t key) {
  return key <= 60U;
}

bool known_aux_key(std::uint64_t key) {
  return key <= 7U;
}

core::Result<MaterialRecord> decode_material(
    ByteView image,
    const Envelope& envelope) {
  const auto main = CborMapView::parse(
      image.subview(envelope.main.absolute_offset, envelope.main.size));
  if (!main.ok()) return core::Result<MaterialRecord>::failure(main.error());
  MaterialRecord result;

#define READ_OPTIONAL(MAP, KEY, MEMBER, METHOD)                                    \
  do {                                                                              \
    const auto status = read_optional((MAP), (KEY), result.MEMBER, &CborMapView::METHOD); \
    if (!status.ok()) return core::Result<MaterialRecord>::failure(status.error()); \
  } while (false)

  for (const auto key : {0U, 1U, 2U, 3U}) {
    core::Result<void> status = core::Result<void>::success();
    if (key == 0U) status = read_uuid(main.value(), key, result.instance_uuid);
    if (key == 1U) status = read_uuid(main.value(), key, result.package_uuid);
    if (key == 2U) status = read_uuid(main.value(), key, result.material_uuid);
    if (key == 3U) status = read_uuid(main.value(), key, result.brand_uuid);
    if (!status.ok()) return core::Result<MaterialRecord>::failure(status.error());
  }
  READ_OPTIONAL(main.value(), 4U, gtin, read_unsigned);
  READ_OPTIONAL(main.value(), 5U, brand_specific_instance_id, read_text);
  READ_OPTIONAL(main.value(), 6U, brand_specific_package_id, read_text);
  READ_OPTIONAL(main.value(), 7U, brand_specific_material_id, read_text);
  READ_OPTIONAL(main.value(), 8U, material_class, read_unsigned);
  READ_OPTIONAL(main.value(), 9U, material_type, read_unsigned);
  READ_OPTIONAL(main.value(), 10U, material_name, read_text);
  READ_OPTIONAL(main.value(), 52U, material_abbreviation, read_text);
  READ_OPTIONAL(main.value(), 11U, brand_name, read_text);
  READ_OPTIONAL(main.value(), 13U, write_protection, read_unsigned);
  READ_OPTIONAL(main.value(), 14U, manufactured_date, read_integer);
  READ_OPTIONAL(main.value(), 55U, country_of_origin, read_text);
  READ_OPTIONAL(main.value(), 15U, expiration_date, read_integer);
  READ_OPTIONAL(main.value(), 16U, nominal_netto_full_weight, read_number);
  READ_OPTIONAL(main.value(), 17U, actual_netto_full_weight, read_number);
  READ_OPTIONAL(main.value(), 53U, nominal_full_length, read_number);
  READ_OPTIONAL(main.value(), 54U, actual_full_length, read_number);
  READ_OPTIONAL(main.value(), 18U, empty_container_weight, read_number);
  const auto color_status = read_color(main.value(), 19U, result.primary_color);
  if (!color_status.ok()) return core::Result<MaterialRecord>::failure(color_status.error());
  for (std::uint64_t index = 0U; index < result.secondary_colors.size(); ++index) {
    const auto status = read_color(main.value(), 20U + index, result.secondary_colors[index]);
    if (!status.ok()) return core::Result<MaterialRecord>::failure(status.error());
  }
  const auto lab_status = read_lab_color(main.value(), 59U, result.primary_color_lab);
  if (!lab_status.ok()) return core::Result<MaterialRecord>::failure(lab_status.error());
  READ_OPTIONAL(main.value(), 60U, primary_color_ral, read_text);
  READ_OPTIONAL(main.value(), 27U, transmission_distance, read_number);
  if (main.value().find(28U) != nullptr) {
    const auto tags = main.value().read_unsigned_array(28U, 16U);
    if (!tags.ok()) return core::Result<MaterialRecord>::failure(tags.error());
    result.tags = tags.value();
  }
  if (main.value().find(56U) != nullptr) {
    const auto certifications = main.value().read_unsigned_array(56U, 8U);
    if (!certifications.ok()) return core::Result<MaterialRecord>::failure(certifications.error());
    result.certifications = certifications.value();
  }
  READ_OPTIONAL(main.value(), 29U, density, read_number);
  READ_OPTIONAL(main.value(), 30U, filament_diameter, read_number);
  READ_OPTIONAL(main.value(), 31U, shore_hardness_a, read_integer);
  READ_OPTIONAL(main.value(), 32U, shore_hardness_d, read_integer);
  READ_OPTIONAL(main.value(), 33U, min_nozzle_diameter, read_number);
  READ_OPTIONAL(main.value(), 34U, min_print_temperature, read_integer);
  READ_OPTIONAL(main.value(), 35U, max_print_temperature, read_integer);
  READ_OPTIONAL(main.value(), 36U, preheat_temperature, read_integer);
  READ_OPTIONAL(main.value(), 37U, min_bed_temperature, read_integer);
  READ_OPTIONAL(main.value(), 38U, max_bed_temperature, read_integer);
  READ_OPTIONAL(main.value(), 39U, min_chamber_temperature, read_integer);
  READ_OPTIONAL(main.value(), 40U, max_chamber_temperature, read_integer);
  READ_OPTIONAL(main.value(), 41U, chamber_temperature, read_integer);
  READ_OPTIONAL(main.value(), 42U, container_width, read_integer);
  READ_OPTIONAL(main.value(), 43U, container_outer_diameter, read_integer);
  READ_OPTIONAL(main.value(), 44U, container_inner_diameter, read_integer);
  READ_OPTIONAL(main.value(), 45U, container_hole_diameter, read_integer);
  READ_OPTIONAL(main.value(), 46U, viscosity_18c, read_number);
  READ_OPTIONAL(main.value(), 47U, viscosity_25c, read_number);
  READ_OPTIONAL(main.value(), 48U, viscosity_40c, read_number);
  READ_OPTIONAL(main.value(), 49U, viscosity_60c, read_number);
  READ_OPTIONAL(main.value(), 50U, container_volumetric_capacity, read_number);
  READ_OPTIONAL(main.value(), 51U, cure_wavelength, read_integer);
  READ_OPTIONAL(main.value(), 57U, drying_temperature, read_integer);
  READ_OPTIONAL(main.value(), 58U, drying_time, read_integer);

  for (const auto& entry : main.value().entries()) {
    if (!known_main_key(entry.key)) ++result.unknown_main_fields;
  }

  if (envelope.auxiliary.has_value()) {
    const auto aux = CborMapView::parse(image.subview(
        envelope.auxiliary->absolute_offset, envelope.auxiliary->size));
    if (!aux.ok()) return core::Result<MaterialRecord>::failure(aux.error());
    READ_OPTIONAL(aux.value(), 0U, consumed_weight, read_number);
    READ_OPTIONAL(aux.value(), 1U, workgroup, read_text);
    READ_OPTIONAL(aux.value(), 2U, general_purpose_range_user, read_text);
    READ_OPTIONAL(aux.value(), 3U, last_stir_time, read_integer);
    READ_OPTIONAL(aux.value(), 4U, storage_location, read_text);
    READ_OPTIONAL(aux.value(), 5U, purchase_time, read_integer);
    READ_OPTIONAL(aux.value(), 6U, purchase_price, read_number);
    READ_OPTIONAL(aux.value(), 7U, purchase_currency, read_text);
    for (const auto& entry : aux.value().entries()) {
      if (!known_aux_key(entry.key)) ++result.unknown_auxiliary_fields;
    }
  }
#undef READ_OPTIONAL

  if (!result.material_class.has_value()) {
    result.validation.errors.emplace_back("missing required material_class");
  }
  const auto recommend = [&](bool present, const char* name) {
    if (!present) result.validation.warnings.emplace_back(std::string("missing recommended ") + name);
  };
  recommend(result.gtin.has_value(), "gtin");
  recommend(result.material_type.has_value(), "material_type");
  recommend(result.material_name.has_value(), "material_name");
  recommend(result.brand_name.has_value(), "brand_name");
  recommend(result.manufactured_date.has_value(), "manufactured_date");
  recommend(result.nominal_netto_full_weight.has_value(), "nominal_netto_full_weight");
  recommend(result.actual_netto_full_weight.has_value(), "actual_netto_full_weight");
  recommend(result.nominal_full_length.has_value(), "nominal_full_length");
  recommend(result.actual_full_length.has_value(), "actual_full_length");
  recommend(result.empty_container_weight.has_value(), "empty_container_weight");
  recommend(result.primary_color.has_value(), "primary_color");
  recommend(!result.tags.empty(), "tags");
  recommend(result.density.has_value(), "density");
  recommend(result.min_print_temperature.has_value(), "min_print_temperature");
  recommend(result.max_print_temperature.has_value(), "max_print_temperature");
  recommend(result.preheat_temperature.has_value(), "preheat_temperature");
  recommend(result.min_bed_temperature.has_value(), "min_bed_temperature");
  recommend(result.max_bed_temperature.has_value(), "max_bed_temperature");

  const auto validate_text = [&](const auto& field, std::size_t maximum, const char* name) {
    if (field.has_value() && field->size() > maximum) {
      result.validation.errors.emplace_back(std::string(name) + " exceeds maximum length");
    }
  };
  validate_text(result.brand_specific_instance_id, 16U, "brand_specific_instance_id");
  validate_text(result.brand_specific_package_id, 16U, "brand_specific_package_id");
  validate_text(result.brand_specific_material_id, 16U, "brand_specific_material_id");
  validate_text(result.material_name, 63U, "material_name");
  validate_text(result.material_abbreviation, 7U, "material_abbreviation");
  validate_text(result.brand_name, 31U, "brand_name");
  validate_text(result.country_of_origin, 2U, "country_of_origin");
  validate_text(result.primary_color_ral, 16U, "primary_color_ral");
  validate_text(result.workgroup, 8U, "workgroup");
  validate_text(result.general_purpose_range_user, 8U, "general_purpose_range_user");
  validate_text(result.storage_location, 8U, "storage_location");
  validate_text(result.purchase_currency, 3U, "purchase_currency");

  const auto validate_finite = [&](const auto& field, const char* name) {
    if (field.has_value() && !std::isfinite(*field)) {
      result.validation.errors.emplace_back(std::string(name) + " is not finite");
    }
  };
  const auto validate_nonnegative = [&](const auto& field, const char* name) {
    validate_finite(field, name);
    if (field.has_value() && std::isfinite(*field) && *field < 0.0) {
      result.validation.errors.emplace_back(std::string(name) + " is negative");
    }
  };
  validate_nonnegative(result.nominal_netto_full_weight, "nominal_netto_full_weight");
  validate_nonnegative(result.actual_netto_full_weight, "actual_netto_full_weight");
  validate_nonnegative(result.nominal_full_length, "nominal_full_length");
  validate_nonnegative(result.actual_full_length, "actual_full_length");
  validate_nonnegative(result.empty_container_weight, "empty_container_weight");
  validate_nonnegative(result.transmission_distance, "transmission_distance");
  validate_nonnegative(result.density, "density");
  validate_nonnegative(result.filament_diameter, "filament_diameter");
  validate_nonnegative(result.min_nozzle_diameter, "min_nozzle_diameter");
  validate_nonnegative(result.viscosity_18c, "viscosity_18c");
  validate_nonnegative(result.viscosity_25c, "viscosity_25c");
  validate_nonnegative(result.viscosity_40c, "viscosity_40c");
  validate_nonnegative(result.viscosity_60c, "viscosity_60c");
  validate_nonnegative(result.container_volumetric_capacity, "container_volumetric_capacity");
  validate_nonnegative(result.consumed_weight, "consumed_weight");
  validate_nonnegative(result.purchase_price, "purchase_price");
  if (result.primary_color_lab.has_value()) {
    for (const auto component : *result.primary_color_lab) {
      if (!std::isfinite(component)) {
        result.validation.errors.emplace_back("primary_color_lab is not finite");
        break;
      }
    }
    if ((*result.primary_color_lab)[0] < 0.0 || (*result.primary_color_lab)[0] > 100.0) {
      result.validation.errors.emplace_back("primary_color_lab L component is outside 0..100");
    }
  }
  if (result.min_print_temperature.has_value() && result.max_print_temperature.has_value() &&
      *result.min_print_temperature > *result.max_print_temperature) {
    result.validation.errors.emplace_back("min_print_temperature exceeds max_print_temperature");
  }
  if (result.min_bed_temperature.has_value() && result.max_bed_temperature.has_value() &&
      *result.min_bed_temperature > *result.max_bed_temperature) {
    result.validation.errors.emplace_back("min_bed_temperature exceeds max_bed_temperature");
  }
  if (result.min_chamber_temperature.has_value() && result.max_chamber_temperature.has_value() &&
      *result.min_chamber_temperature > *result.max_chamber_temperature) {
    result.validation.errors.emplace_back("min_chamber_temperature exceeds max_chamber_temperature");
  }
  return core::Result<MaterialRecord>::success(std::move(result));
}

}  // namespace

core::Result<DecodedTag> Codec::decode(core::ByteView tag_image) {
  const auto envelope = parse_envelope(tag_image);
  if (!envelope.ok()) return core::Result<DecodedTag>::failure(envelope.error());
  const auto material = decode_material(tag_image, envelope.value());
  if (!material.ok()) return core::Result<DecodedTag>::failure(material.error());
  return core::Result<DecodedTag>::success({envelope.value(), material.value()});
}

core::Result<std::vector<std::uint8_t>> Codec::update_consumed_weight(
    core::ByteView tag_image,
    double consumed_grams) {
  if (!std::isfinite(consumed_grams) || consumed_grams < 0.0) {
    return core::Result<std::vector<std::uint8_t>>::failure(
        malformed("consumed weight must be finite and non-negative"));
  }
  const auto decoded = decode(tag_image);
  if (!decoded.ok()) return core::Result<std::vector<std::uint8_t>>::failure(decoded.error());
  if (!decoded.value().material.validation.valid()) {
    return core::Result<std::vector<std::uint8_t>>::failure(
        malformed("cannot update a semantically invalid OpenPrintTag"));
  }
  if (decoded.value().envelope.capability_access != 0U) {
    return core::Result<std::vector<std::uint8_t>>::failure({
        core::ErrorCategory::tag_write_protected,
        "Type 5 capability container does not permit writing",
        false,
    });
  }
  if (!decoded.value().envelope.auxiliary.has_value()) {
    return core::Result<std::vector<std::uint8_t>>::failure(
        malformed("OpenPrintTag has no auxiliary region"));
  }
  const auto& layout = *decoded.value().envelope.auxiliary;
  const auto aux = CborMapView::parse(tag_image.subview(layout.absolute_offset, layout.size));
  if (!aux.ok()) return core::Result<std::vector<std::uint8_t>>::failure(aux.error());
  const auto encoded_number = CborMapView::encode_number(consumed_grams);
  const auto updated_region = aux.value().replace(
      0U, core::ByteView(encoded_number), layout.size);
  if (!updated_region.ok()) {
    return core::Result<std::vector<std::uint8_t>>::failure(updated_region.error());
  }

  std::vector<std::uint8_t> output(tag_image.data, tag_image.data + tag_image.size);
  std::copy(
      updated_region.value().begin(), updated_region.value().end(),
      output.begin() + static_cast<std::ptrdiff_t>(layout.absolute_offset));
  const auto verified = decode(core::ByteView(output));
  if (!verified.ok() || !verified.value().material.validation.valid() ||
      !verified.value().material.consumed_weight.has_value() ||
      std::fabs(*verified.value().material.consumed_weight - consumed_grams) > 0.001) {
    return core::Result<std::vector<std::uint8_t>>::failure(
        malformed("updated auxiliary region failed semantic verification"));
  }
  return core::Result<std::vector<std::uint8_t>>::success(std::move(output));
}

}  // namespace opentag::nfc::openprinttag

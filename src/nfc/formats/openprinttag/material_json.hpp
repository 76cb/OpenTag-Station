#pragma once
#include "nfc/formats/openprinttag/spoolman_mapping.hpp"
#include <ArduinoJson.h>
namespace opentag::nfc::openprinttag {
inline void material_json(JsonObject out, const MaterialRecord &m) {
#define OPT_FIELD(name)                                                        \
  if (m.name)                                                                  \
    out[#name] = *m.name;                                                      \
  else                                                                         \
    out[#name] = nullptr
  OPT_FIELD(material_class);
  OPT_FIELD(material_type);
  OPT_FIELD(material_name);
  OPT_FIELD(material_abbreviation);
  OPT_FIELD(brand_name);
  OPT_FIELD(gtin);
  OPT_FIELD(brand_specific_instance_id);
  OPT_FIELD(brand_specific_package_id);
  OPT_FIELD(brand_specific_material_id);
  OPT_FIELD(nominal_netto_full_weight);
  OPT_FIELD(actual_netto_full_weight);
  OPT_FIELD(empty_container_weight);
  OPT_FIELD(nominal_full_length);
  OPT_FIELD(actual_full_length);
  OPT_FIELD(density);
  OPT_FIELD(filament_diameter);
  OPT_FIELD(consumed_weight);
  OPT_FIELD(manufactured_date);
  OPT_FIELD(expiration_date);
  OPT_FIELD(country_of_origin);
  OPT_FIELD(min_print_temperature);
  OPT_FIELD(max_print_temperature);
  OPT_FIELD(preheat_temperature);
  OPT_FIELD(min_bed_temperature);
  OPT_FIELD(max_bed_temperature);
  OPT_FIELD(min_chamber_temperature);
  OPT_FIELD(max_chamber_temperature);
  OPT_FIELD(chamber_temperature);
  OPT_FIELD(drying_temperature);
  OPT_FIELD(drying_time);
  OPT_FIELD(storage_location);
  OPT_FIELD(purchase_price);
  OPT_FIELD(purchase_currency);
  OPT_FIELD(purchase_time);
  OPT_FIELD(workgroup);
  OPT_FIELD(general_purpose_range_user);
  OPT_FIELD(last_stir_time);
  OPT_FIELD(shore_hardness_a);
  OPT_FIELD(shore_hardness_d);
  OPT_FIELD(min_nozzle_diameter);
  OPT_FIELD(transmission_distance);
  OPT_FIELD(container_width);
  OPT_FIELD(container_outer_diameter);
  OPT_FIELD(container_inner_diameter);
  OPT_FIELD(container_hole_diameter);
  OPT_FIELD(viscosity_18c);
  OPT_FIELD(viscosity_25c);
  OPT_FIELD(viscosity_40c);
  OPT_FIELD(viscosity_60c);
  OPT_FIELD(container_volumetric_capacity);
  OPT_FIELD(cure_wavelength);
  OPT_FIELD(primary_color_ral);
  OPT_FIELD(write_protection);
#undef OPT_FIELD
#define OPT_UUID(name)                                                         \
  if (m.name)                                                                  \
    out[#name] = instance_uuid_text(*m.name);                                  \
  else                                                                         \
    out[#name] = nullptr
  OPT_UUID(instance_uuid);
  OPT_UUID(package_uuid);
  OPT_UUID(material_uuid);
  OPT_UUID(brand_uuid);
#undef OPT_UUID
  auto color = [](JsonArray a, const ColorRgba &c) {
    a.add(c.red);
    a.add(c.green);
    a.add(c.blue);
    a.add(c.alpha);
  };
  if (m.primary_color)
    color(out["primary_color"].to<JsonArray>(), *m.primary_color);
  else
    out["primary_color"] = nullptr;
  auto secondary = out["secondary_colors"].to<JsonArray>();
  for (const auto &c : m.secondary_colors)
    if (c)
      color(secondary.add<JsonArray>(), *c);
  auto tags = out["tags"].to<JsonArray>();
  for (auto tag : m.tags)
    tags.add(tag);
  auto certifications = out["certifications"].to<JsonArray>();
  for (auto value : m.certifications)
    certifications.add(value);
  if (m.primary_color_lab) {
    auto lab = out["primary_color_lab"].to<JsonArray>();
    for (auto value : *m.primary_color_lab)
      lab.add(value);
  }
  out["unknown_main_fields"] = m.unknown_main_fields;
  out["unknown_auxiliary_fields"] = m.unknown_auxiliary_fields;
}
} // namespace opentag::nfc::openprinttag

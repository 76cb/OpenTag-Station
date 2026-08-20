#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace opentag::domain {

using SpoolId = std::int32_t;

struct Color {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
  std::uint8_t alpha{255};
};

struct Spool {
  SpoolId id{0};
  std::int32_t filament_id{0};
  std::string display_name;
  std::string vendor;
  std::string material;
  std::string subtype;
  std::optional<Color> primary_color;
  std::optional<float> remaining_grams;
  std::optional<float> initial_grams;
  float used_grams{0.0F};
  std::optional<float> empty_spool_grams;
  std::optional<float> package_empty_spool_grams;
  std::optional<float> vendor_empty_spool_grams;
  std::optional<std::string> location;
  std::optional<std::string> article_number;
  std::optional<std::string> openprinttag_instance_uuid;
  std::optional<std::string> nfc_uid;
  std::map<std::string, std::string> extra_json;
  bool archived{false};
};

}  // namespace opentag::domain

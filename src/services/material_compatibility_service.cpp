#include "services/material_compatibility_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace opentag::services {
namespace {

std::string uppercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

bool contains(const std::string& text, const std::string& token) {
  return text.find(token) != std::string::npos;
}

std::string material_description(
    const nfc::openprinttag::MaterialRecord& material) {
  std::string result;
  if (material.material_name.has_value()) result += *material.material_name;
  if (material.material_abbreviation.has_value()) {
    if (!result.empty()) result += ' ';
    result += *material.material_abbreviation;
  }
  return uppercase(result);
}

}  // namespace

std::vector<CompatibilityAdvisory> MaterialCompatibilityService::evaluate(
    const nfc::openprinttag::MaterialRecord& material,
    const config::ToolheadProfile& toolhead) {
  std::vector<CompatibilityAdvisory> result;
  result.reserve(6U);
  const auto description = material_description(material);
  const auto nozzle_material = uppercase(toolhead.nozzle_material);

  if (!toolhead.enabled) {
    result.push_back({
        CompatibilityAdvisoryCode::toolhead_disabled,
        CompatibilityAdvisorySeverity::warning,
        "Toolhead disabled",
        "This local toolhead profile is disabled; review its status before assignment.",
    });
  }

  const bool abrasive =
      contains(description, "-CF") || contains(description, " CF") ||
      contains(description, "-GF") || contains(description, " GF") ||
      contains(description, "CARBON FIBER") ||
      contains(description, "CARBON FIBRE") ||
      contains(description, "GLASS FIBER") ||
      contains(description, "GLASS FIBRE");
  if (abrasive && contains(nozzle_material, "BRASS")) {
    result.push_back({
        CompatibilityAdvisoryCode::abrasive_filament_brass_nozzle,
        CompatibilityAdvisorySeverity::warning,
        "Abrasive filament",
        "Abrasive filament may cause rapid wear on this brass nozzle.",
    });
  }

  if (material.min_print_temperature.has_value() &&
      *material.min_print_temperature > toolhead.maximum_temperature_c) {
    result.push_back({
        CompatibilityAdvisoryCode::temperature_above_toolhead_limit,
        CompatibilityAdvisorySeverity::warning,
        "Temperature limit",
        "The material minimum print temperature exceeds this toolhead profile's maximum.",
    });
  }

  if (material.min_nozzle_diameter.has_value() &&
      std::isfinite(*material.min_nozzle_diameter) &&
      *material.min_nozzle_diameter >
          static_cast<double>(toolhead.nozzle_diameter_mm) + 0.0001) {
    result.push_back({
        CompatibilityAdvisoryCode::nozzle_too_small,
        CompatibilityAdvisorySeverity::warning,
        "Nozzle size",
        "The configured nozzle is smaller than the material's minimum nozzle diameter.",
    });
  }

  if (contains(description, "TPU") || contains(description, "TPE") ||
      contains(description, "FLEX")) {
    result.push_back({
        CompatibilityAdvisoryCode::flexible_filament,
        CompatibilityAdvisorySeverity::information,
        "Flexible filament",
        "Confirm the selected toolhead and filament path are prepared for flexible material.",
    });
  }

  if (contains(description, "PVA") || contains(description, "BVOH") ||
      contains(description, "SUPPORT")) {
    result.push_back({
        CompatibilityAdvisoryCode::support_material,
        CompatibilityAdvisorySeverity::information,
        "Support material",
        "Confirm support-material handling, storage, and purge settings before printing.",
    });
  }
  return result;
}

}  // namespace opentag::services

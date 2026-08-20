#pragma once

#include <string>
#include <vector>

#include "config/configuration_service.hpp"
#include "nfc/formats/openprinttag/codec.hpp"

namespace opentag::services {

enum class CompatibilityAdvisoryCode {
  toolhead_disabled,
  abrasive_filament_brass_nozzle,
  temperature_above_toolhead_limit,
  nozzle_too_small,
  flexible_filament,
  support_material,
};

enum class CompatibilityAdvisorySeverity {
  information,
  warning,
};

struct CompatibilityAdvisory {
  CompatibilityAdvisoryCode code;
  CompatibilityAdvisorySeverity severity;
  std::string title;
  std::string message;
};

class MaterialCompatibilityService final {
 public:
  [[nodiscard]] static std::vector<CompatibilityAdvisory> evaluate(
      const nfc::openprinttag::MaterialRecord& material,
      const config::ToolheadProfile& toolhead);
};

}  // namespace opentag::services

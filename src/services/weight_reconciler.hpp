#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "domain/weight.hpp"

namespace opentag::services {

enum class ReconciliationDecision {
  unavailable,
  within_tolerance,
  warning,
  confirmation_required,
};

enum class ReconciliationAction {
  update_spoolman,
  update_openprinttag,
  update_both,
  ignore,
};

struct ReconciliationTolerances {
  float normal_grams{5.0F};
  float warning_grams{20.0F};

  [[nodiscard]] bool valid() const {
    return std::isfinite(normal_grams) && std::isfinite(warning_grams) &&
        normal_grams >= 0.0F && warning_grams >= normal_grams;
  }
};

struct ReconciliationResult {
  ReconciliationDecision decision{ReconciliationDecision::unavailable};
  std::optional<float> measured_remaining_grams;
  std::optional<float> spoolman_difference_grams;
  std::optional<float> tag_difference_grams;
  std::optional<float> maximum_absolute_difference_grams;
};

class WeightReconciler {
 public:
  static ReconciliationResult compare(
      const domain::WeightSnapshot& snapshot,
      ReconciliationTolerances tolerances) {
    ReconciliationResult result;
    result.measured_remaining_grams = snapshot.physical_remaining_grams();
    if (!result.measured_remaining_grams.has_value() || !tolerances.valid()) {
      return result;
    }

    float maximum_difference = 0.0F;
    bool compared = false;
    if (snapshot.spoolman_remaining_grams.has_value() &&
        std::isfinite(*snapshot.spoolman_remaining_grams) &&
        *snapshot.spoolman_remaining_grams >= 0.0F) {
      result.spoolman_difference_grams =
          *result.measured_remaining_grams - *snapshot.spoolman_remaining_grams;
      maximum_difference = std::max(
          maximum_difference, std::fabs(*result.spoolman_difference_grams));
      compared = true;
    }
    if (snapshot.tag_remaining_grams.has_value() &&
        std::isfinite(*snapshot.tag_remaining_grams) &&
        *snapshot.tag_remaining_grams >= 0.0F) {
      result.tag_difference_grams =
          *result.measured_remaining_grams - *snapshot.tag_remaining_grams;
      maximum_difference = std::max(
          maximum_difference, std::fabs(*result.tag_difference_grams));
      compared = true;
    }
    if (!compared) return result;

    result.maximum_absolute_difference_grams = maximum_difference;
    result.decision = maximum_difference <= tolerances.normal_grams
                          ? ReconciliationDecision::within_tolerance
                      : maximum_difference <= tolerances.warning_grams
                          ? ReconciliationDecision::warning
                          : ReconciliationDecision::confirmation_required;
    return result;
  }

  static ReconciliationResult compare(
      const domain::WeightSnapshot& snapshot,
      float tolerance_grams) {
    return compare(snapshot, {tolerance_grams, tolerance_grams});
  }
};

}  // namespace opentag::services

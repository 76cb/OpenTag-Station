#pragma once

#include <cmath>
#include <optional>

namespace opentag::domain {

enum class EmptyWeightSource {
  openprinttag,
  spoolman_spool,
  package_default,
  vendor_default,
  manual,
  unavailable,
};

struct WeightReading {
  float gross_grams{0.0F};
  bool stable{false};
};

struct WeightSnapshot {
  WeightReading physical;
  std::optional<float> empty_spool_grams;
  EmptyWeightSource empty_weight_source{EmptyWeightSource::unavailable};
  std::optional<float> spoolman_remaining_grams;
  std::optional<float> tag_remaining_grams;

  [[nodiscard]] std::optional<float> physical_remaining_grams() const {
    if (!physical.stable || !empty_spool_grams.has_value() ||
        !std::isfinite(physical.gross_grams) ||
        !std::isfinite(*empty_spool_grams) || *empty_spool_grams < 0.0F) {
      return std::nullopt;
    }
    const auto remaining = physical.gross_grams - *empty_spool_grams;
    return remaining >= 0.0F ? std::optional<float>(remaining) : std::nullopt;
  }
};

struct EmptyWeightCandidates {
  std::optional<float> openprinttag_grams;
  std::optional<float> spoolman_spool_grams;
  std::optional<float> package_default_grams;
  std::optional<float> vendor_default_grams;
  std::optional<float> manual_grams;
};

struct ResolvedEmptyWeight {
  float grams{0.0F};
  EmptyWeightSource source{EmptyWeightSource::unavailable};
};

class EmptyWeightResolver {
 public:
  [[nodiscard]] static std::optional<ResolvedEmptyWeight> resolve(
      const EmptyWeightCandidates& candidates) {
    const auto valid = [](const std::optional<float>& value) {
      return value.has_value() && std::isfinite(*value) && *value >= 0.0F;
    };
    if (valid(candidates.openprinttag_grams)) {
      return ResolvedEmptyWeight{
          *candidates.openprinttag_grams, EmptyWeightSource::openprinttag};
    }
    if (valid(candidates.spoolman_spool_grams)) {
      return ResolvedEmptyWeight{
          *candidates.spoolman_spool_grams, EmptyWeightSource::spoolman_spool};
    }
    if (valid(candidates.package_default_grams)) {
      return ResolvedEmptyWeight{
          *candidates.package_default_grams, EmptyWeightSource::package_default};
    }
    if (valid(candidates.vendor_default_grams)) {
      return ResolvedEmptyWeight{
          *candidates.vendor_default_grams, EmptyWeightSource::vendor_default};
    }
    if (valid(candidates.manual_grams)) {
      return ResolvedEmptyWeight{
          *candidates.manual_grams, EmptyWeightSource::manual};
    }
    return std::nullopt;
  }
};

}  // namespace opentag::domain

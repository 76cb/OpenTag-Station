#pragma once

#include <cmath>
#include <optional>
#include <utility>

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
    // Spoolman is canonical: a value corrected on the Spoolman spool must win
    // over a copy written to the tag earlier, which only changes when the tag
    // is rewritten.
    // A value of exactly 0 is usually "never set" (rc.9 could create spools
    // that way, and a tag written from one copies it), so a zero only wins
    // when no source at all holds a real weight.
    const auto real = [&valid](const std::optional<float>& value) {
      return valid(value) && *value > 0.0F;
    };
    const std::pair<const std::optional<float>*, EmptyWeightSource> sources[] = {
        {&candidates.spoolman_spool_grams, EmptyWeightSource::spoolman_spool},
        {&candidates.openprinttag_grams, EmptyWeightSource::openprinttag},
        {&candidates.package_default_grams, EmptyWeightSource::package_default},
        {&candidates.vendor_default_grams, EmptyWeightSource::vendor_default},
        {&candidates.manual_grams, EmptyWeightSource::manual}};
    for (const auto& [grams, source] : sources) {
      if (real(*grams)) return ResolvedEmptyWeight{**grams, source};
    }
    for (const auto& [grams, source] : sources) {
      if (valid(*grams)) return ResolvedEmptyWeight{**grams, source};
    }
    return std::nullopt;
  }
};

}  // namespace opentag::domain

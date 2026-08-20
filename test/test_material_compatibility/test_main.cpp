#include <unity.h>

#include <algorithm>
#include <vector>

#include "config/configuration_service.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "services/material_compatibility_service.hpp"

namespace {

using opentag::config::ToolheadProfile;
using opentag::nfc::openprinttag::MaterialRecord;
using opentag::services::CompatibilityAdvisory;
using opentag::services::CompatibilityAdvisoryCode;
using opentag::services::MaterialCompatibilityService;

bool has(
    const std::vector<CompatibilityAdvisory>& advisories,
    CompatibilityAdvisoryCode code) {
  return std::any_of(
      advisories.begin(), advisories.end(),
      [code](const auto& advisory) { return advisory.code == code; });
}

ToolheadProfile profile() {
  ToolheadProfile result;
  result.backend_id = 0;
  result.display_name = "T1";
  result.nozzle_diameter_mm = 0.4F;
  result.nozzle_material = "brass";
  result.maximum_temperature_c = 300U;
  result.enabled = true;
  return result;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_carbon_fiber_on_brass_warns_about_abrasion() {
  MaterialRecord material;
  material.material_name = "PA-CF";

  const auto result = MaterialCompatibilityService::evaluate(material, profile());

  TEST_ASSERT_TRUE(has(
      result, CompatibilityAdvisoryCode::abrasive_filament_brass_nozzle));
}

void test_hardened_nozzle_avoids_brass_warning() {
  MaterialRecord material;
  material.material_abbreviation = "PETG-GF";
  auto toolhead = profile();
  toolhead.nozzle_material = "ObXidian hardened steel";

  const auto result = MaterialCompatibilityService::evaluate(material, toolhead);

  TEST_ASSERT_FALSE(has(
      result, CompatibilityAdvisoryCode::abrasive_filament_brass_nozzle));
}

void test_temperature_and_minimum_nozzle_size_are_checked() {
  MaterialRecord material;
  material.material_name = "Engineering Polymer";
  material.min_print_temperature = 315;
  material.min_nozzle_diameter = 0.6;

  const auto result = MaterialCompatibilityService::evaluate(material, profile());

  TEST_ASSERT_TRUE(has(
      result, CompatibilityAdvisoryCode::temperature_above_toolhead_limit));
  TEST_ASSERT_TRUE(has(result, CompatibilityAdvisoryCode::nozzle_too_small));
}

void test_flexible_and_support_materials_get_advisories() {
  MaterialRecord flexible;
  flexible.material_abbreviation = "TPU";
  MaterialRecord support;
  support.material_name = "BVOH Support";

  const auto flexible_result =
      MaterialCompatibilityService::evaluate(flexible, profile());
  const auto support_result =
      MaterialCompatibilityService::evaluate(support, profile());

  TEST_ASSERT_TRUE(has(
      flexible_result, CompatibilityAdvisoryCode::flexible_filament));
  TEST_ASSERT_TRUE(has(
      support_result, CompatibilityAdvisoryCode::support_material));
}

void test_disabled_profile_is_visible_but_remains_advisory() {
  MaterialRecord material;
  material.material_name = "PLA";
  auto toolhead = profile();
  toolhead.enabled = false;

  const auto result = MaterialCompatibilityService::evaluate(material, toolhead);

  TEST_ASSERT_TRUE(has(result, CompatibilityAdvisoryCode::toolhead_disabled));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_carbon_fiber_on_brass_warns_about_abrasion);
  RUN_TEST(test_hardened_nozzle_avoids_brass_warning);
  RUN_TEST(test_temperature_and_minimum_nozzle_size_are_checked);
  RUN_TEST(test_flexible_and_support_materials_get_advisories);
  RUN_TEST(test_disabled_profile_is_visible_but_remains_advisory);
  return UNITY_END();
}

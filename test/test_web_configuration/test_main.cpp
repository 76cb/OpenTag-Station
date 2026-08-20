#include <unity.h>

#include <string>
#include <vector>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "web/api_router.hpp"
#include "web/configuration_patch.hpp"

namespace {

using opentag::config::Configuration;
using opentag::config::VersionedConfiguration;
using opentag::core::ErrorCategory;
using opentag::web::apply_configuration_patch;
using opentag::web::api::ConfigurationPatchMutation;

VersionedConfiguration configured_snapshot() {
  Configuration configuration;
  configuration.device.hostname = "original-station";
  configuration.wifi.ssid = "original-network";
  configuration.wifi.password = "wifi-secret";
  configuration.web.access_token = "original-access-token";
  configuration.spoolman.url = "https://spoolman.local";
  configuration.spoolman.authentication_token = "spoolman-secret";
  configuration.spoolman.ca_certificate_pem = "spoolman-ca";
  configuration.filabridge.url = "http://filabridge.local:5000";
  configuration.filabridge.authentication_token = "filabridge-secret";
  configuration.filabridge.ca_certificate_pem = "filabridge-ca";
  configuration.filabridge.selected_printer_id = "printer-a";
  configuration.toolheads = {
      {0, "T1", 0.4F, true, "brass", 300U, "primary"},
  };
  opentag::domain::ConfirmedSpoolMapping mapping;
  mapping.spool_id = 17;
  mapping.instance_uuid = "00112233-4455-6677-8899-aabbccddeeff";
  configuration.spool_identity_mappings.push_back(mapping);
  configuration.setup.completed_steps = 1U;

  opentag::services::ScaleCalibration calibration;
  calibration.zero_offset_counts = 12345;
  calibration.counts_per_gram = 42.5;
  calibration.reference_grams = 500.0F;
  calibration.load_cell_capacity_grams = 5000.0F;
  configuration.scale_calibration = calibration;
  return {configuration, 41U};
}

ConfigurationPatchMutation patch_for(
    const VersionedConfiguration& current) {
  ConfigurationPatchMutation patch;
  patch.expected_revision = current.revision;
  return patch;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_omitted_credentials_and_unrelated_values_are_preserved() {
  const auto current = configured_snapshot();
  TEST_ASSERT_TRUE(current.configuration.validate().ok());
  auto patch = patch_for(current);
  opentag::web::api::DevicePatch device;
  device.hostname = "renamed-station";
  patch.device = device;
  opentag::web::api::WifiPatch wifi;
  wifi.ssid = "new-network";
  patch.wifi = wifi;

  const auto applied = apply_configuration_patch(current, patch);
  TEST_ASSERT_TRUE(applied.ok());
  const auto& result = applied.value();
  TEST_ASSERT_EQUAL_STRING("renamed-station", result.device.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("new-network", result.wifi.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("wifi-secret", result.wifi.password.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "original-access-token", result.web.access_token.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "spoolman-secret", result.spoolman.authentication_token.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "spoolman-ca", result.spoolman.ca_certificate_pem.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "filabridge-secret", result.filabridge.authentication_token.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "filabridge-ca", result.filabridge.ca_certificate_pem.c_str());
  TEST_ASSERT_EQUAL_UINT(1U, result.toolheads.size());
  TEST_ASSERT_EQUAL_UINT(1U, result.spool_identity_mappings.size());
  TEST_ASSERT_EQUAL_UINT32(1U, result.setup.completed_steps);
  TEST_ASSERT_TRUE(result.scale_calibration.has_value());
}

void test_explicit_empty_credentials_clear_only_requested_secrets() {
  const auto current = configured_snapshot();
  auto patch = patch_for(current);
  opentag::web::api::WifiPatch wifi;
  wifi.password = "";
  patch.wifi = wifi;
  opentag::web::api::WebPatch web;
  web.access_token = "";
  patch.web = web;
  opentag::web::api::SpoolmanPatch spoolman;
  spoolman.authentication_token = "";
  spoolman.ca_certificate_pem = "";
  patch.spoolman = spoolman;
  opentag::web::api::FilaBridgePatch filabridge;
  filabridge.authentication_token = "";
  patch.filabridge = filabridge;

  const auto applied = apply_configuration_patch(current, patch);
  TEST_ASSERT_TRUE(applied.ok());
  const auto& result = applied.value();
  TEST_ASSERT_TRUE(result.wifi.password.empty());
  TEST_ASSERT_TRUE(result.web.access_token.empty());
  TEST_ASSERT_TRUE(result.spoolman.authentication_token.empty());
  TEST_ASSERT_TRUE(result.spoolman.ca_certificate_pem.empty());
  TEST_ASSERT_TRUE(result.filabridge.authentication_token.empty());
  TEST_ASSERT_EQUAL_STRING(
      "filabridge-ca", result.filabridge.ca_certificate_pem.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "original-network", result.wifi.ssid.c_str());
}

void test_web_access_token_replacement_and_validation_are_atomic() {
  const auto current = configured_snapshot();

  auto replacement_patch = patch_for(current);
  opentag::web::api::WebPatch replacement;
  replacement.access_token = "replacement.Token_1234";
  replacement_patch.web = replacement;
  const auto replaced =
      apply_configuration_patch(current, replacement_patch);
  TEST_ASSERT_TRUE(replaced.ok());
  TEST_ASSERT_EQUAL_STRING(
      "replacement.Token_1234", replaced.value().web.access_token.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "original-access-token", current.configuration.web.access_token.c_str());

  auto short_patch = patch_for(current);
  opentag::web::api::WebPatch short_value;
  short_value.access_token = "too-short";
  short_patch.web = short_value;
  const auto short_result = apply_configuration_patch(current, short_patch);
  TEST_ASSERT_FALSE(short_result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::configuration),
      static_cast<int>(short_result.error().category));

  auto invalid_character_patch = patch_for(current);
  opentag::web::api::WebPatch invalid_character;
  invalid_character.access_token = "invalid-token-value!";
  invalid_character_patch.web = invalid_character;
  const auto invalid_character_result =
      apply_configuration_patch(current, invalid_character_patch);
  TEST_ASSERT_FALSE(invalid_character_result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::configuration),
      static_cast<int>(invalid_character_result.error().category));
  TEST_ASSERT_EQUAL_STRING(
      "original-access-token", current.configuration.web.access_token.c_str());
}

void test_stale_revision_returns_conflict_without_a_proposal() {
  const auto current = configured_snapshot();
  auto patch = patch_for(current);
  --patch.expected_revision;
  opentag::web::api::DevicePatch device;
  device.hostname = "must-not-apply";
  patch.device = device;

  const auto applied = apply_configuration_patch(current, patch);
  TEST_ASSERT_FALSE(applied.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::conflict),
      static_cast<int>(applied.error().category));
  TEST_ASSERT_FALSE(applied.error().retryable);
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, applied.error().message.find("reload and retry"));
  TEST_ASSERT_EQUAL_STRING(
      "original-station", current.configuration.device.hostname.c_str());
}

void test_invalid_merged_configuration_fails_atomically() {
  const auto current = configured_snapshot();
  auto patch = patch_for(current);
  opentag::web::api::DevicePatch device;
  device.hostname = "valid-but-uncommitted";
  device.dim_after_ms = current.configuration.device.sleep_after_ms + 1U;
  patch.device = device;

  const auto applied = apply_configuration_patch(current, patch);
  TEST_ASSERT_FALSE(applied.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::configuration),
      static_cast<int>(applied.error().category));
  TEST_ASSERT_EQUAL_STRING(
      "original-station", current.configuration.device.hostname.c_str());
  TEST_ASSERT_EQUAL_UINT32(
      120000U, current.configuration.device.dim_after_ms);
}

void test_profile_identity_or_capacity_change_clears_calibration_only_then() {
  const auto current = configured_snapshot();

  auto overload_patch = patch_for(current);
  opentag::web::api::ScaleProfilePatch overload;
  overload.overload_ratio = 1.25F;
  overload_patch.scale_profile = overload;
  const auto overload_applied =
      apply_configuration_patch(current, overload_patch);
  TEST_ASSERT_TRUE(overload_applied.ok());
  TEST_ASSERT_TRUE(overload_applied.value().scale_calibration.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1.25F, overload_applied.value().scale_hardware.overload_ratio);

  auto same_patch = patch_for(current);
  opentag::web::api::ScaleProfilePatch same;
  same.id = "yzc-133-5kg";
  same_patch.scale_profile = same;
  const auto same_applied = apply_configuration_patch(current, same_patch);
  TEST_ASSERT_TRUE(same_applied.ok());
  TEST_ASSERT_TRUE(same_applied.value().scale_calibration.has_value());

  auto changed_patch = patch_for(current);
  opentag::web::api::ScaleProfilePatch changed;
  changed.id = "yzc-133-2kg";
  changed_patch.scale_profile = changed;
  const auto changed_applied =
      apply_configuration_patch(current, changed_patch);
  TEST_ASSERT_TRUE(changed_applied.ok());
  TEST_ASSERT_EQUAL_STRING(
      "YZC-133", changed_applied.value().scale_hardware.load_cell_model.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F,
      2000.0F,
      changed_applied.value().scale_hardware.rated_capacity_grams);
  TEST_ASSERT_FALSE(changed_applied.value().scale_calibration.has_value());
}

void test_all_typed_nonsecret_sections_are_applied() {
  const auto current = configured_snapshot();
  auto patch = patch_for(current);

  opentag::web::api::DevicePatch device;
  device.brightness_percent = 55U;
  device.dim_after_ms = 30000U;
  device.sleep_after_ms = 60000U;
  device.update_channel = "beta";
  patch.device = device;
  opentag::web::api::WifiPatch wifi;
  wifi.auto_reconnect = false;
  wifi.connect_timeout_ms = 20000U;
  wifi.reconnect_initial_ms = 2000U;
  wifi.reconnect_max_ms = 30000U;
  patch.wifi = wifi;
  opentag::web::api::SpoolmanPatch spoolman;
  spoolman.identity_field = "station_uuid";
  spoolman.nfc_uid_field = "tag_uid";
  patch.spoolman = spoolman;
  opentag::web::api::FilaBridgePatch filabridge;
  filabridge.selected_printer_id = "printer-b";
  patch.filabridge = filabridge;
  opentag::web::api::ScaleProfilePatch scale;
  scale.overload_ratio = 1.20F;
  patch.scale_profile = scale;
  patch.toolheads = std::vector<opentag::web::api::ToolheadProfilePatch>{
      {1, "T2", 0.6F, true, "hardened steel", 320U, "abrasive"},
  };
  opentag::web::api::ReconciliationPatch reconciliation;
  reconciliation.normal_tolerance_grams = 8.0F;
  reconciliation.warning_tolerance_grams = 30.0F;
  patch.reconciliation = reconciliation;

  const auto applied = apply_configuration_patch(current, patch);
  TEST_ASSERT_TRUE(applied.ok());
  const auto& result = applied.value();
  TEST_ASSERT_EQUAL_UINT8(55U, result.device.brightness_percent);
  TEST_ASSERT_FALSE(result.wifi.auto_reconnect);
  TEST_ASSERT_EQUAL_UINT32(20000U, result.wifi.connect_timeout_ms);
  TEST_ASSERT_EQUAL_STRING(
      "station_uuid", result.spoolman.identity_field.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "tag_uid", result.spoolman.nfc_uid_field.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "printer-b", result.filabridge.selected_printer_id.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1.20F, result.scale_hardware.overload_ratio);
  TEST_ASSERT_TRUE(result.scale_calibration.has_value());
  TEST_ASSERT_EQUAL_UINT(1U, result.toolheads.size());
  TEST_ASSERT_EQUAL_INT32(1, result.toolheads.front().backend_id);
  TEST_ASSERT_EQUAL_STRING(
      "hardened steel", result.toolheads.front().nozzle_material.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 8.0F, result.reconciliation.normal_tolerance_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 30.0F, result.reconciliation.warning_tolerance_grams);
}

void test_conflicting_profile_alias_is_rejected() {
  const auto current = configured_snapshot();
  auto patch = patch_for(current);
  opentag::web::api::ScaleProfilePatch scale;
  scale.id = "yzc-133-2kg";
  scale.rated_capacity_grams = 5000U;
  patch.scale_profile = scale;

  const auto applied = apply_configuration_patch(current, patch);
  TEST_ASSERT_FALSE(applied.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::configuration),
      static_cast<int>(applied.error().category));
  TEST_ASSERT_TRUE(current.configuration.scale_calibration.has_value());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_omitted_credentials_and_unrelated_values_are_preserved);
  RUN_TEST(test_explicit_empty_credentials_clear_only_requested_secrets);
  RUN_TEST(test_web_access_token_replacement_and_validation_are_atomic);
  RUN_TEST(test_stale_revision_returns_conflict_without_a_proposal);
  RUN_TEST(test_invalid_merged_configuration_fails_atomically);
  RUN_TEST(test_profile_identity_or_capacity_change_clears_calibration_only_then);
  RUN_TEST(test_all_typed_nonsecret_sections_are_applied);
  RUN_TEST(test_conflicting_profile_alias_is_rejected);
  return UNITY_END();
}

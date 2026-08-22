#include <unity.h>

#include <optional>
#include <string>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "services/first_run_setup.hpp"

namespace {

using opentag::config::Configuration;
using opentag::config::ConfigurationService;
using opentag::config::IConfigurationDocumentStore;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::services::FirstRunSetup;
using opentag::services::IScaleCalibrationStore;
using opentag::services::ScaleCalibration;
using opentag::services::SetupStep;

class MemoryDocumentStore final : public IConfigurationDocumentStore {
 public:
  std::optional<std::string> document;
  std::optional<std::string> backup_document;
  bool load_fails{false};
  bool save_fails{false};
  std::size_t save_count{0U};

  Result<std::optional<std::string>> load_configuration_document() override {
    if (load_fails) {
      return Result<std::optional<std::string>>::failure(
          {ErrorCategory::storage, "document load failed", false});
    }
    return Result<std::optional<std::string>>::success(document);
  }

  Result<std::optional<std::string>>
  load_configuration_backup_document() override {
    if (load_fails) {
      return Result<std::optional<std::string>>::failure(
          {ErrorCategory::storage, "backup load failed", false});
    }
    return Result<std::optional<std::string>>::success(backup_document);
  }

  Result<void> save_configuration_document(const std::string& value) override {
    ++save_count;
    if (save_fails) {
      return Result<void>::failure(
          {ErrorCategory::storage, "document save failed", false});
    }
    document = value;
    return Result<void>::success();
  }
};

class LegacyScaleStore final : public IScaleCalibrationStore {
 public:
  std::optional<ScaleCalibration> calibration;
  bool load_fails{false};
  bool save_fails{false};
  bool clear_fails{false};
  std::size_t save_count{0U};
  std::size_t clear_count{0U};

  Result<std::optional<ScaleCalibration>> load_scale_calibration() override {
    if (load_fails) {
      return Result<std::optional<ScaleCalibration>>::failure(
          {ErrorCategory::storage, "legacy load failed", false});
    }
    return Result<std::optional<ScaleCalibration>>::success(calibration);
  }

  Result<void> save_scale_calibration(const ScaleCalibration& value) override {
    ++save_count;
    if (save_fails) {
      return Result<void>::failure(
          {ErrorCategory::storage, "legacy save failed", false});
    }
    calibration = value;
    return Result<void>::success();
  }

  Result<void> clear_scale_calibration() override {
    ++clear_count;
    if (clear_fails) {
      return Result<void>::failure(
          {ErrorCategory::storage, "legacy clear failed", false});
    }
    calibration.reset();
    return Result<void>::success();
  }
};

ScaleCalibration valid_scale(float capacity_grams = 2000.0F) {
  ScaleCalibration result;
  result.zero_offset_counts = 12345;
  result.counts_per_gram = -42.5;
  result.reference_grams = 500.0F;
  result.load_cell_capacity_grams = capacity_grams;
  return result;
}

const char* schema_one_document = R"json({
  "schema_version": 1,
  "hardware_id": "wt32-sc01-plus-rev-a",
  "device": {"hostname": "legacy-station"},
  "wifi": {},
  "spoolman": {},
  "filabridge": {},
  "reconciliation": {"normal_tolerance_grams": 7.0},
  "future_section": {"preserve_me": 42}
})json";

const char* schema_two_document = R"json({
  "schema_version": 2,
  "hardware_id": "wt32-sc01-plus-rev-a",
  "device": {},
  "wifi": {},
  "spoolman": {"identity_field":"legacy_uuid"},
  "filabridge": {},
  "toolheads": [],
  "reconciliation": {},
  "setup": {},
  "future_section": {"preserve_me": 43}
})json";

const char* schema_three_2kg_calibration_without_profile = R"json({
  "schema_version": 3,
  "hardware_id": "wt32-sc01-plus-rev-a",
  "device": {},
  "wifi": {},
  "spoolman": {},
  "filabridge": {},
  "scale": {
    "schema_version": 1,
    "zero_offset_counts": 12345,
    "counts_per_gram": -42.5,
    "reference_grams": 500.0,
    "load_cell_capacity_grams": 2000.0
  },
  "toolheads": [],
  "spool_identity_mappings": [],
  "reconciliation": {},
  "setup": {}
})json";

const char* schema_three_web_document = R"json({
  "schema_version": 3,
  "hardware_id": "wt32-sc01-plus-rev-a",
  "web": {
    "access_token": "LocalApiToken-0123456789._~",
    "future_policy": "preserve-me"
  },
  "future_section": {"preserve_me": 44}
})json";

}  // namespace

void setUp() {}
void tearDown() {}

void test_missing_document_creates_current_schema_defaults() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.status().initialized);
  TEST_ASSERT_TRUE(service.status().persistence_available);
  TEST_ASSERT_EQUAL_UINT32(Configuration::current_schema, service.snapshot().schema_version);
  TEST_ASSERT_EQUAL_STRING("opentag-station", service.snapshot().device.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "YZC-133", service.snapshot().scale_hardware.load_cell_model.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F, service.snapshot().scale_hardware.rated_capacity_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1.10F, service.snapshot().scale_hardware.overload_ratio);
  TEST_ASSERT_FALSE(service.snapshot().scale_calibration.has_value());
  TEST_ASSERT_EQUAL_UINT(1U, documents.save_count);
  TEST_ASSERT_TRUE(documents.document.has_value());
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, documents.document->find("\"scale_profile\""));
  TEST_ASSERT_EQUAL(std::string::npos, documents.document->find("\"scale\":"));
}

void test_legacy_scale_calibration_is_migrated_and_mirrored() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  legacy.calibration = valid_scale();
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.snapshot().scale_calibration.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F, service.snapshot().scale_hardware.rated_capacity_grams);
  TEST_ASSERT_EQUAL_INT32(
      12345, service.snapshot().scale_calibration->zero_offset_counts);

  auto changed = valid_scale();
  changed.zero_offset_counts = 54321;
  TEST_ASSERT_TRUE(service.save_scale_calibration(changed).ok());
  TEST_ASSERT_EQUAL_UINT(1U, legacy.save_count);
  TEST_ASSERT_EQUAL_INT32(54321, legacy.calibration->zero_offset_counts);
  TEST_ASSERT_EQUAL_INT32(
      54321, service.load_scale_calibration().value()->zero_offset_counts);
}

void test_legacy_save_failure_does_not_commit_central_calibration() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  const auto before = service.versioned_snapshot();
  TEST_ASSERT_TRUE(documents.document.has_value());
  const auto document_before = *documents.document;

  legacy.save_fails = true;
  const auto result = service.save_scale_calibration(valid_scale(5000.0F));

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::storage),
      static_cast<int>(result.error().category));
  TEST_ASSERT_EQUAL_UINT64(before.revision, service.revision());
  TEST_ASSERT_FALSE(service.snapshot().scale_calibration.has_value());
  TEST_ASSERT_EQUAL_STRING(document_before.c_str(), documents.document->c_str());
  TEST_ASSERT_FALSE(legacy.calibration.has_value());
  TEST_ASSERT_EQUAL_UINT(1U, legacy.save_count);
}

void test_schema_three_2kg_calibration_infers_missing_hardware_profile() {
  MemoryDocumentStore documents;
  documents.document = schema_three_2kg_calibration_without_profile;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);

  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_FALSE(service.status().migrated);
  const auto configured = service.snapshot();
  TEST_ASSERT_EQUAL_STRING(
      "YZC-133", configured.scale_hardware.load_cell_model.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F, configured.scale_hardware.rated_capacity_grams);
  TEST_ASSERT_TRUE(configured.scale_calibration.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F,
      configured.scale_calibration->load_cell_capacity_grams);
  TEST_ASSERT_TRUE(configured.web.access_token.empty());
}

void test_schema_three_web_token_round_trips_and_preserves_unknown_fields() {
  MemoryDocumentStore documents;
  documents.document = schema_three_web_document;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);

  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_FALSE(service.status().migrated);
  TEST_ASSERT_EQUAL_STRING(
      "LocalApiToken-0123456789._~",
      service.snapshot().web.access_token.c_str());

  auto updated = service.snapshot();
  updated.device.hostname = "web-token-roundtrip";
  TEST_ASSERT_TRUE(service.replace(updated).ok());
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, documents.document->find("future_policy"));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, documents.document->find("preserve-me"));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, documents.document->find("future_section"));

  ConfigurationService restored(documents, legacy);
  TEST_ASSERT_TRUE(restored.initialize().ok());
  TEST_ASSERT_EQUAL_STRING(
      "LocalApiToken-0123456789._~",
      restored.snapshot().web.access_token.c_str());
}

void test_uncalibrated_2kg_profile_round_trips_separately_from_calibration() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService writer(documents, legacy);
  TEST_ASSERT_TRUE(writer.initialize().ok());
  auto configured = writer.snapshot();
  configured.scale_hardware.rated_capacity_grams = 2000.0F;
  configured.scale_hardware.overload_ratio = 1.15F;
  TEST_ASSERT_TRUE(writer.replace(configured).ok());
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, documents.document->find("\"scale_profile\""));
  TEST_ASSERT_EQUAL(std::string::npos, documents.document->find("\"scale\":"));

  ConfigurationService reader(documents, legacy);
  TEST_ASSERT_TRUE(reader.initialize().ok());
  const auto restored = reader.snapshot();
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F, restored.scale_hardware.rated_capacity_grams);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1.15F, restored.scale_hardware.overload_ratio);
  TEST_ASSERT_FALSE(restored.scale_calibration.has_value());
}

void test_5kg_profile_and_calibration_round_trip_and_mismatch_is_rejected() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService writer(documents, legacy);
  TEST_ASSERT_TRUE(writer.initialize().ok());
  TEST_ASSERT_TRUE(writer.save_scale_calibration(valid_scale(5000.0F)).ok());

  ConfigurationService reader(documents, legacy);
  TEST_ASSERT_TRUE(reader.initialize().ok());
  auto restored = reader.snapshot();
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F, restored.scale_hardware.rated_capacity_grams);
  TEST_ASSERT_TRUE(restored.scale_calibration.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F,
      restored.scale_calibration->load_cell_capacity_grams);

  restored.scale_hardware.rated_capacity_grams = 2000.0F;
  TEST_ASSERT_FALSE(reader.replace(restored).ok());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F,
      reader.snapshot().scale_hardware.rated_capacity_grams);

  restored.scale_calibration.reset();
  TEST_ASSERT_TRUE(reader.replace(restored).ok());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F,
      reader.snapshot().scale_hardware.rated_capacity_grams);
  TEST_ASSERT_FALSE(reader.snapshot().scale_calibration.has_value());
}

void test_profile_change_clears_legacy_calibration_before_document_commit() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.save_scale_calibration(valid_scale(5000.0F)).ok());
  TEST_ASSERT_TRUE(legacy.calibration.has_value());

  const auto before = service.versioned_snapshot();
  const auto save_count = documents.save_count;
  auto changed = before.configuration;
  changed.scale_hardware.rated_capacity_grams = 2000.0F;
  changed.scale_calibration.reset();

  TEST_ASSERT_TRUE(
      service.replace_if_revision(changed, before.revision).ok());
  TEST_ASSERT_EQUAL_UINT(1U, legacy.clear_count);
  TEST_ASSERT_FALSE(legacy.calibration.has_value());
  TEST_ASSERT_EQUAL_UINT(save_count + 1U, documents.save_count);
  TEST_ASSERT_EQUAL_UINT64(before.revision + 1U, service.revision());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F,
      service.snapshot().scale_hardware.rated_capacity_grams);
  TEST_ASSERT_FALSE(service.snapshot().scale_calibration.has_value());
  TEST_ASSERT_EQUAL(
      std::string::npos, documents.document->find("\"scale\":"));
}

void test_profile_change_clears_stale_legacy_when_central_calibration_is_absent() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  {
    ConfigurationService writer(documents, legacy);
    TEST_ASSERT_TRUE(writer.initialize().ok());
    TEST_ASSERT_FALSE(writer.snapshot().scale_calibration.has_value());
  }

  legacy.calibration = valid_scale(5000.0F);
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_FALSE(service.snapshot().scale_calibration.has_value());
  TEST_ASSERT_TRUE(legacy.calibration.has_value());

  const auto before = service.versioned_snapshot();
  auto changed = before.configuration;
  changed.scale_hardware.rated_capacity_grams = 2000.0F;
  TEST_ASSERT_TRUE(
      service.replace_if_revision(changed, before.revision).ok());
  TEST_ASSERT_EQUAL_UINT(1U, legacy.clear_count);
  TEST_ASSERT_FALSE(legacy.calibration.has_value());
  TEST_ASSERT_FALSE(service.snapshot().scale_calibration.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 2000.0F,
      service.snapshot().scale_hardware.rated_capacity_grams);
}

void test_legacy_clear_failure_aborts_profile_change_transactionally() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.save_scale_calibration(valid_scale(5000.0F)).ok());

  const auto before = service.versioned_snapshot();
  const auto document_before = *documents.document;
  const auto save_count = documents.save_count;
  legacy.clear_fails = true;
  auto changed = before.configuration;
  changed.scale_hardware.rated_capacity_grams = 2000.0F;
  changed.scale_calibration.reset();

  const auto result = service.replace_if_revision(changed, before.revision);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::storage),
      static_cast<int>(result.error().category));
  TEST_ASSERT_EQUAL_UINT(1U, legacy.clear_count);
  TEST_ASSERT_TRUE(legacy.calibration.has_value());
  TEST_ASSERT_EQUAL_UINT(save_count, documents.save_count);
  TEST_ASSERT_EQUAL_STRING(document_before.c_str(), documents.document->c_str());
  TEST_ASSERT_EQUAL_UINT64(before.revision, service.revision());
  const auto unchanged = service.snapshot();
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F, unchanged.scale_hardware.rated_capacity_grams);
  TEST_ASSERT_TRUE(unchanged.scale_calibration.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 5000.0F,
      unchanged.scale_calibration->load_cell_capacity_grams);
}

void test_schema_one_migrates_additively_and_preserves_unknown_fields() {
  MemoryDocumentStore documents;
  documents.document = schema_one_document;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.status().migrated);
  TEST_ASSERT_EQUAL_UINT32(1U, service.status().loaded_schema);
  TEST_ASSERT_EQUAL_UINT32(Configuration::current_schema, service.snapshot().schema_version);
  TEST_ASSERT_EQUAL_STRING("stable", service.snapshot().device.update_channel.c_str());
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 20.0F, service.snapshot().reconciliation.warning_tolerance_grams);

  const auto exported = service.export_json(false);
  TEST_ASSERT_TRUE(exported.ok());
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, exported.value().find("future_section"));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, exported.value().find("preserve_me"));
}

void test_schema_two_migrates_identity_defaults_and_preserves_unknown_fields() {
  MemoryDocumentStore documents;
  documents.document = schema_two_document;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);

  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.status().migrated);
  TEST_ASSERT_EQUAL_UINT32(2U, service.status().loaded_schema);
  TEST_ASSERT_EQUAL_STRING("legacy_uuid", service.snapshot().spoolman.identity_field.c_str());
  TEST_ASSERT_EQUAL_STRING("nfc_uid", service.snapshot().spoolman.nfc_uid_field.c_str());
  TEST_ASSERT_EQUAL_UINT(0U, service.snapshot().spool_identity_mappings.size());
  const auto exported = service.export_json(false);
  TEST_ASSERT_TRUE(exported.ok());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, exported.value().find("future_section"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, exported.value().find("preserve_me"));
}

void test_corrupt_primary_recovers_valid_backup_transactionally() {
  MemoryDocumentStore documents;
  documents.document = "{corrupt";
  documents.backup_document = schema_one_document;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);

  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_TRUE(service.status().recovered_from_backup);
  TEST_ASSERT_TRUE(service.status().migrated);
  TEST_ASSERT_EQUAL_STRING(
      "legacy-station", service.snapshot().device.hostname.c_str());
  TEST_ASSERT_EQUAL_UINT(1U, documents.save_count);
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, documents.document->find("legacy-station"));
}

void test_invalid_import_is_transactional_and_wrong_hardware_is_rejected() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  const auto original = *documents.document;

  TEST_ASSERT_FALSE(service.import_json("{bad json", false).ok());
  TEST_ASSERT_EQUAL_STRING(original.c_str(), documents.document->c_str());

  std::string wrong_hardware = *documents.document;
  const auto position = wrong_hardware.find("wt32-sc01-plus-rev-a");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, position);
  wrong_hardware.replace(position, 21U, "different-hardware");
  TEST_ASSERT_FALSE(service.import_json(wrong_hardware, true).ok());
  TEST_ASSERT_EQUAL_STRING(original.c_str(), documents.document->c_str());
}

void test_default_export_redacts_all_credentials_and_ssid() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  auto configured = service.snapshot();
  configured.wifi.ssid = "private-network";
  configured.wifi.password = "wifi-secret";
  configured.spoolman.authentication_token = "spoolman-secret";
  configured.spoolman.ca_certificate_pem = "private-ca";
  configured.filabridge.authentication_token = "filabridge-secret";
  configured.filabridge.ca_certificate_pem = "private-filabridge-ca";
  configured.web.access_token = "local-api-secret-token";
  TEST_ASSERT_TRUE(service.replace(configured).ok());

  const auto redacted = service.export_json(false);
  TEST_ASSERT_TRUE(redacted.ok());
  TEST_ASSERT_EQUAL(std::string::npos, redacted.value().find("private-network"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.value().find("wifi-secret"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.value().find("spoolman-secret"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.value().find("filabridge-secret"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.value().find("private-ca"));
  TEST_ASSERT_EQUAL(
      std::string::npos, redacted.value().find("private-filabridge-ca"));
  TEST_ASSERT_EQUAL(
      std::string::npos, redacted.value().find("local-api-secret-token"));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, redacted.value().find("credentials_configured"));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, redacted.value().find("access_token_configured"));

  const auto complete = service.export_json(true);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, complete.value().find("private-network"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, complete.value().find("wifi-secret"));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, complete.value().find("local-api-secret-token"));
}

void test_noncredential_import_preserves_current_secrets_and_network() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  auto configured = service.snapshot();
  configured.wifi.ssid = "existing-network";
  configured.wifi.password = "existing-password";
  configured.spoolman.authentication_token = "existing-token";
  configured.web.access_token = "existing-local-api-token";
  TEST_ASSERT_TRUE(service.replace(configured).ok());

  auto imported = configured;
  imported.device.hostname = "imported-station";
  imported.wifi.ssid = "untrusted-network";
  imported.wifi.password = "untrusted-password";
  imported.spoolman.authentication_token = "untrusted-token";
  imported.web.access_token = "untrusted-local-api-token";
  MemoryDocumentStore source;
  LegacyScaleStore source_legacy;
  ConfigurationService source_service(source, source_legacy);
  TEST_ASSERT_TRUE(source_service.initialize().ok());
  TEST_ASSERT_TRUE(source_service.replace(imported).ok());
  const auto payload = source_service.export_json(true);

  TEST_ASSERT_TRUE(service.import_json(payload.value(), false).ok());
  const auto result = service.snapshot();
  TEST_ASSERT_EQUAL_STRING("imported-station", result.device.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("existing-network", result.wifi.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("existing-password", result.wifi.password.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "existing-token", result.spoolman.authentication_token.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "existing-local-api-token", result.web.access_token.c_str());
}

void test_failed_save_does_not_replace_live_configuration() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  const auto initial_revision = service.revision();
  auto changed = service.snapshot();
  changed.device.hostname = "should-not-stick";
  documents.save_fails = true;
  TEST_ASSERT_FALSE(service.replace(changed).ok());
  TEST_ASSERT_EQUAL_STRING("opentag-station", service.snapshot().device.hostname.c_str());
  TEST_ASSERT_FALSE(service.status().persistence_available);
  TEST_ASSERT_EQUAL_UINT64(initial_revision, service.revision());
}

void test_revision_is_consistent_and_advances_once_per_successful_commit() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);

  TEST_ASSERT_EQUAL_UINT64(0U, service.revision());
  TEST_ASSERT_EQUAL_UINT64(0U, service.status().revision);
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_EQUAL_UINT64(1U, service.revision());
  TEST_ASSERT_EQUAL_UINT64(1U, service.status().revision);

  const auto initial = service.versioned_snapshot();
  TEST_ASSERT_EQUAL_UINT64(1U, initial.revision);
  TEST_ASSERT_EQUAL_STRING(
      "opentag-station", initial.configuration.device.hostname.c_str());

  auto changed = initial.configuration;
  changed.device.hostname = "revision-two";
  TEST_ASSERT_TRUE(
      service.replace_if_revision(changed, initial.revision).ok());
  TEST_ASSERT_EQUAL_UINT64(2U, service.revision());
  TEST_ASSERT_EQUAL_UINT64(2U, service.versioned_snapshot().revision);
  TEST_ASSERT_EQUAL_STRING(
      "revision-two", service.snapshot().device.hostname.c_str());

  changed.device.hostname = "revision-three";
  TEST_ASSERT_TRUE(service.replace(changed).ok());
  TEST_ASSERT_EQUAL_UINT64(3U, service.revision());
  TEST_ASSERT_EQUAL_UINT64(3U, service.status().revision);
}

void test_conditional_replace_rejects_stale_revision_transactionally() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());

  const auto stale = service.versioned_snapshot();
  auto intervening = stale.configuration;
  intervening.device.hostname = "newer-configuration";
  TEST_ASSERT_TRUE(
      service.replace_if_revision(intervening, stale.revision).ok());
  const auto current_revision = service.revision();
  const auto save_count = documents.save_count;

  auto obsolete = stale.configuration;
  obsolete.device.hostname = "stale-configuration";
  const auto rejected =
      service.replace_if_revision(obsolete, stale.revision);
  TEST_ASSERT_FALSE(rejected.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::conflict),
      static_cast<int>(rejected.error().category));
  TEST_ASSERT_TRUE(rejected.error().retryable);
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, rejected.error().message.find("revision conflict"));
  TEST_ASSERT_EQUAL_UINT64(current_revision, service.revision());
  TEST_ASSERT_EQUAL_UINT(save_count, documents.save_count);
  TEST_ASSERT_EQUAL_STRING(
      "newer-configuration", service.snapshot().device.hostname.c_str());
}

void test_revision_does_not_advance_for_validation_or_storage_failure() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  const auto initial = service.versioned_snapshot();

  auto invalid = initial.configuration;
  invalid.device.hostname = "-invalid";
  TEST_ASSERT_FALSE(
      service.replace_if_revision(invalid, initial.revision).ok());
  TEST_ASSERT_EQUAL_UINT64(initial.revision, service.revision());

  auto valid = initial.configuration;
  valid.device.hostname = "cannot-persist";
  documents.save_fails = true;
  TEST_ASSERT_FALSE(
      service.replace_if_revision(valid, initial.revision).ok());
  TEST_ASSERT_EQUAL_UINT64(initial.revision, service.revision());
  TEST_ASSERT_EQUAL_STRING(
      "opentag-station", service.snapshot().device.hostname.c_str());
}

void test_other_persisted_mutations_invalidate_older_snapshots() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  const auto stale = service.versioned_snapshot();

  TEST_ASSERT_TRUE(
      service.save_scale_calibration(valid_scale(5000.0F)).ok());
  TEST_ASSERT_EQUAL_UINT64(stale.revision + 1U, service.revision());

  auto obsolete = stale.configuration;
  obsolete.device.hostname = "would-drop-calibration";
  TEST_ASSERT_FALSE(
      service.replace_if_revision(obsolete, stale.revision).ok());
  TEST_ASSERT_EQUAL_UINT64(stale.revision + 1U, service.revision());
  TEST_ASSERT_TRUE(service.snapshot().scale_calibration.has_value());
}

void test_initialize_revision_changes_only_on_successful_load() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);

  documents.load_fails = true;
  TEST_ASSERT_FALSE(service.initialize().ok());
  TEST_ASSERT_EQUAL_UINT64(0U, service.revision());

  documents.load_fails = false;
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_EQUAL_UINT64(1U, service.revision());
  TEST_ASSERT_TRUE(service.initialize().ok());
  TEST_ASSERT_EQUAL_UINT64(2U, service.revision());
}

void test_validation_rejects_invalid_urls_and_duplicate_toolheads() {
  Configuration configuration;
  configuration.spoolman.url = "ftp://not-supported";
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.spoolman.url = "http://user@example.com";
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.spoolman.url = "https://example.com:0";
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.spoolman.url.clear();
  configuration.toolheads = {
      {0, "T1", 0.4F, true},
      {0, "Duplicate", 0.6F, true},
  };
  TEST_ASSERT_FALSE(configuration.validate().ok());
}

void test_validation_rejects_invalid_scale_hardware_settings() {
  Configuration configuration;
  configuration.scale_hardware.load_cell_model = "unknown";
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.scale_hardware = {};
  configuration.scale_hardware.rated_capacity_grams = 0.0F;
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.scale_hardware = {};
  configuration.scale_hardware.overload_ratio = 2.01F;
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.scale_hardware = {};
  configuration.scale_hardware.rated_capacity_grams = 2000.0F;
  TEST_ASSERT_TRUE(configuration.validate().ok());
  configuration.scale_hardware.rated_capacity_grams = 3000.0F;
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.scale_hardware.rated_capacity_grams = 5000.0F;
  TEST_ASSERT_TRUE(configuration.validate().ok());
}

void test_web_access_token_validation_is_fail_closed_and_ascii_only() {
  Configuration configuration;
  TEST_ASSERT_TRUE(configuration.validate().ok());

  configuration.web.access_token = std::string(15U, 'A');
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.web.access_token = std::string(16U, 'A');
  TEST_ASSERT_TRUE(configuration.validate().ok());
  configuration.web.access_token = std::string(128U, 'z');
  TEST_ASSERT_TRUE(configuration.validate().ok());
  configuration.web.access_token = std::string(129U, 'z');
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.web.access_token = "ValidTokenChars-._~";
  TEST_ASSERT_TRUE(configuration.validate().ok());
  configuration.web.access_token = "invalid token with spaces";
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.web.access_token = "invalid:token:characters";
  TEST_ASSERT_FALSE(configuration.validate().ok());
  configuration.web.access_token =
      std::string("NonAsciiToken-123") + static_cast<char>(0xC3);
  TEST_ASSERT_FALSE(configuration.validate().ok());
}

void test_local_interface_snapshot_is_narrow_current_and_independent() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService service(documents, legacy);
  TEST_ASSERT_TRUE(service.initialize().ok());
  const auto initial = service.local_interface_settings_snapshot();
  TEST_ASSERT_EQUAL_STRING("opentag-station", initial.hostname.c_str());
  TEST_ASSERT_TRUE(initial.web.access_token.empty());
  TEST_ASSERT_EQUAL_UINT64(service.revision(), initial.revision);

  auto configured = service.snapshot();
  configured.device.hostname = "low-heap-station";
  configured.spoolman.ca_certificate_pem = std::string(4096U, 'S');
  configured.filabridge.ca_certificate_pem = std::string(4096U, 'F');
  configured.web.access_token = "LocalApiToken-0123456789";
  TEST_ASSERT_TRUE(service.replace(configured).ok());

  auto local = service.local_interface_settings_snapshot();
  TEST_ASSERT_EQUAL_STRING("low-heap-station", local.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "LocalApiToken-0123456789", local.web.access_token.c_str());
  TEST_ASSERT_EQUAL_UINT64(service.revision(), local.revision);
  local.hostname.clear();
  local.web.access_token.clear();

  const auto current = service.local_interface_settings_snapshot();
  TEST_ASSERT_EQUAL_STRING("low-heap-station", current.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "LocalApiToken-0123456789", current.web.access_token.c_str());
}

void test_complete_toolhead_profile_round_trips() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService writer(documents, legacy);
  TEST_ASSERT_TRUE(writer.initialize().ok());
  auto configured = writer.snapshot();
  configured.toolheads = {
      {2, "T3", 0.6F, true, "ObXidian", 300U, "Abrasive filament"},
  };
  TEST_ASSERT_TRUE(writer.replace(configured).ok());

  ConfigurationService reader(documents, legacy);
  TEST_ASSERT_TRUE(reader.initialize().ok());
  const auto restored = reader.snapshot();
  TEST_ASSERT_EQUAL_UINT(1U, restored.toolheads.size());
  const auto& profile = restored.toolheads.front();
  TEST_ASSERT_EQUAL_STRING("ObXidian", profile.nozzle_material.c_str());
  TEST_ASSERT_EQUAL_UINT16(300U, profile.maximum_temperature_c);
  TEST_ASSERT_EQUAL_STRING("Abrasive filament", profile.notes.c_str());
}

void test_confirmed_spool_mapping_round_trips_and_conflicts_are_rejected() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService writer(documents, legacy);
  TEST_ASSERT_TRUE(writer.initialize().ok());
  opentag::domain::ConfirmedSpoolMapping mapping;
  mapping.spool_id = 17;
  mapping.instance_uuid = "00112233-4455-6677-8899-aabbccddeeff";
  mapping.nfc_uid = "E004010203040506";
  TEST_ASSERT_TRUE(writer.confirm_spool_identity_mapping(mapping).ok());

  ConfigurationService reader(documents, legacy);
  TEST_ASSERT_TRUE(reader.initialize().ok());
  const auto restored = reader.load_spool_identity_mappings();
  TEST_ASSERT_TRUE(restored.ok());
  TEST_ASSERT_EQUAL_UINT(1U, restored.value().size());
  TEST_ASSERT_EQUAL_INT32(17, restored.value().front().spool_id);
  TEST_ASSERT_EQUAL_STRING(
      "E004010203040506", restored.value().front().nfc_uid->c_str());

  mapping.spool_id = 18;
  TEST_ASSERT_FALSE(reader.confirm_spool_identity_mapping(mapping).ok());
  TEST_ASSERT_EQUAL_UINT(
      1U, reader.load_spool_identity_mappings().value().size());
}

void test_first_run_navigation_allows_tokenless_setup_completion() {
  MemoryDocumentStore documents;
  LegacyScaleStore legacy;
  ConfigurationService configuration(documents, legacy);
  TEST_ASSERT_TRUE(configuration.initialize().ok());
  TEST_ASSERT_TRUE(configuration.snapshot().web.access_token.empty());
  FirstRunSetup setup(configuration);

  TEST_ASSERT_TRUE(setup.go_to(SetupStep::scale_calibration).ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetupStep::scale_calibration),
      static_cast<int>(setup.current()));
  TEST_ASSERT_FALSE(setup.complete());
  TEST_ASSERT_TRUE(setup.mark_complete(SetupStep::wifi).ok());
  TEST_ASSERT_TRUE(setup.step_complete(SetupStep::wifi));
  TEST_ASSERT_TRUE(setup.go_to(SetupStep::ready).ok());
  TEST_ASSERT_TRUE(setup.mark_complete(SetupStep::ready).ok());
  TEST_ASSERT_TRUE(setup.complete());
  TEST_ASSERT_TRUE(configuration.snapshot().web.access_token.empty());
  TEST_ASSERT_TRUE(configuration.snapshot().validate().ok());
  TEST_ASSERT_FALSE(setup.next().ok());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_missing_document_creates_current_schema_defaults);
  RUN_TEST(test_legacy_scale_calibration_is_migrated_and_mirrored);
  RUN_TEST(test_legacy_save_failure_does_not_commit_central_calibration);
  RUN_TEST(test_schema_three_2kg_calibration_infers_missing_hardware_profile);
  RUN_TEST(test_schema_three_web_token_round_trips_and_preserves_unknown_fields);
  RUN_TEST(test_uncalibrated_2kg_profile_round_trips_separately_from_calibration);
  RUN_TEST(test_5kg_profile_and_calibration_round_trip_and_mismatch_is_rejected);
  RUN_TEST(test_profile_change_clears_legacy_calibration_before_document_commit);
  RUN_TEST(test_profile_change_clears_stale_legacy_when_central_calibration_is_absent);
  RUN_TEST(test_legacy_clear_failure_aborts_profile_change_transactionally);
  RUN_TEST(test_schema_one_migrates_additively_and_preserves_unknown_fields);
  RUN_TEST(test_schema_two_migrates_identity_defaults_and_preserves_unknown_fields);
  RUN_TEST(test_corrupt_primary_recovers_valid_backup_transactionally);
  RUN_TEST(test_invalid_import_is_transactional_and_wrong_hardware_is_rejected);
  RUN_TEST(test_default_export_redacts_all_credentials_and_ssid);
  RUN_TEST(test_noncredential_import_preserves_current_secrets_and_network);
  RUN_TEST(test_failed_save_does_not_replace_live_configuration);
  RUN_TEST(test_revision_is_consistent_and_advances_once_per_successful_commit);
  RUN_TEST(test_conditional_replace_rejects_stale_revision_transactionally);
  RUN_TEST(test_revision_does_not_advance_for_validation_or_storage_failure);
  RUN_TEST(test_other_persisted_mutations_invalidate_older_snapshots);
  RUN_TEST(test_initialize_revision_changes_only_on_successful_load);
  RUN_TEST(test_validation_rejects_invalid_urls_and_duplicate_toolheads);
  RUN_TEST(test_validation_rejects_invalid_scale_hardware_settings);
  RUN_TEST(test_web_access_token_validation_is_fail_closed_and_ascii_only);
  RUN_TEST(test_local_interface_snapshot_is_narrow_current_and_independent);
  RUN_TEST(test_complete_toolhead_profile_round_trips);
  RUN_TEST(test_confirmed_spool_mapping_round_trips_and_conflicts_are_rejected);
  RUN_TEST(test_first_run_navigation_allows_tokenless_setup_completion);
  return UNITY_END();
}

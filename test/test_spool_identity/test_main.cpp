#include <unity.h>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "domain/spool.hpp"
#include "domain/spool_identity.hpp"
#include "integrations/inventory.hpp"
#include "services/spool_identity_resolver.hpp"
#include "services/spool_identity_store.hpp"

namespace {

using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::domain::ConfirmedSpoolMapping;
using opentag::domain::Spool;
using opentag::domain::SpoolIdentity;
using opentag::integrations::BackendCapabilities;
using opentag::integrations::CreateSpoolRequest;
using opentag::integrations::ExtraFieldDefinition;
using opentag::integrations::ISpoolInventory;
using opentag::integrations::RemainingWeightUpdate;
using opentag::integrations::SpoolFilter;
using opentag::services::ISpoolIdentityMappingStore;
using opentag::services::SpoolIdentityResolver;
using opentag::services::SpoolMatchSource;
using opentag::services::SpoolResolutionStatus;

Result<std::vector<Spool>> spool_result(std::vector<Spool> value) {
  return Result<std::vector<Spool>>::success(std::move(value));
}

Spool spool(std::int32_t id, const std::string& vendor = "Prusament") {
  Spool value;
  value.id = id;
  value.filament_id = id + 100;
  value.display_name = vendor + " PETG Orange";
  value.vendor = vendor;
  value.material = "PETG";
  value.subtype = "Orange";
  return value;
}

class FakeInventory final : public ISpoolInventory {
 public:
  std::deque<Result<std::vector<Spool>>> find_results;
  std::vector<SpoolFilter> filters;
  std::map<std::int32_t, Spool> by_id;

  Result<std::vector<Spool>> list_spools() override {
    return find_spools({});
  }
  Result<std::vector<Spool>> find_spools(const SpoolFilter& filter) override {
    filters.push_back(filter);
    TEST_ASSERT_FALSE_MESSAGE(find_results.empty(), "unexpected spool search");
    if (find_results.empty()) {
      return Result<std::vector<Spool>>::failure(
          {ErrorCategory::network, "unexpected search", false});
    }
    auto result = std::move(find_results.front());
    find_results.pop_front();
    return result;
  }
  Result<Spool> get_spool(opentag::domain::SpoolId id) override {
    const auto found = by_id.find(id);
    if (found == by_id.end()) {
      return Result<Spool>::failure(
          {ErrorCategory::invalid_response, "unknown spool", false});
    }
    return Result<Spool>::success(found->second);
  }
  Result<Spool> create_spool(const CreateSpoolRequest&) override {
    return Result<Spool>::failure(
        {ErrorCategory::api_changed, "unused", false});
  }
  Result<Spool> set_remaining_weight(
      opentag::domain::SpoolId,
      const RemainingWeightUpdate&) override {
    return Result<Spool>::failure(
        {ErrorCategory::api_changed, "unused", false});
  }
  Result<std::vector<std::string>> list_locations() override {
    return Result<std::vector<std::string>>::success({});
  }
  Result<std::vector<ExtraFieldDefinition>> list_extra_fields() override {
    return Result<std::vector<ExtraFieldDefinition>>::success({});
  }
  BackendCapabilities capabilities() const override { return {}; }
};

class MemoryMappings final : public ISpoolIdentityMappingStore {
 public:
  std::vector<ConfirmedSpoolMapping> values;
  std::optional<ConfirmedSpoolMapping> confirmed;

  Result<std::vector<ConfirmedSpoolMapping>> load_spool_identity_mappings() override {
    return Result<std::vector<ConfirmedSpoolMapping>>::success(values);
  }
  Result<void> confirm_spool_identity_mapping(
      const ConfirmedSpoolMapping& mapping) override {
    confirmed = mapping;
    return Result<void>::success();
  }
};

opentag::config::SpoolmanSettings settings() {
  opentag::config::SpoolmanSettings result;
  result.identity_field = "station_uuid";
  result.nfc_uid_field = "tag_uid";
  return result;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_configured_instance_uuid_is_first_and_unique_match_wins() {
  FakeInventory inventory;
  MemoryMappings mappings;
  inventory.find_results.push_back(spool_result({spool(17)}));
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.instance_uuid = "00112233-4455-6677-8899-aabbccddeeff";
  identity.nfc_uid = "E004010203040506";

  const auto result = resolver.resolve(identity);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolResolutionStatus::matched),
      static_cast<int>(result.value().status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolMatchSource::configured_identity_field),
      static_cast<int>(result.value().source));
  TEST_ASSERT_EQUAL_INT32(17, result.value().match()->id);
  TEST_ASSERT_EQUAL_UINT(1U, inventory.filters.size());
  TEST_ASSERT_EQUAL_STRING(
      R"json("00112233-4455-6677-8899-aabbccddeeff")json",
      inventory.filters[0].extra_json.at("station_uuid").c_str());
}

void test_duplicate_instance_uuid_is_reported_as_conflict() {
  FakeInventory inventory;
  MemoryMappings mappings;
  inventory.find_results.push_back(spool_result({spool(17), spool(18)}));
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.instance_uuid = "duplicate";

  const auto result = resolver.resolve(identity);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolResolutionStatus::conflict),
      static_cast<int>(result.value().status));
  TEST_ASSERT_EQUAL_UINT(2U, result.value().candidates.size());
}

void test_confirmed_instance_cache_precedes_nfc_and_metadata() {
  FakeInventory inventory;
  MemoryMappings mappings;
  inventory.find_results.push_back(spool_result({}));
  mappings.values.push_back({17, "cached-uuid", "E004010203040506"});
  inventory.by_id.emplace(17, spool(17));
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.instance_uuid = "cached-uuid";
  identity.nfc_uid = "E004010203040506";

  const auto result = resolver.resolve(identity);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolMatchSource::confirmed_identity_cache),
      static_cast<int>(result.value().source));
  TEST_ASSERT_EQUAL_INT32(17, result.value().match()->id);
  TEST_ASSERT_EQUAL_UINT(1U, inventory.filters.size());
}

void test_nfc_uid_mapping_follows_missing_instance_sources() {
  FakeInventory inventory;
  MemoryMappings mappings;
  inventory.find_results.push_back(spool_result({}));
  inventory.find_results.push_back(spool_result({spool(22)}));
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.instance_uuid = "not-found";
  identity.nfc_uid = "E004010203040506";

  const auto result = resolver.resolve(identity);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolMatchSource::nfc_uid),
      static_cast<int>(result.value().source));
  TEST_ASSERT_EQUAL_INT32(22, result.value().match()->id);
  TEST_ASSERT_EQUAL_STRING(
      R"json("E004010203040506")json",
      inventory.filters[1].extra_json.at("tag_uid").c_str());
}

void test_gtin_exact_match_precedes_metadata_candidates() {
  FakeInventory inventory;
  MemoryMappings mappings;
  auto first = spool(31);
  first.article_number = "8594173675001";
  auto second = spool(32);
  second.article_number = "different";
  inventory.find_results.push_back(spool_result({first, second}));
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.gtin = 8594173675001ULL;
  identity.brand_name = "Prusament";
  identity.material_abbreviation = "PETG";

  const auto result = resolver.resolve(identity);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolMatchSource::package_or_material_identity),
      static_cast<int>(result.value().source));
  TEST_ASSERT_EQUAL_INT32(31, result.value().match()->id);
}

void test_multiple_metadata_candidates_require_manual_selection() {
  FakeInventory inventory;
  MemoryMappings mappings;
  inventory.find_results.push_back(spool_result({spool(41), spool(42), spool(43)}));
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.brand_name = "Prusament";
  identity.material_abbreviation = "PETG";

  const auto result = resolver.resolve(identity);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(SpoolResolutionStatus::ambiguous),
      static_cast<int>(result.value().status));
  TEST_ASSERT_EQUAL_UINT(3U, result.value().candidates.size());
}

void test_confirmation_persists_both_stable_identities() {
  FakeInventory inventory;
  MemoryMappings mappings;
  SpoolIdentityResolver resolver(inventory, mappings, settings());
  SpoolIdentity identity;
  identity.instance_uuid = "stable-uuid";
  identity.nfc_uid = "E004010203040506";

  const auto result = resolver.confirm(identity, 17);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_TRUE(mappings.confirmed.has_value());
  TEST_ASSERT_EQUAL_INT32(17, mappings.confirmed->spool_id);
  TEST_ASSERT_EQUAL_STRING(
      "stable-uuid", mappings.confirmed->instance_uuid->c_str());
}

void test_openprinttag_uuid_and_nfc_uid_are_normalized() {
  opentag::nfc::openprinttag::MaterialRecord material;
  material.instance_uuid = std::array<std::uint8_t, 16>{
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  opentag::nfc::nfcv::Uid uid;
  uid.bytes = {0xE0, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

  const auto identity = opentag::services::identity_from_openprinttag(material, uid);

  TEST_ASSERT_EQUAL_STRING(
      "00112233-4455-6677-8899-aabbccddeeff",
      identity.instance_uuid->c_str());
  TEST_ASSERT_EQUAL_STRING("E004010203040506", identity.nfc_uid->c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_configured_instance_uuid_is_first_and_unique_match_wins);
  RUN_TEST(test_duplicate_instance_uuid_is_reported_as_conflict);
  RUN_TEST(test_confirmed_instance_cache_precedes_nfc_and_metadata);
  RUN_TEST(test_nfc_uid_mapping_follows_missing_instance_sources);
  RUN_TEST(test_gtin_exact_match_precedes_metadata_candidates);
  RUN_TEST(test_multiple_metadata_candidates_require_manual_selection);
  RUN_TEST(test_confirmation_persists_both_stable_identities);
  RUN_TEST(test_openprinttag_uuid_and_nfc_uid_are_normalized);
  return UNITY_END();
}

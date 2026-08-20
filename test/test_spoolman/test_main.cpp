#include <unity.h>

#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "integrations/backend_capabilities.hpp"
#include "integrations/spoolman/spoolman_adapter.hpp"
#include "network/http_transport.hpp"

namespace {

using opentag::config::SpoolmanSettings;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::integrations::BackendCapability;
using opentag::integrations::RemainingWeightUpdate;
using opentag::integrations::SpoolFilter;
using opentag::integrations::spoolman::SpoolmanAdapter;
using opentag::network::HttpRequest;
using opentag::network::HttpResponse;
using opentag::network::IHttpTransport;

struct ExpectedRequest {
  std::string method;
  std::string url;
  std::optional<std::string> exact_body;
  Result<HttpResponse> response;
};

class ScriptedTransport final : public IHttpTransport {
 public:
  std::deque<ExpectedRequest> requests;

  Result<HttpResponse> perform(const HttpRequest& request) override {
    TEST_ASSERT_FALSE_MESSAGE(requests.empty(), "unexpected HTTP request");
    if (requests.empty()) {
      return Result<HttpResponse>::failure(
          {ErrorCategory::network, "unexpected request", false});
    }
    auto expected = std::move(requests.front());
    requests.pop_front();
    TEST_ASSERT_EQUAL_STRING(expected.method.c_str(), request.method.c_str());
    TEST_ASSERT_EQUAL_STRING(expected.url.c_str(), request.url.c_str());
    if (expected.exact_body.has_value()) {
      TEST_ASSERT_EQUAL_STRING(expected.exact_body->c_str(), request.body.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(5000U, request.connect_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(7000U, request.read_timeout_ms);
    return expected.response;
  }

  void expect(
      const std::string& method,
      const std::string& path,
      const std::string& body,
      std::int32_t status = 200) {
    requests.push_back({
        method,
        "http://spoolman.local/api/v1" + path,
        std::nullopt,
        Result<HttpResponse>::success({status, body, "application/json"}),
    });
  }

  void expect_body(
      const std::string& method,
      const std::string& path,
      const std::string& request_body,
      const std::string& response_body,
      std::int32_t status = 200) {
    requests.push_back({
        method,
        "http://spoolman.local/api/v1" + path,
        request_body,
        Result<HttpResponse>::success(
            {status, response_body, "application/json"}),
    });
  }
};

SpoolmanSettings settings() {
  SpoolmanSettings value;
  value.url = "http://spoolman.local/";
  value.authentication_token = "test-token";
  value.identity_field = "opentag_instance_uuid";
  return value;
}

std::string spool_json(
    float used = 250.0F,
    float remaining = 750.0F,
    const std::string& extra =
        R"json({"opentag_instance_uuid":"\"uuid-123\"","nfc_uid":"\"E00401AA\"","owner":"\"alice\""})json") {
  return std::string(R"json({
    "id":17,
    "remaining_weight":)json") + std::to_string(remaining) + R"json(,
    "initial_weight":1000.0,
    "spool_weight":210.0,
    "used_weight":)json" + std::to_string(used) + R"json(,
    "location":"Drybox A",
    "archived":false,
    "extra":)json" + extra + R"json(,
    "filament":{
      "id":9,
      "name":"Galaxy Black",
      "material":"PLA",
      "article_number":"GTIN-123",
      "color_hex":"10203080",
      "spool_weight":205.0,
      "vendor":{"name":"Prusament","empty_spool_weight":200.0}
    }
  })json";
}

void script_probe(
    ScriptedTransport& transport,
    const std::string& version = "0.26.1") {
  transport.expect("GET", "/health", R"json({"status":"healthy"})json");
  transport.expect(
      "GET", "/info",
      std::string(R"json({"version":")json") + version +
          R"json(","git_commit":"8d9eb739"})json");
  transport.expect(
      "GET", "/spool?allow_archived=false&limit=1&offset=0", "[]");
  transport.expect("GET", "/location", "[]");
  transport.expect("GET", "/field/spool", "[]");
}

void assert_transport_consumed(const ScriptedTransport& transport) {
  TEST_ASSERT_TRUE_MESSAGE(transport.requests.empty(), "expected request was not sent");
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_probe_known_version_enables_proven_reads_and_guarded_writes() {
  ScriptedTransport transport;
  script_probe(transport);
  SpoolmanAdapter adapter(transport, settings());

  const auto result = adapter.probe();

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_TRUE(result.value().connected);
  TEST_ASSERT_TRUE(result.value().healthy);
  TEST_ASSERT_TRUE(result.value().version_formally_tested);
  TEST_ASSERT_EQUAL_STRING("0.26.1", result.value().version.c_str());
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::health));
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::runtime_version));
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::list_spools));
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::search_spools));
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::list_locations));
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::list_extra_fields));
  TEST_ASSERT_TRUE(result.value().capabilities.has(BackendCapability::create_spool));
  TEST_ASSERT_TRUE(result.value().capabilities.has(
      BackendCapability::update_remaining_weight));
  assert_transport_consumed(transport);
}

void test_unknown_version_stays_connected_but_write_capabilities_are_off() {
  ScriptedTransport transport;
  script_probe(transport, "0.27.0");
  SpoolmanAdapter adapter(transport, settings());

  const auto status = adapter.probe();
  TEST_ASSERT_TRUE(status.ok());
  TEST_ASSERT_TRUE(status.value().connected);
  TEST_ASSERT_FALSE(status.value().version_formally_tested);
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::list_spools));
  TEST_ASSERT_FALSE(status.value().capabilities.has(BackendCapability::create_spool));

  opentag::integrations::CreateSpoolRequest create;
  create.filament_id = 1;
  const auto rejected = adapter.create_spool(create);
  TEST_ASSERT_FALSE(rejected.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::api_changed),
      static_cast<int>(rejected.error().category));
  assert_transport_consumed(transport);
}

void test_spool_response_is_normalized_and_identity_values_are_decoded() {
  ScriptedTransport transport;
  transport.expect(
      "GET", "/spool?allow_archived=false&filament.material=PLA%20Plus&extra.opentag_instance_uuid=%22uuid-123%22&limit=2&offset=0",
      "[" + spool_json() + "]");
  SpoolmanAdapter adapter(transport, settings());
  SpoolFilter filter;
  filter.material = "PLA Plus";
  filter.extra_json["opentag_instance_uuid"] = R"json("uuid-123")json";
  filter.maximum_results = 2U;

  const auto result = adapter.find_spools(filter);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, result.value().size());
  const auto& spool = result.value().front();
  TEST_ASSERT_EQUAL_INT32(17, spool.id);
  TEST_ASSERT_EQUAL_INT32(9, spool.filament_id);
  TEST_ASSERT_EQUAL_STRING("Prusament Galaxy Black", spool.display_name.c_str());
  TEST_ASSERT_EQUAL_STRING("PLA", spool.material.c_str());
  TEST_ASSERT_TRUE(spool.remaining_grams.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 750.0F, *spool.remaining_grams);
  TEST_ASSERT_TRUE(spool.openprinttag_instance_uuid.has_value());
  TEST_ASSERT_EQUAL_STRING("uuid-123", spool.openprinttag_instance_uuid->c_str());
  TEST_ASSERT_TRUE(spool.nfc_uid.has_value());
  TEST_ASSERT_EQUAL_STRING("E00401AA", spool.nfc_uid->c_str());
  TEST_ASSERT_TRUE(spool.primary_color.has_value());
  TEST_ASSERT_EQUAL_UINT8(0x10U, spool.primary_color->red);
  TEST_ASSERT_EQUAL_UINT8(0x80U, spool.primary_color->alpha);
  assert_transport_consumed(transport);
}

void test_malformed_spool_contract_is_rejected_as_api_change() {
  ScriptedTransport transport;
  transport.expect("GET", "/spool/17", R"json({"id":17,"filament":{}})json");
  SpoolmanAdapter adapter(transport, settings());

  const auto result = adapter.get_spool(17);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::api_changed),
      static_cast<int>(result.error().category));
  assert_transport_consumed(transport);
}

void test_remaining_weight_update_checks_concurrency_and_verifies_readback() {
  ScriptedTransport transport;
  script_probe(transport);
  transport.expect("GET", "/spool/17", spool_json());
  transport.expect_body(
      "PATCH", "/spool/17", R"json({"remaining_weight":700})json", spool_json());
  transport.expect("GET", "/spool/17", spool_json(300.0F, 700.0F));
  SpoolmanAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());
  RemainingWeightUpdate update;
  update.expected_used_grams = 250.0F;
  update.remaining_grams = 700.0F;

  const auto result = adapter.set_remaining_weight(17, update);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 700.0F, *result.value().remaining_grams);
  assert_transport_consumed(transport);
}

void test_remaining_weight_update_aborts_if_usage_changed() {
  ScriptedTransport transport;
  script_probe(transport);
  transport.expect("GET", "/spool/17", spool_json(260.0F));
  SpoolmanAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());
  RemainingWeightUpdate update;
  update.expected_used_grams = 250.0F;
  update.remaining_grams = 700.0F;

  const auto result = adapter.set_remaining_weight(17, update);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, result.error().message.find("usage changed"));
  assert_transport_consumed(transport);
}

void test_remaining_weight_update_rejects_mismatched_readback() {
  ScriptedTransport transport;
  script_probe(transport);
  transport.expect("GET", "/spool/17", spool_json());
  transport.expect_body(
      "PATCH", "/spool/17", R"json({"remaining_weight":700})json", spool_json());
  transport.expect("GET", "/spool/17", spool_json(250.0F, 710.0F));
  SpoolmanAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());
  RemainingWeightUpdate update;
  update.expected_used_grams = 250.0F;
  update.remaining_grams = 700.0F;

  const auto result = adapter.set_remaining_weight(17, update);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, result.error().message.find("readback did not match"));
  assert_transport_consumed(transport);
}

void test_explicit_create_spool_serializes_identity_without_implicit_creation() {
  ScriptedTransport transport;
  script_probe(transport);
  transport.expect_body(
      "POST", "/spool",
      R"json({"filament_id":9,"initial_weight":1000,"remaining_weight":750,"spool_weight":210,"location":"Drybox A","extra":{"opentag_instance_uuid":"\"uuid-123\""}})json",
      spool_json());
  SpoolmanAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());
  opentag::integrations::CreateSpoolRequest request;
  request.filament_id = 9;
  request.initial_grams = 1000.0F;
  request.remaining_grams = 750.0F;
  request.empty_spool_grams = 210.0F;
  request.location = "Drybox A";
  request.extra_json["opentag_instance_uuid"] = R"json("uuid-123")json";

  const auto result = adapter.create_spool(request);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_INT32(17, result.value().id);
  assert_transport_consumed(transport);
}

void test_extra_field_patch_contains_only_intended_key_and_is_verified() {
  ScriptedTransport transport;
  script_probe(transport);
  const std::string changed_extra =
      R"json({"opentag_instance_uuid":"\"uuid-new\"","owner":"\"alice\""})json";
  transport.expect_body(
      "PATCH", "/spool/17",
      R"json({"extra":{"opentag_instance_uuid":"\"uuid-new\""}})json",
      spool_json());
  transport.expect("GET", "/spool/17", spool_json(250.0F, 750.0F, changed_extra));
  SpoolmanAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());

  const auto result = adapter.set_extra_field(
      17, "opentag_instance_uuid", R"json("uuid-new")json");

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_STRING(
      R"json("alice")json", result.value().extra_json.at("owner").c_str());
  assert_transport_consumed(transport);
}

void test_location_and_field_shapes_are_bounded_and_typed() {
  ScriptedTransport transport;
  transport.expect("GET", "/location", R"json(["Drybox A","Shelf 2"])json");
  transport.expect(
      "GET", "/field/spool",
      R"json([{"key":"opentag_instance_uuid","name":"OpenPrintTag UUID","field_type":"text","multi_choice":false}])json");
  SpoolmanAdapter adapter(transport, settings());

  const auto locations = adapter.list_locations();
  const auto fields = adapter.list_extra_fields();

  TEST_ASSERT_TRUE(locations.ok());
  TEST_ASSERT_EQUAL_UINT(2U, locations.value().size());
  TEST_ASSERT_TRUE(fields.ok());
  TEST_ASSERT_EQUAL_UINT(1U, fields.value().size());
  TEST_ASSERT_EQUAL_STRING("opentag_instance_uuid", fields.value()[0].key.c_str());
  assert_transport_consumed(transport);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_probe_known_version_enables_proven_reads_and_guarded_writes);
  RUN_TEST(test_unknown_version_stays_connected_but_write_capabilities_are_off);
  RUN_TEST(test_spool_response_is_normalized_and_identity_values_are_decoded);
  RUN_TEST(test_malformed_spool_contract_is_rejected_as_api_change);
  RUN_TEST(test_remaining_weight_update_checks_concurrency_and_verifies_readback);
  RUN_TEST(test_remaining_weight_update_aborts_if_usage_changed);
  RUN_TEST(test_remaining_weight_update_rejects_mismatched_readback);
  RUN_TEST(test_explicit_create_spool_serializes_identity_without_implicit_creation);
  RUN_TEST(test_extra_field_patch_contains_only_intended_key_and_is_verified);
  RUN_TEST(test_location_and_field_shapes_are_bounded_and_typed);
  return UNITY_END();
}

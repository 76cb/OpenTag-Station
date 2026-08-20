#include <unity.h>

#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "domain/printer.hpp"
#include "integrations/backend_capabilities.hpp"
#include "integrations/filabridge/filabridge_adapter.hpp"
#include "network/http_transport.hpp"

namespace {

using opentag::config::FilaBridgeSettings;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::domain::PrinterState;
using opentag::integrations::BackendCapability;
using opentag::integrations::filabridge::FilaBridgeAdapter;
using opentag::network::HttpRequest;
using opentag::network::HttpResponse;
using opentag::network::IHttpTransport;

constexpr const char* printers_json = R"json({
  "printers": {
    "xl-stable-id": {
      "name":"Workshop XL",
      "ip_address":"192.0.2.15",
      "api_key":"",
      "api_key_set":true,
      "toolheads":5,
      "type":"prusa-xl",
      "serial":"XL123",
      "toolhead_names":{"0":"Left","4":"Support"}
    }
  }
})json";

constexpr const char* idle_status_json = R"json({
  "printers":{"xl-stable-id":{"name":"Workshop XL","state":"IDLE"}},
  "toolhead_mappings":{"xl-stable-id":{
    "0":{"printer_name":"Workshop XL","toolhead_id":0,"spool_id":147,"mapped_at":"now","display_name":"Left"},
    "1":{"printer_name":"Workshop XL","toolhead_id":1,"spool_id":0,"mapped_at":"","display_name":"Toolhead 1"},
    "2":{"printer_name":"Workshop XL","toolhead_id":2,"spool_id":0,"mapped_at":"","display_name":"Toolhead 2"},
    "3":{"printer_name":"Workshop XL","toolhead_id":3,"spool_id":0,"mapped_at":"","display_name":"Toolhead 3"},
    "4":{"printer_name":"Workshop XL","toolhead_id":4,"spool_id":204,"mapped_at":"now","display_name":"Support"}
  }},
  "timestamp":"2026-08-20T00:00:00Z"
})json";

std::string printing_status_json() {
  std::string result = idle_status_json;
  result.replace(result.find("IDLE"), 4U, "PRINTING");
  return result;
}

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
    TEST_ASSERT_TRUE(request.maximum_response_bytes <= 65536U);
    return expected.response;
  }

  void expect(
      const std::string& method,
      const std::string& path,
      const std::string& response_body,
      std::int32_t status = 200) {
    requests.push_back({
        method,
        "http://filabridge.local" + path,
        std::nullopt,
        Result<HttpResponse>::success({status, response_body, "application/json"}),
    });
  }

  void expect_body(
      const std::string& method,
      const std::string& path,
      const std::string& exact_body,
      const std::string& response_body,
      std::int32_t status = 200) {
    requests.push_back({
        method,
        "http://filabridge.local" + path,
        exact_body,
        Result<HttpResponse>::success({status, response_body, "application/json"}),
    });
  }
};

FilaBridgeSettings settings() {
  FilaBridgeSettings value;
  value.url = "http://filabridge.local/";
  value.authentication_token = "test-token";
  value.selected_printer_id = "xl-stable-id";
  return value;
}

void script_read(
    ScriptedTransport& transport,
    const std::string& status = idle_status_json) {
  transport.expect("GET", "/api/printers", printers_json);
  transport.expect("GET", "/api/status", status);
}

void script_probe(
    ScriptedTransport& transport,
    const std::string& version = "v1.2.2") {
  transport.expect(
      "GET", "/healthz",
      std::string(R"json({"status":"ok","version":")json") + version +
          R"json("})json");
  script_read(transport);
}

void assert_consumed(const ScriptedTransport& transport) {
  TEST_ASSERT_TRUE_MESSAGE(transport.requests.empty(), "expected request was not sent");
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_probe_known_version_enables_reads_and_guarded_mapping() {
  ScriptedTransport transport;
  script_probe(transport);
  FilaBridgeAdapter adapter(transport, settings());

  const auto status = adapter.probe();

  TEST_ASSERT_TRUE(status.ok());
  TEST_ASSERT_TRUE(status.value().connected);
  TEST_ASSERT_TRUE(status.value().healthy);
  TEST_ASSERT_TRUE(status.value().version_formally_tested);
  TEST_ASSERT_EQUAL_STRING("v1.2.2", status.value().version.c_str());
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::health));
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::runtime_version));
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::get_printers));
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::get_toolheads));
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::get_print_state));
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::map_toolhead));
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::unmap_toolhead));
  assert_consumed(transport);
}

void test_unknown_version_keeps_reads_but_refuses_writes() {
  ScriptedTransport transport;
  script_probe(transport, "v1.3.0");
  FilaBridgeAdapter adapter(transport, settings());

  const auto status = adapter.probe();
  const auto rejected = adapter.assign_spool("xl-stable-id", 1, 99);

  TEST_ASSERT_TRUE(status.ok());
  TEST_ASSERT_FALSE(status.value().version_formally_tested);
  TEST_ASSERT_TRUE(status.value().capabilities.has(BackendCapability::get_printers));
  TEST_ASSERT_FALSE(status.value().capabilities.has(BackendCapability::map_toolhead));
  TEST_ASSERT_FALSE(rejected.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::api_changed),
      static_cast<int>(rejected.error().category));
  assert_consumed(transport);
}

void test_printers_use_stable_ids_and_normalize_complete_t1_to_t5_list() {
  ScriptedTransport transport;
  script_read(transport);
  FilaBridgeAdapter adapter(transport, settings());

  const auto result = adapter.list_printers();

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, result.value().size());
  const auto& printer = result.value().front();
  TEST_ASSERT_EQUAL_STRING("xl-stable-id", printer.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Workshop XL", printer.display_name.c_str());
  TEST_ASSERT_EQUAL(static_cast<int>(PrinterState::idle), static_cast<int>(printer.state));
  TEST_ASSERT_EQUAL_UINT(5U, printer.toolheads.size());
  for (int index = 0; index < 5; ++index) {
    TEST_ASSERT_EQUAL_INT(index, printer.toolheads[index].backend_id);
    TEST_ASSERT_EQUAL_INT(index + 1, printer.toolheads[index].display_number);
  }
  TEST_ASSERT_EQUAL_STRING("Left", printer.toolheads[0].display_name.c_str());
  TEST_ASSERT_EQUAL_STRING("Toolhead 1", printer.toolheads[1].display_name.c_str());
  TEST_ASSERT_EQUAL_INT32(147, *printer.toolheads[0].assigned_spool);
  TEST_ASSERT_FALSE(printer.toolheads[1].assigned_spool.has_value());
  TEST_ASSERT_EQUAL_INT32(204, *printer.toolheads[4].assigned_spool);
  assert_consumed(transport);
}

void test_active_print_state_is_exposed_without_guessing() {
  ScriptedTransport transport;
  script_read(transport, printing_status_json());
  FilaBridgeAdapter adapter(transport, settings());

  const auto result = adapter.list_printers();

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(PrinterState::printing),
      static_cast<int>(result.value().front().state));
  TEST_ASSERT_TRUE(opentag::domain::is_active_print_state(result.value().front().state));
  assert_consumed(transport);
}

void test_assignment_translates_stable_id_to_name_and_uses_zero_based_id() {
  ScriptedTransport transport;
  script_probe(transport);
  script_read(transport);
  transport.expect_body(
      "POST", "/api/map_toolhead",
      R"json({"printer_name":"Workshop XL","toolhead_id":2,"spool_id":99})json",
      R"json({"message":"mapped"})json");
  FilaBridgeAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());

  const auto result = adapter.assign_spool("xl-stable-id", 2, 99);

  TEST_ASSERT_TRUE(result.ok());
  assert_consumed(transport);
}

void test_unassignment_uses_zero_spool_id() {
  ScriptedTransport transport;
  script_probe(transport);
  script_read(transport);
  transport.expect_body(
      "POST", "/api/map_toolhead",
      R"json({"printer_name":"Workshop XL","toolhead_id":4,"spool_id":0})json",
      R"json({"message":"unmapped"})json");
  FilaBridgeAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());

  const auto result = adapter.unassign_spool("xl-stable-id", 4);

  TEST_ASSERT_TRUE(result.ok());
  assert_consumed(transport);
}

void test_conflicting_assignment_is_structured_and_not_retried() {
  ScriptedTransport transport;
  script_probe(transport);
  script_read(transport);
  transport.expect_body(
      "POST", "/api/map_toolhead",
      R"json({"printer_name":"Workshop XL","toolhead_id":1,"spool_id":147})json",
      R"json({"detail":"spool already mapped"})json", 409);
  FilaBridgeAdapter adapter(transport, settings());
  TEST_ASSERT_TRUE(adapter.probe().ok());

  const auto result = adapter.assign_spool("xl-stable-id", 1, 147);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::invalid_response),
      static_cast<int>(result.error().category));
  TEST_ASSERT_FALSE(result.error().retryable);
  assert_consumed(transport);
}

void test_malformed_mapping_is_rejected_as_api_change() {
  ScriptedTransport transport;
  transport.expect("GET", "/api/printers", printers_json);
  transport.expect(
      "GET", "/api/status",
      R"json({"printers":{"xl-stable-id":{"state":"IDLE"}},"toolhead_mappings":{"xl-stable-id":{"2":{"toolhead_id":3,"spool_id":99}}}})json");
  FilaBridgeAdapter adapter(transport, settings());

  const auto result = adapter.list_printers();

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::api_changed),
      static_cast<int>(result.error().category));
  assert_consumed(transport);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_probe_known_version_enables_reads_and_guarded_mapping);
  RUN_TEST(test_unknown_version_keeps_reads_but_refuses_writes);
  RUN_TEST(test_printers_use_stable_ids_and_normalize_complete_t1_to_t5_list);
  RUN_TEST(test_active_print_state_is_exposed_without_guessing);
  RUN_TEST(test_assignment_translates_stable_id_to_name_and_uses_zero_based_id);
  RUN_TEST(test_unassignment_uses_zero_spool_id);
  RUN_TEST(test_conflicting_assignment_is_structured_and_not_retried);
  RUN_TEST(test_malformed_mapping_is_rejected_as_api_change);
  return UNITY_END();
}

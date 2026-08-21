#include <unity.h>

#include <ArduinoJson.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "web/api_router.hpp"

namespace {

using opentag::core::Error;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::web::api::ConfigurationPatchMutation;
using opentag::web::api::BodyTransport;
using opentag::web::api::Header;
using opentag::web::api::IApiContext;
using opentag::web::api::Method;
using opentag::web::api::Mutation;
using opentag::web::api::MutationKind;
using opentag::web::api::OperationReceipt;
using opentag::web::api::Request;
using opentag::web::api::Resource;
using opentag::web::api::Response;
using opentag::web::api::Router;
using opentag::web::api::ScaleCalibrationMutation;
using opentag::web::api::ToolheadAssignmentMutation;
using opentag::web::api::ToolheadUnassignmentMutation;
using opentag::web::api::UpdateControlMutation;

class FakeContext final : public IApiContext {
 public:
  bool authorize_mutation(std::string_view bearer_token) override {
    ++authorization_calls;
    last_bearer_token = std::string(bearer_token);
    return authorization_configured && bearer_token == accepted_bearer_token;
  }

  Result<std::string> snapshot_json(Resource resource) override {
    ++snapshot_calls;
    last_resource = resource;
    if (snapshot_error.has_value()) {
      return Result<std::string>::failure(*snapshot_error);
    }
    return Result<std::string>::success(
        resource == Resource::redacted_configuration
            ? configuration_payload
            : snapshot_payload);
  }

  Result<std::optional<std::string>> operation_status_json(
      std::uint64_t operation_id) override {
    ++operation_calls;
    last_operation_id = operation_id;
    if (operation_error.has_value()) {
      return Result<std::optional<std::string>>::failure(*operation_error);
    }
    return Result<std::optional<std::string>>::success(
        operation_known
            ? std::optional<std::string>{operation_payload}
            : std::nullopt);
  }

  Result<OperationReceipt> submit(const Mutation& mutation) override {
    ++submit_calls;
    last_mutation = mutation;
    if (submit_error.has_value()) {
      return Result<OperationReceipt>::failure(*submit_error);
    }
    return Result<OperationReceipt>::success(OperationReceipt{next_operation_id});
  }

  std::size_t authorization_calls{0U};
  bool authorization_configured{true};
  std::string accepted_bearer_token{"station-secret"};
  std::string last_bearer_token;
  std::size_t snapshot_calls{0U};
  std::size_t operation_calls{0U};
  std::size_t submit_calls{0U};
  Resource last_resource{Resource::status};
  std::uint64_t last_operation_id{0U};
  std::string snapshot_payload{R"({"ready":true})"};
  std::string configuration_payload{
      R"({"revision":7,"web":{"access_token_configured":true},"wifi":{"password_configured":true}})"};
  bool operation_known{true};
  std::string operation_payload{
      R"({"operation_id":42,"state":"succeeded"})"};
  std::uint64_t next_operation_id{42U};
  std::optional<Error> snapshot_error;
  std::optional<Error> operation_error;
  std::optional<Error> submit_error;
  std::optional<Mutation> last_mutation;
};

Request get_request(std::string path) {
  return {Method::get, std::move(path), {}, {}};
}

Request mutation_request(
    Method method,
    std::string path,
    std::string body,
    std::string idempotency_key = "browser-1",
    std::string bearer_token = "station-secret") {
  return {
      method,
      std::move(path),
      {
          {"Content-Type", "application/json; charset=utf-8"},
          {"X-OpenTag-Request", "web"},
          {"Idempotency-Key", std::move(idempotency_key)},
          {"Authorization", "Bearer " + std::move(bearer_token)},
      },
      std::move(body),
  };
}

const char* response_header(const Response& response, const char* name) {
  for (const auto& header : response.headers) {
    if (header.name == name) return header.value.c_str();
  }
  return nullptr;
}

void assert_body_contains(const Response& response, const char* expected) {
  TEST_ASSERT_NOT_NULL_MESSAGE(
      std::strstr(response.body.c_str(), expected), response.body.c_str());
}

void assert_versioned_error(
    const Response& response,
    int expected_status,
    const char* expected_code) {
  TEST_ASSERT_EQUAL_INT(expected_status, response.status);
  JsonDocument document;
  const auto parsed = deserializeJson(document, response.body);
  TEST_ASSERT_FALSE_MESSAGE(parsed, response.body.c_str());
  TEST_ASSERT_EQUAL_STRING("v1", document["api_version"].as<const char*>());
  TEST_ASSERT_FALSE(document["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING(
      expected_code, document["error"]["code"].as<const char*>());
  TEST_ASSERT_TRUE(document["error"]["retryable"].is<bool>());
}

bool route_exists(Method method, const char* path_pattern) {
  for (const auto& route : opentag::web::api::routes) {
    if (route.method == method &&
        std::strcmp(route.path_pattern, path_pattern) == 0) {
      return true;
    }
  }
  return false;
}

const opentag::web::api::RouteMetadata* route_metadata(
    Method method,
    const char* path_pattern) {
  for (const auto& route : opentag::web::api::routes) {
    if (route.method == method &&
        std::strcmp(route.path_pattern, path_pattern) == 0) {
      return &route;
    }
  }
  return nullptr;
}
std::string read_project_source(std::string_view relative_path) {
  const auto load = [](const std::string& path) {
    std::ifstream input(path);
    if (!input) return std::string{};
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  };

  const auto direct = load(std::string(relative_path));
  if (!direct.empty()) return direct;

  const std::string compiled_file = __FILE__;
  constexpr std::string_view marker = "test/test_web_api/test_main.cpp";
  const auto marker_at = compiled_file.rfind(marker);
  if (marker_at == std::string::npos) return {};
  return load(compiled_file.substr(0U, marker_at) + std::string(relative_path));
}


constexpr const char* valid_update_sha256 =
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

constexpr const char* valid_assignment = R"({
  "printer_id":"printer-a",
  "expected_spool_id":123,
  "expected_current_spool_id":45,
  "expected_printer_state":"idle",
  "spool_generation":9,
  "printer_revision":17,
  "replace_occupied_confirmed":true,
  "advanced_override":false
})";

constexpr const char* valid_unassignment = R"({
  "printer_id":"printer-a",
  "expected_current_spool_id":45,
  "expected_printer_state":"paused",
  "spool_generation":9,
  "printer_revision":17,
  "advanced_override":true
})";

}  // namespace

void setUp() {}
void tearDown() {}

void test_route_table_contains_the_complete_versioned_surface() {
  TEST_ASSERT_EQUAL_UINT(26U, opentag::web::api::routes.size());
  const std::array<std::pair<Method, const char*>, 26U> expected = {{
      {Method::get, "/api/v1/status"},
      {Method::get, "/api/v1/device"},
      {Method::get, "/api/v1/health"},
      {Method::get, "/api/v1/scale"},
      {Method::post, "/api/v1/scale/tare"},
      {Method::post, "/api/v1/scale/calibrate"},
      {Method::get, "/api/v1/nfc"},
      {Method::get, "/api/v1/nfc/tag"},
      {Method::post, "/api/v1/nfc/read"},
      {Method::get, "/api/v1/spool"},
      {Method::get, "/api/v1/printers"},
      {Method::get, "/api/v1/toolheads"},
      {Method::post, "/api/v1/toolheads/{id}/assign"},
      {Method::post, "/api/v1/toolheads/{id}/unassign"},
      {Method::get, "/api/v1/config"},
      {Method::patch, "/api/v1/config"},
      {Method::get, "/api/v1/diagnostics"},
      {Method::get, "/api/v1/logs"},
      {Method::post, "/api/v1/backends/test"},
      {Method::get, "/api/v1/update"},
      {Method::post, "/api/v1/update/upload"},
      {Method::post, "/api/v1/update/reboot"},
      {Method::post, "/api/v1/update/cancel"},
      {Method::post, "/api/v1/device/reboot"},
      {Method::post, "/api/v1/device/factory-reset"},
      {Method::get, "/api/v1/operations/{id}"},
  }};
  for (const auto& route : expected) {
    TEST_ASSERT_TRUE_MESSAGE(
        route_exists(route.first, route.second), route.second);
  }
}

void test_snapshot_routes_return_versioned_bounded_envelopes() {
  FakeContext context;
  Router router(context);
  const std::array<std::pair<const char*, Resource>, 13U> snapshots = {{
      {"/api/v1/status", Resource::status},
      {"/api/v1/device", Resource::device},
      {"/api/v1/health", Resource::health},
      {"/api/v1/scale", Resource::scale},
      {"/api/v1/nfc", Resource::nfc},
      {"/api/v1/nfc/tag", Resource::nfc_tag},
      {"/api/v1/spool", Resource::spool},
      {"/api/v1/printers", Resource::printers},
      {"/api/v1/toolheads", Resource::toolheads},
      {"/api/v1/config", Resource::redacted_configuration},
      {"/api/v1/diagnostics", Resource::diagnostics},
      {"/api/v1/logs", Resource::logs},
      {"/api/v1/update", Resource::update},
  }};

  for (const auto& snapshot : snapshots) {
    const auto response = router.handle(get_request(snapshot.first));
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, response.status, snapshot.first);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(snapshot.second),
        static_cast<int>(context.last_resource));
    assert_body_contains(response, "\"api_version\":\"v1\"");
    assert_body_contains(response, "\"ok\":true");
    TEST_ASSERT_EQUAL_STRING(
        "application/json; charset=utf-8",
        response_header(response, "Content-Type"));
    TEST_ASSERT_EQUAL_STRING("no-store", response_header(response, "Cache-Control"));
    TEST_ASSERT_EQUAL_STRING("nosniff", response_header(response, "X-Content-Type-Options"));
  }
  TEST_ASSERT_EQUAL_UINT(snapshots.size(), context.snapshot_calls);
}

void test_firmware_upload_is_declared_but_rejected_by_buffered_router() {
  FakeContext context;
  Router router(context);
  const auto* route = route_metadata(
      Method::post, "/api/v1/update/upload");
  TEST_ASSERT_NOT_NULL(route);
  TEST_ASSERT_TRUE(route->mutation);
  TEST_ASSERT_EQUAL_UINT(
      opentag::web::api::maximum_firmware_image_bytes,
      route->maximum_body_bytes);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BodyTransport::streaming_binary),
      static_cast<int>(route->body_transport));

  const auto response = router.handle(mutation_request(
      Method::post, "/api/v1/update/upload", "not-a-buffered-image"));
  assert_versioned_error(response, 500, "streaming_transport_required");
  TEST_ASSERT_EQUAL_UINT(0U, context.authorization_calls);
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);
}

void test_upload_generation_accepts_canonical_zero_and_rejects_aliases() {
  std::uint64_t generation = 99U;
  TEST_ASSERT_TRUE(opentag::web::api::parse_canonical_generation(
      "0", generation));
  TEST_ASSERT_EQUAL_UINT64(0U, generation);
  TEST_ASSERT_TRUE(opentag::web::api::parse_canonical_generation(
      "1", generation));
  TEST_ASSERT_EQUAL_UINT64(1U, generation);
  TEST_ASSERT_TRUE(opentag::web::api::parse_canonical_generation(
      "18446744073709551615", generation));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, generation);

  const std::array<const char*, 8U> invalid = {{
      "",
      "00",
      "01",
      "+1",
      "-1",
      " 1",
      "1 ",
      "18446744073709551616",
  }};
  for (const auto* value : invalid) {
    TEST_ASSERT_FALSE(opentag::web::api::parse_canonical_generation(
        value, generation));
  }
}

void test_route_misses_versions_and_methods_have_stable_errors() {
  FakeContext context;
  Router router(context);

  assert_versioned_error(
      router.handle(get_request("/api/v2/status")),
      404,
      "unsupported_api_version");
  assert_versioned_error(
      router.handle(get_request("/api/v1/missing")), 404, "route_not_found");

  auto wrong_method = get_request("/api/v1/scale/tare");
  const auto response = router.handle(wrong_method);
  assert_versioned_error(response, 405, "method_not_allowed");
  TEST_ASSERT_EQUAL_STRING("POST", response_header(response, "Allow"));

  const std::array<Method, 4U> forwarded_methods = {{
      Method::put, Method::delete_method, Method::head, Method::options,
  }};
  for (const auto method : forwarded_methods) {
    const Request forwarded{method, "/api/v1/status", {}, {}};
    const auto forwarded_response = router.handle(forwarded);
    assert_versioned_error(
        forwarded_response, 405, "method_not_allowed");
    TEST_ASSERT_EQUAL_STRING(
        "GET", response_header(forwarded_response, "Allow"));
  }
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);
}

void test_request_shape_and_body_bounds_are_enforced_before_dispatch() {
  FakeContext context;
  Router router(context);

  assert_versioned_error(
      router.handle(get_request("/api/v1/status?secret=x")),
      400,
      "invalid_request");
  assert_versioned_error(
      router.handle(get_request("/api/v1/status/../config")),
      404,
      "route_not_found");

  auto with_body = get_request("/api/v1/status");
  with_body.body = "{}";
  assert_versioned_error(
      router.handle(with_body), 400, "unexpected_body");

  auto too_large = mutation_request(
      Method::patch,
      "/api/v1/config",
      std::string(opentag::web::api::maximum_request_body_bytes + 1U, 'x'));
  assert_versioned_error(router.handle(too_large), 413, "request_too_large");

  auto route_too_large = mutation_request(
      Method::post, "/api/v1/scale/tare", std::string(257U, 'x'));
  assert_versioned_error(
      router.handle(route_too_large), 413, "request_too_large");

  auto duplicate_header = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}");
  duplicate_header.headers.push_back({"content-type", "application/json"});
  assert_versioned_error(
      router.handle(duplicate_header), 400, "invalid_request_headers");
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);
}

void test_mutation_authorization_is_fail_closed_and_precedes_parsing() {
  FakeContext context;
  Router router(context);

  auto missing = mutation_request(
      Method::post, "/api/v1/scale/tare", "{");
  missing.headers.pop_back();
  auto response = router.handle(missing);
  assert_versioned_error(response, 401, "authentication_required");
  TEST_ASSERT_EQUAL_STRING("Bearer", response_header(response, "WWW-Authenticate"));
  TEST_ASSERT_EQUAL_UINT(0U, context.authorization_calls);
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);

  auto malformed = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}");
  malformed.headers.back().value = "bearer station-secret";
  assert_versioned_error(
      router.handle(malformed), 401, "authentication_required");
  malformed.headers.back().value = "Bearer  station-secret";
  assert_versioned_error(
      router.handle(malformed), 401, "authentication_required");
  TEST_ASSERT_EQUAL_UINT(0U, context.authorization_calls);

  auto wrong = mutation_request(
      Method::post,
      "/api/v1/scale/tare",
      "{}",
      "wrong-token-request",
      "incorrect-secret");
  assert_versioned_error(
      router.handle(wrong), 401, "authentication_required");
  TEST_ASSERT_EQUAL_UINT(1U, context.authorization_calls);
  TEST_ASSERT_EQUAL_STRING("incorrect-secret", context.last_bearer_token.c_str());
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);

  context.authorization_configured = false;
  auto unconfigured = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}");
  assert_versioned_error(
      router.handle(unconfigured), 401, "authentication_required");
  TEST_ASSERT_EQUAL_UINT(2U, context.authorization_calls);
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);

  context.authorization_configured = true;
  response = router.handle(mutation_request(
      Method::post, "/api/v1/scale/tare", "{}"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  TEST_ASSERT_EQUAL_UINT(3U, context.authorization_calls);
  TEST_ASSERT_EQUAL_UINT(1U, context.submit_calls);

  const auto public_response = router.handle(get_request("/api/v1/status"));
  TEST_ASSERT_EQUAL_INT(200, public_response.status);
  TEST_ASSERT_EQUAL_UINT(3U, context.authorization_calls);
}

void test_mutation_headers_are_required_and_idempotency_is_bounded() {
  FakeContext context;
  Router router(context);

  auto missing_source = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}");
  missing_source.headers.erase(missing_source.headers.begin() + 1);
  assert_versioned_error(
      router.handle(missing_source), 400, "invalid_request_headers");

  auto wrong_content = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}");
  wrong_content.headers[0].value = "text/plain";
  assert_versioned_error(
      router.handle(wrong_content), 400, "invalid_request_headers");

  auto bad_key = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}", "contains a space");
  assert_versioned_error(
      router.handle(bad_key), 400, "invalid_request_headers");

  auto accepted = mutation_request(
      Method::post, "/api/v1/scale/tare", "{}", "web:station_1.retry-2");
  TEST_ASSERT_EQUAL_INT(202, router.handle(accepted).status);
  TEST_ASSERT_EQUAL_STRING(
      "web:station_1.retry-2", context.last_mutation->idempotency_key.c_str());
}

void test_empty_mutations_are_strict_and_forward_typed_commands() {
  FakeContext context;
  Router router(context);
  const std::array<std::pair<const char*, MutationKind>, 3U> operations = {{
      {"/api/v1/scale/tare", MutationKind::scale_tare},
      {"/api/v1/nfc/read", MutationKind::nfc_read},
      {"/api/v1/backends/test", MutationKind::backend_test},
  }};

  for (const auto& operation : operations) {
    const auto response = router.handle(mutation_request(
        Method::post, operation.first, "{}"));
    TEST_ASSERT_EQUAL_INT(202, response.status);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(operation.second),
        static_cast<int>(context.last_mutation->kind));
    TEST_ASSERT_TRUE(
        std::holds_alternative<opentag::web::api::EmptyMutation>(
            context.last_mutation->payload));
    assert_body_contains(response, "\"operation_id\":42");
    assert_body_contains(response, "\"state\":\"queued\"");
  }

  assert_versioned_error(
      router.handle(mutation_request(
          Method::post, "/api/v1/nfc/read", R"({"force":true})")),
      400,
      "invalid_request");
  assert_versioned_error(
      router.handle(mutation_request(
          Method::post, "/api/v1/nfc/read", "[]")),
      400,
      "invalid_request");
  assert_versioned_error(
      router.handle(mutation_request(
          Method::post, "/api/v1/nfc/read", "{")),
      400,
      "invalid_request");
}

void test_calibration_accepts_only_a_bounded_numeric_reference() {
  FakeContext context;
  Router router(context);
  const auto response = router.handle(mutation_request(
      Method::post,
      "/api/v1/scale/calibrate",
      R"({"reference_grams":2500.5})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(MutationKind::scale_calibration),
      static_cast<int>(context.last_mutation->kind));
  const auto& calibration =
      std::get<ScaleCalibrationMutation>(context.last_mutation->payload);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 2500.5F, calibration.reference_grams);

  const std::array<const char*, 5U> invalid = {{
      R"({"reference_grams":true})",
      R"({"reference_grams":"100"})",
      R"({"reference_grams":0})",
      R"({"reference_grams":5000.1})",
      R"({"reference_grams":100,"extra":1})",
  }};
  for (const auto* body : invalid) {
    TEST_ASSERT_EQUAL_INT(400, router.handle(mutation_request(
        Method::post, "/api/v1/scale/calibrate", body)).status);
  }
}

void test_assignment_captures_all_stale_state_preconditions() {
  FakeContext context;
  Router router(context);
  const auto response = router.handle(mutation_request(
      Method::post, "/api/v1/toolheads/4/assign", valid_assignment));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  const auto& assignment =
      std::get<ToolheadAssignmentMutation>(context.last_mutation->payload);
  TEST_ASSERT_EQUAL_INT(4, assignment.backend_toolhead_id);
  TEST_ASSERT_EQUAL_INT(123, assignment.expected_spool_id);
  TEST_ASSERT_EQUAL_INT(45, *assignment.preconditions.expected_current_spool_id);
  TEST_ASSERT_EQUAL_STRING(
      "printer-a", assignment.preconditions.printer_id.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "idle", assignment.preconditions.expected_printer_state.c_str());
  TEST_ASSERT_EQUAL_UINT64(9U, assignment.preconditions.spool_generation);
  TEST_ASSERT_EQUAL_UINT64(17U, assignment.preconditions.printer_revision);
  TEST_ASSERT_TRUE(assignment.replace_occupied_confirmed);
  TEST_ASSERT_FALSE(assignment.preconditions.advanced_override);

  const auto empty_current = router.handle(mutation_request(
      Method::post,
      "/api/v1/toolheads/0/assign",
      R"({"printer_id":"p","expected_spool_id":1,"expected_current_spool_id":null,"expected_printer_state":"offline","spool_generation":0,"printer_revision":0,"replace_occupied_confirmed":false,"advanced_override":true})"));
  TEST_ASSERT_EQUAL_INT(202, empty_current.status);
  const auto& vacant =
      std::get<ToolheadAssignmentMutation>(context.last_mutation->payload);
  TEST_ASSERT_FALSE(vacant.preconditions.expected_current_spool_id.has_value());
}

void test_toolhead_paths_and_assignment_shapes_are_strict() {
  FakeContext context;
  Router router(context);
  const std::array<const char*, 3U> bad_paths = {{
      "/api/v1/toolheads/5/assign",
      "/api/v1/toolheads/04/assign",
      "/api/v1/toolheads/x/assign",
  }};
  for (const auto* path : bad_paths) {
    assert_versioned_error(
        router.handle(mutation_request(Method::post, path, valid_assignment)),
        400,
        "invalid_request");
  }

  assert_versioned_error(
      router.handle(mutation_request(
          Method::post,
          "/api/v1/toolheads/0/assign",
          R"({"printer_id":"p","expected_spool_id":1,"expected_current_spool_id":null,"expected_printer_state":"printing","spool_generation":0,"printer_revision":0,"replace_occupied_confirmed":false,"advanced_override":false,"unknown":true})")),
      400,
      "invalid_request");
  assert_versioned_error(
      router.handle(mutation_request(
          Method::post,
          "/api/v1/toolheads/0/assign",
          R"({"printer_id":"p","expected_spool_id":1,"expected_current_spool_id":null,"expected_printer_state":"invented","spool_generation":0,"printer_revision":0,"replace_occupied_confirmed":false,"advanced_override":false})")),
      400,
      "invalid_request");
}

void test_unassignment_requires_exact_six_field_body_and_current_spool() {
  FakeContext context;
  Router router(context);
  const auto response = router.handle(mutation_request(
      Method::post, "/api/v1/toolheads/2/unassign", valid_unassignment));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  const auto& unassignment =
      std::get<ToolheadUnassignmentMutation>(context.last_mutation->payload);
  TEST_ASSERT_EQUAL_INT(2, unassignment.backend_toolhead_id);
  TEST_ASSERT_EQUAL_INT(
      45, *unassignment.preconditions.expected_current_spool_id);
  TEST_ASSERT_TRUE(unassignment.preconditions.advanced_override);

  assert_versioned_error(
      router.handle(mutation_request(
          Method::post,
          "/api/v1/toolheads/2/unassign",
          R"({"printer_id":"printer-a","expected_current_spool_id":null,"expected_printer_state":"paused","spool_generation":9,"printer_revision":17,"advanced_override":true})")),
      400,
      "invalid_request");
  assert_versioned_error(
      router.handle(mutation_request(
          Method::post,
          "/api/v1/toolheads/2/unassign",
          R"({"printer_id":"printer-a","expected_current_spool_id":45,"expected_printer_state":"paused","spool_generation":9,"printer_revision":17,"replace_occupied_confirmed":false,"advanced_override":true})")),
      400,
      "invalid_request");
}

void test_configuration_patch_is_typed_and_omitted_secrets_remain_omitted() {
  FakeContext context;
  Router router(context);
  const auto response = router.handle(mutation_request(
      Method::patch,
      "/api/v1/config",
      R"({
        "expected_revision":7,
        "wifi":{"ssid":"Workshop","auto_reconnect":true},
        "spoolman":{"url":"https://spool.local","authentication_token":"new-token"},
        "scale_profile":{"id":"yzc-133-5kg","load_cell_model":"YZC-133","rated_capacity_grams":5000,"overload_ratio":1.1},
        "toolheads":[{"backend_id":0,"display_name":"T1","nozzle_diameter_mm":0.4,"enabled":true,"nozzle_material":"brass","maximum_temperature_c":300,"notes":"primary"}]
      })"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  const auto& patch =
      std::get<ConfigurationPatchMutation>(context.last_mutation->payload);
  TEST_ASSERT_EQUAL_UINT64(7U, patch.expected_revision);
  TEST_ASSERT_TRUE(patch.wifi.has_value());
  TEST_ASSERT_EQUAL_STRING("Workshop", patch.wifi->ssid->c_str());
  TEST_ASSERT_FALSE(patch.wifi->password.has_value());
  TEST_ASSERT_FALSE(patch.web.has_value());
  TEST_ASSERT_EQUAL_STRING(
      "new-token", patch.spoolman->authentication_token->c_str());
  TEST_ASSERT_EQUAL_STRING("YZC-133", patch.scale_profile->model->c_str());
  TEST_ASSERT_EQUAL_UINT32(5000U, *patch.scale_profile->rated_capacity_grams);
  TEST_ASSERT_EQUAL_UINT(1U, patch.toolheads->size());
  TEST_ASSERT_EQUAL_INT(0, patch.toolheads->front().backend_id);
  TEST_ASSERT_NOT_NULL(
      std::strstr(response.body.c_str(), "\"kind\":\"configuration\""));
  TEST_ASSERT_NULL(std::strstr(response.body.c_str(), "new-token"));
}

void test_configuration_web_access_token_patch_is_strict() {
  FakeContext context;
  Router router(context);

  auto response = router.handle(mutation_request(
      Method::patch,
      "/api/v1/config",
      R"({"expected_revision":7,"web":{"access_token":"0123456789abcdef"}})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  const auto& set_patch =
      std::get<ConfigurationPatchMutation>(context.last_mutation->payload);
  TEST_ASSERT_TRUE(set_patch.web.has_value());
  TEST_ASSERT_TRUE(set_patch.web->access_token.has_value());
  TEST_ASSERT_EQUAL_STRING(
      "0123456789abcdef", set_patch.web->access_token->c_str());
  TEST_ASSERT_NULL(std::strstr(response.body.c_str(), "0123456789abcdef"));

  response = router.handle(mutation_request(
      Method::patch,
      "/api/v1/config",
      R"({"expected_revision":7,"web":{"access_token":""}})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  const auto& clear_patch =
      std::get<ConfigurationPatchMutation>(context.last_mutation->payload);
  TEST_ASSERT_TRUE(clear_patch.web.has_value());
  TEST_ASSERT_TRUE(clear_patch.web->access_token.has_value());
  TEST_ASSERT_TRUE(clear_patch.web->access_token->empty());

  const std::string maximum_token(128U, 'A');
  const auto maximum_body =
      std::string{"{\"expected_revision\":7,\"web\":{\"access_token\":\""} +
      maximum_token + "\"}}";
  response = router.handle(mutation_request(
      Method::patch, "/api/v1/config", maximum_body));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  const auto& maximum_patch =
      std::get<ConfigurationPatchMutation>(context.last_mutation->payload);
  TEST_ASSERT_TRUE(maximum_patch.web.has_value());
  TEST_ASSERT_TRUE(maximum_patch.web->access_token.has_value());
  TEST_ASSERT_EQUAL_UINT(128U, maximum_patch.web->access_token->size());

  const std::array<const char*, 8U> invalid = {{
      R"({"expected_revision":7,"web":"not-an-object"})",
      R"({"expected_revision":7,"web":{"access_token":123}})",
      R"({"expected_revision":7,"web":{"access_token":null}})",
      R"({"expected_revision":7,"web":{"unexpected":true}})",
      R"({"expected_revision":7,"web":{}})",
      R"({"expected_revision":7,"web":{"access_token":"too-short"}})",
      R"({"expected_revision":7,"web":{"access_token":"invalid-token-value!"}})",
      R"({"expected_revision":7,"web":{"access_token":true}})",
  }};
  for (const auto* body : invalid) {
    assert_versioned_error(
        router.handle(mutation_request(
            Method::patch, "/api/v1/config", body)),
        400,
        "invalid_request");
  }

  const std::string oversized_token(129U, 'A');
  const auto oversized_body =
      std::string{"{\"expected_revision\":7,\"web\":{\"access_token\":\""} +
      oversized_token + "\"}}";
  assert_versioned_error(
      router.handle(mutation_request(
          Method::patch, "/api/v1/config", oversized_body)),
      400,
      "invalid_request");
  TEST_ASSERT_EQUAL_UINT(3U, context.submit_calls);
}

void test_configuration_patch_rejects_unknown_invalid_or_ambiguous_fields() {
  FakeContext context;
  Router router(context);
  const std::array<const char*, 6U> invalid = {{
      R"({"device":{"hostname":"station"}})",
      R"({"expected_revision":7})",
      R"({"expected_revision":7,"wifi":{"password_configured":true}})",
      R"({"expected_revision":7,"scale_profile":{"id":"yzc-133-2kg","rated_capacity_grams":5000}})",
      R"({"expected_revision":7,"scale_profile":{"rated_capacity_grams":2001}})",
      R"({"expected_revision":7,"toolheads":[{"backend_id":0,"display_name":"T1","nozzle_diameter_mm":0.4,"enabled":true,"nozzle_material":"brass","maximum_temperature_c":300},{"backend_id":0,"display_name":"T2","nozzle_diameter_mm":0.6,"enabled":true,"nozzle_material":"steel","maximum_temperature_c":300}]})",
  }};
  for (const auto* body : invalid) {
    assert_versioned_error(
        router.handle(mutation_request(
            Method::patch, "/api/v1/config", body)),
        400,
        "invalid_request");
  }
  TEST_ASSERT_EQUAL_UINT(0U, context.submit_calls);
}

void test_configuration_snapshot_blocks_secret_keys_but_allows_flags() {
  FakeContext context;
  Router router(context);
  auto response = router.handle(get_request("/api/v1/config"));
  TEST_ASSERT_EQUAL_INT(200, response.status);
  assert_body_contains(response, "password_configured");
  assert_body_contains(response, "access_token_configured");

  context.configuration_payload =
      R"({"wifi":{"password":"never-return-this"}})";
  response = router.handle(get_request("/api/v1/config"));
  assert_versioned_error(response, 500, "unsafe_configuration_snapshot");
  TEST_ASSERT_NULL(std::strstr(response.body.c_str(), "never-return-this"));

  context.configuration_payload =
      R"({"backend":{"authentication_token":"secret"}})";
  response = router.handle(get_request("/api/v1/config"));
  assert_versioned_error(response, 500, "unsafe_configuration_snapshot");
}

void test_snapshot_json_must_be_valid_and_bounded() {
  FakeContext context;
  Router router(context);
  context.snapshot_payload = "{";
  assert_versioned_error(
      router.handle(get_request("/api/v1/status")), 500, "invalid_snapshot");

  context.snapshot_payload = std::string(
      opentag::web::api::maximum_snapshot_json_bytes + 1U, 'x');
  assert_versioned_error(
      router.handle(get_request("/api/v1/status")), 500, "invalid_snapshot");
}

void test_operation_status_has_strict_positive_ids_and_not_found_state() {
  FakeContext context;
  Router router(context);
  auto response = router.handle(get_request("/api/v1/operations/42"));
  TEST_ASSERT_EQUAL_INT(200, response.status);
  TEST_ASSERT_EQUAL_UINT64(42U, context.last_operation_id);
  assert_body_contains(response, "\"state\":\"succeeded\"");

  context.operation_known = false;
  response = router.handle(get_request("/api/v1/operations/99"));
  assert_versioned_error(response, 404, "operation_not_found");

  assert_versioned_error(
      router.handle(get_request("/api/v1/operations/0")),
      400,
      "invalid_operation_id");
  assert_versioned_error(
      router.handle(get_request("/api/v1/operations/042")),
      400,
      "invalid_operation_id");
}

void test_context_failures_map_to_structured_service_errors() {
  FakeContext context;
  Router router(context);
  context.snapshot_error = Error{
      ErrorCategory::scale_unavailable,
      "ADC unavailable \"temporarily\"",
      true,
  };
  auto response = router.handle(get_request("/api/v1/scale"));
  assert_versioned_error(response, 503, "scale_unavailable");
  JsonDocument document;
  TEST_ASSERT_FALSE(deserializeJson(document, response.body));
  TEST_ASSERT_TRUE(document["error"]["retryable"].as<bool>());
  TEST_ASSERT_EQUAL_STRING(
      "ADC unavailable \"temporarily\"",
      document["error"]["message"].as<const char*>());

  context.snapshot_error.reset();
  context.submit_error =
      Error{ErrorCategory::configuration, "stale revision", false};
  response = router.handle(mutation_request(
      Method::post, "/api/v1/scale/tare", "{}"));
  assert_versioned_error(response, 422, "validation_failed");

  context.submit_error =
      Error{ErrorCategory::conflict, "stale precondition", false};
  response = router.handle(mutation_request(
      Method::post, "/api/v1/scale/tare", "{}"));
  assert_versioned_error(response, 409, "state_conflict");

  context.submit_error.reset();
  context.snapshot_error = Error{
      ErrorCategory::firmware_update,
      "OTA owner is unavailable",
      true,
  };
  response = router.handle(get_request("/api/v1/update"));
  assert_versioned_error(response, 503, "update_unavailable");

  context.snapshot_error = Error{
      ErrorCategory::firmware_update,
      "Firmware manifest is incompatible",
      false,
  };
  response = router.handle(get_request("/api/v1/update"));
  assert_versioned_error(response, 422, "firmware_validation_failed");
}

void test_payload_digest_is_stable_and_covers_route_kind_and_exact_body() {
  FakeContext context;
  Router router(context);

  TEST_ASSERT_EQUAL_INT(202, router.handle(mutation_request(
      Method::post, "/api/v1/scale/tare", "{}", "first-key")).status);
  const auto exact_digest = context.last_mutation->payload_digest;
  TEST_ASSERT_NOT_EQUAL_UINT64(0U, exact_digest);

  TEST_ASSERT_EQUAL_INT(202, router.handle(mutation_request(
      Method::post, "/api/v1/scale/tare", "{}", "second-key")).status);
  TEST_ASSERT_EQUAL_UINT64(exact_digest, context.last_mutation->payload_digest);

  TEST_ASSERT_EQUAL_INT(202, router.handle(mutation_request(
      Method::post, "/api/v1/scale/tare", "{ }", "third-key")).status);
  TEST_ASSERT_NOT_EQUAL_UINT64(exact_digest, context.last_mutation->payload_digest);

  TEST_ASSERT_EQUAL_INT(202, router.handle(mutation_request(
      Method::post, "/api/v1/nfc/read", "{}", "fourth-key")).status);
  TEST_ASSERT_NOT_EQUAL_UINT64(exact_digest, context.last_mutation->payload_digest);
}

void test_zero_receipts_are_not_reported_as_accepted() {
  FakeContext context;
  Router router(context);
  context.next_operation_id = 0U;
  const auto response = router.handle(mutation_request(
      Method::post, "/api/v1/nfc/read", "{}"));
  assert_versioned_error(response, 503, "operation_not_queued");
  JsonDocument document;
  TEST_ASSERT_FALSE(deserializeJson(document, response.body));
  TEST_ASSERT_TRUE(document["error"]["retryable"].as<bool>());
}

void test_reboot_and_factory_reset_require_exact_confirmations() {
  FakeContext context;
  Router router(context);
  auto response = router.handle(mutation_request(
      Method::post,
      "/api/v1/device/reboot",
      R"({"confirmation":"REBOOT"})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(MutationKind::reboot),
      static_cast<int>(context.last_mutation->kind));

  response = router.handle(mutation_request(
      Method::post,
      "/api/v1/device/factory-reset",
      R"({"confirmation":"FACTORY RESET"})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(MutationKind::factory_reset),
      static_cast<int>(context.last_mutation->kind));

  assert_versioned_error(
      router.handle(mutation_request(
          Method::post,
          "/api/v1/device/factory-reset",
          R"({"confirmation":"yes"})")),
      400,
      "invalid_request");
}

void test_update_reboot_and_cancel_require_exact_stale_state_preconditions() {
  FakeContext context;
  Router router(context);
  const std::string prefix =
      std::string(R"({"upload_operation_id":91,"expected_generation":7,"expected_sha256":")") +
      valid_update_sha256 + R"(","confirmation":")";

  auto response = router.handle(mutation_request(
      Method::post,
      "/api/v1/update/reboot",
      prefix + R"(REBOOT INTO UPDATE"})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(MutationKind::update_reboot),
      static_cast<int>(context.last_mutation->kind));
  auto payload =
      std::get<UpdateControlMutation>(context.last_mutation->payload);
  TEST_ASSERT_EQUAL_UINT64(91U, payload.upload_operation_id);
  TEST_ASSERT_EQUAL_UINT64(7U, payload.expected_generation);
  TEST_ASSERT_EQUAL_STRING(valid_update_sha256, payload.expected_sha256.c_str());
  TEST_ASSERT_EQUAL_STRING("REBOOT INTO UPDATE", payload.confirmation.c_str());

  response = router.handle(mutation_request(
      Method::post,
      "/api/v1/update/cancel",
      prefix + R"(CANCEL UPDATE"})"));
  TEST_ASSERT_EQUAL_INT(202, response.status);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(MutationKind::update_cancel),
      static_cast<int>(context.last_mutation->kind));

  const std::array<std::string, 7U> invalid_bodies = {{
      std::string(R"({"upload_operation_id":0,"expected_generation":7,"expected_sha256":")") +
          valid_update_sha256 + R"(","confirmation":"REBOOT INTO UPDATE"})",
      std::string(R"({"upload_operation_id":91,"expected_generation":0,"expected_sha256":")") +
          valid_update_sha256 + R"(","confirmation":"REBOOT INTO UPDATE"})",
      std::string(R"({"upload_operation_id":"91","expected_generation":7,"expected_sha256":")") +
          valid_update_sha256 + R"(","confirmation":"REBOOT INTO UPDATE"})",
      R"({"upload_operation_id":91,"expected_generation":7,"expected_sha256":"abc","confirmation":"REBOOT INTO UPDATE"})",
      R"({"upload_operation_id":91,"expected_generation":7,"expected_sha256":"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF","confirmation":"REBOOT INTO UPDATE"})",
      std::string(R"({"upload_operation_id":91,"expected_generation":7,"expected_sha256":")") +
          valid_update_sha256 + R"(","confirmation":"yes"})",
      std::string(R"({"upload_operation_id":91,"expected_generation":7,"expected_sha256":")") +
          valid_update_sha256 + R"(","confirmation":"REBOOT INTO UPDATE","extra":true})",
  }};
  for (const auto& body : invalid_bodies) {
    assert_versioned_error(
        router.handle(mutation_request(
            Method::post, "/api/v1/update/reboot", body)),
        400,
        "invalid_request");
  }
}

void test_public_update_partition_serializer_never_exposes_flash_address() {
  const auto source = read_project_source("src/web/application_api_context.cpp");
  TEST_ASSERT_FALSE(source.empty());

  const auto begin = source.find("void write_partition(");
  const auto end = source.find("void write_firmware(", begin);
  TEST_ASSERT_TRUE(begin != std::string::npos);
  TEST_ASSERT_TRUE(end != std::string::npos);
  const auto serializer = source.substr(begin, end - begin);
  TEST_ASSERT_TRUE(serializer.find("object[\"address\"]") == std::string::npos);
  TEST_ASSERT_TRUE(serializer.find("object[\"label\"]") != std::string::npos);
  TEST_ASSERT_TRUE(serializer.find("object[\"size\"]") != std::string::npos);
  TEST_ASSERT_TRUE(serializer.find("object[\"subtype\"]") != std::string::npos);
  TEST_ASSERT_TRUE(serializer.find("object[\"present\"]") != std::string::npos);
}

void test_update_owner_capabilities_and_safe_reboot_retry_are_fail_closed() {
  const auto source = read_project_source("src/web/application_api_context.cpp");
  TEST_ASSERT_FALSE(source.empty());

  const auto begin = source.find("bool validated_unselected_candidate(");
  const auto end = source.find("void write_update(", begin);
  TEST_ASSERT_TRUE(begin != std::string::npos);
  TEST_ASSERT_TRUE(end != std::string::npos);
  const auto predicate = source.substr(begin, end - begin);
  TEST_ASSERT_TRUE(predicate.find("!update.activation_intent") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("bool activation_retry_candidate(") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("PartitionImageState::new_image") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("PartitionImageState::pending_verify") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("PartitionImageState::valid") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("PartitionImageState::undefined") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("update.activation_intent") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("update.target.present()") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("same_partition(update.boot, update.running)") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("same_partition(update.inactive, update.target)") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("same_partition(update.boot, update.target)") != std::string::npos);
  TEST_ASSERT_TRUE(predicate.find("same_partition(update.running, update.target)") != std::string::npos);

  TEST_ASSERT_TRUE(source.find("capabilities[\"owner_ready\"]") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("capabilities[\"maximum_image_bytes\"]") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("capabilities[\"upload_available\"] = owner_ready &&") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("capabilities[\"cancel_available\"] = owner_ready &&") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("capabilities[\"reboot_available\"] = owner_ready &&") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("queues[\"ota\"]") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("document[\"ota_owner_ready\"]") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("abort_upload(precondition, cleanup_now_ms)") != std::string::npos);

  const auto worker = read_project_source("src/application/ota_worker.cpp");
  TEST_ASSERT_FALSE(worker.empty());
  const auto recovery_branch = worker.find(
      "if (rollback_seed_recovery_state(initialized_state))");
  const auto candidate_branch = worker.find(
      "else if (pending_bootloader_confirmation(", recovery_branch);
  TEST_ASSERT_TRUE(recovery_branch != std::string::npos);
  TEST_ASSERT_TRUE(candidate_branch != std::string::npos);
  TEST_ASSERT_TRUE(recovery_branch < candidate_branch);
  TEST_ASSERT_TRUE(worker.find(
      "manager_.cancel(*abandoned_begin, millis())") != std::string::npos);

  const auto application = read_project_source("src/application/application.cpp");
  TEST_ASSERT_FALSE(application.empty());
  TEST_ASSERT_TRUE(application.find(
      "device_control_started_ = ota_task_started_ &&") != std::string::npos);
  TEST_ASSERT_TRUE(application.find(
      "network_task_started_ = ota_task_started_ &&") != std::string::npos);
}

void test_update_ui_treats_explicit_server_capability_false_as_authoritative() {
  const auto source = read_project_source("src/web/web_assets.cpp");
  TEST_ASSERT_FALSE(source.empty());

  const auto begin = source.find("function updateCapability(");
  const auto end = source.find("function renderUpdate(", begin);
  TEST_ASSERT_TRUE(begin != std::string::npos);
  TEST_ASSERT_TRUE(end != std::string::npos);
  const auto buttons = source.substr(begin, end - begin);
  TEST_ASSERT_TRUE(buttons.find("Object.prototype.hasOwnProperty.call") != std::string::npos);
  TEST_ASSERT_TRUE(buttons.find("updateCapability(capabilities, 'upload_available'") != std::string::npos);
  TEST_ASSERT_TRUE(buttons.find("updateCapability(capabilities, 'cancel_available'") != std::string::npos);
  TEST_ASSERT_TRUE(buttons.find("updateCapability(capabilities, 'reboot_available'") != std::string::npos);
  TEST_ASSERT_TRUE(buttons.find("capabilities.upload_available === true ||") == std::string::npos);
  TEST_ASSERT_TRUE(buttons.find("capabilities.cancel_available === true ||") == std::string::npos);
  TEST_ASSERT_TRUE(buttons.find("capabilities.reboot_available === true ||") == std::string::npos);
  TEST_ASSERT_TRUE(source.find("first(payload.message, payload.detail)") != std::string::npos);
  TEST_ASSERT_TRUE(source.find(
      "it is not selected for boot until you confirm reboot") == std::string::npos);
  TEST_ASSERT_TRUE(source.find(
      "if (await submitRestartButton(button, '/update/reboot', body, 'Update reboot'))") !=
      std::string::npos);
  TEST_ASSERT_TRUE(source.find(
      "setText('update-state', 'Reboot accepted; waiting for candidate');") !=
      std::string::npos);
  TEST_ASSERT_TRUE(source.find("return true;") != std::string::npos);
  TEST_ASSERT_TRUE(source.find("return false;") != std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_route_table_contains_the_complete_versioned_surface);
  RUN_TEST(test_snapshot_routes_return_versioned_bounded_envelopes);
  RUN_TEST(test_firmware_upload_is_declared_but_rejected_by_buffered_router);
  RUN_TEST(
      test_upload_generation_accepts_canonical_zero_and_rejects_aliases);
  RUN_TEST(test_route_misses_versions_and_methods_have_stable_errors);
  RUN_TEST(test_request_shape_and_body_bounds_are_enforced_before_dispatch);
  RUN_TEST(test_mutation_authorization_is_fail_closed_and_precedes_parsing);
  RUN_TEST(test_mutation_headers_are_required_and_idempotency_is_bounded);
  RUN_TEST(test_empty_mutations_are_strict_and_forward_typed_commands);
  RUN_TEST(test_calibration_accepts_only_a_bounded_numeric_reference);
  RUN_TEST(test_assignment_captures_all_stale_state_preconditions);
  RUN_TEST(test_toolhead_paths_and_assignment_shapes_are_strict);
  RUN_TEST(test_unassignment_requires_exact_six_field_body_and_current_spool);
  RUN_TEST(test_configuration_patch_is_typed_and_omitted_secrets_remain_omitted);
  RUN_TEST(test_configuration_web_access_token_patch_is_strict);
  RUN_TEST(test_configuration_patch_rejects_unknown_invalid_or_ambiguous_fields);
  RUN_TEST(test_configuration_snapshot_blocks_secret_keys_but_allows_flags);
  RUN_TEST(test_snapshot_json_must_be_valid_and_bounded);
  RUN_TEST(test_operation_status_has_strict_positive_ids_and_not_found_state);
  RUN_TEST(test_context_failures_map_to_structured_service_errors);
  RUN_TEST(test_payload_digest_is_stable_and_covers_route_kind_and_exact_body);
  RUN_TEST(test_zero_receipts_are_not_reported_as_accepted);
  RUN_TEST(test_reboot_and_factory_reset_require_exact_confirmations);
  RUN_TEST(
      test_update_reboot_and_cancel_require_exact_stale_state_preconditions);
  RUN_TEST(test_public_update_partition_serializer_never_exposes_flash_address);
  RUN_TEST(
      test_update_owner_capabilities_and_safe_reboot_retry_are_fail_closed);
  RUN_TEST(
      test_update_ui_treats_explicit_server_capability_false_as_authoritative);
  return UNITY_END();
}

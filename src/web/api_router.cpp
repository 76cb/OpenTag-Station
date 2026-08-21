#include "web/api_router.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace opentag::web::api {
namespace {

constexpr std::size_t maximum_error_message_bytes = 512U;
constexpr std::size_t maximum_json_nesting = 8U;

core::Error invalid_request(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

std::string bounded_text(std::string value, std::size_t maximum) {
  if (value.size() > maximum) value.resize(maximum);
  for (auto& character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20U && character != '\t' && character != '\n') {
      character = ' ';
    }
  }
  return value;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

std::string trim_ascii(std::string value) {
  const auto whitespace = [](char character) {
    return character == ' ' || character == '\t';
  };
  while (!value.empty() && whitespace(value.front())) value.erase(value.begin());
  while (!value.empty() && whitespace(value.back())) value.pop_back();
  return value;
}

Response base_response(std::int32_t status, std::string body) {
  Response result;
  result.status = status;
  result.headers = {
      {"Content-Type", "application/json; charset=utf-8"},
      {"Cache-Control", "no-store"},
      {"X-Content-Type-Options", "nosniff"},
  };
  result.body = std::move(body);
  return result;
}

Response error_response(
    std::int32_t status,
    const char* code,
    std::string message,
    bool retryable = false) {
  JsonDocument document;
  document["api_version"] = version;
  document["ok"] = false;
  auto error = document["error"].to<JsonObject>();
  error["code"] = code;
  error["message"] = bounded_text(
      std::move(message), maximum_error_message_bytes);
  error["retryable"] = retryable;
  std::string body;
  serializeJson(document, body);
  return base_response(status, std::move(body));
}

Response authentication_required() {
  auto response = error_response(
      401,
      "authentication_required",
      "A valid bearer token is required for mutation requests");
  response.headers.push_back({"WWW-Authenticate", "Bearer"});
  return response;
}

Response context_error(const core::Error& error) {
  switch (error.category) {
    case core::ErrorCategory::conflict:
      return error_response(409, "state_conflict", error.message, error.retryable);
    case core::ErrorCategory::network:
      return error_response(503, "network_unavailable", error.message, error.retryable);
    case core::ErrorCategory::authentication:
      return error_response(502, "backend_authentication_failed", error.message, false);
    case core::ErrorCategory::backend_unavailable:
      return error_response(503, "backend_unavailable", error.message, error.retryable);
    case core::ErrorCategory::api_changed:
      return error_response(502, "backend_api_changed", error.message, false);
    case core::ErrorCategory::invalid_response:
      return error_response(502, "invalid_backend_response", error.message, error.retryable);
    case core::ErrorCategory::nfc_communication:
    case core::ErrorCategory::nfc_crc:
    case core::ErrorCategory::multiple_tags:
      return error_response(503, "nfc_unavailable", error.message, error.retryable);
    case core::ErrorCategory::unsupported_tag:
    case core::ErrorCategory::invalid_openprinttag:
      return error_response(422, "invalid_tag", error.message, false);
    case core::ErrorCategory::tag_write_protected:
    case core::ErrorCategory::tag_removed:
      return error_response(409, "tag_state_conflict", error.message, error.retryable);
    case core::ErrorCategory::scale_unavailable:
      return error_response(503, "scale_unavailable", error.message, error.retryable);
    case core::ErrorCategory::scale_unstable:
      return error_response(409, "scale_unstable", error.message, error.retryable);
    case core::ErrorCategory::configuration:
      return error_response(422, "validation_failed", error.message, false);
    case core::ErrorCategory::storage:
      return error_response(507, "persistence_failed", error.message, error.retryable);
    case core::ErrorCategory::firmware_update:
      return error.retryable
          ? error_response(503, "update_unavailable", error.message, true)
          : error_response(
                422,
                "firmware_validation_failed",
                error.message,
                false);
  }
  return error_response(500, "internal_error", "The request could not be completed");
}

const char* mutation_name(MutationKind kind) {
  switch (kind) {
    case MutationKind::scale_tare: return "scale_tare";
    case MutationKind::scale_calibration: return "scale_calibration";
    case MutationKind::nfc_read: return "nfc_read";
    case MutationKind::toolhead_assignment: return "toolhead_assignment";
    case MutationKind::toolhead_unassignment: return "toolhead_unassignment";
    case MutationKind::configuration_patch: return "configuration";
    case MutationKind::backend_test: return "backend_probe";
    case MutationKind::update_reboot: return "update_reboot";
    case MutationKind::update_cancel: return "update_cancel";
    case MutationKind::reboot: return "reboot";
    case MutationKind::factory_reset: return "factory_reset";
    case MutationKind::network_scan: return "network_scan";
    case MutationKind::network_connect: return "network_connect";
    case MutationKind::network_setup_mode: return "network_setup_mode";
  }
  return "unknown";
}

std::uint64_t mutation_payload_digest(
    MutationKind kind,
    const std::string& path,
    const std::string& body) {
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  std::uint64_t digest = offset_basis;
  const auto absorb = [&](unsigned char byte) {
    digest ^= static_cast<std::uint64_t>(byte);
    digest *= prime;
  };
  absorb(static_cast<unsigned char>(kind));
  absorb(0U);
  for (const auto character : path) {
    absorb(static_cast<unsigned char>(character));
  }
  absorb(0U);
  for (const auto character : body) {
    absorb(static_cast<unsigned char>(character));
  }
  return digest;
}

Response operation_response(MutationKind kind, std::uint64_t operation_id) {
  JsonDocument document;
  document["api_version"] = version;
  document["ok"] = true;
  auto data = document["data"].to<JsonObject>();
  data["operation_id"] = operation_id;
  data["kind"] = mutation_name(kind);
  data["state"] = "queued";
  std::string body;
  serializeJson(document, body);
  return base_response(202, std::move(body));
}

bool forbidden_configuration_key(std::string_view key) {
  std::string normalized(key);
  normalized = lower_ascii(std::move(normalized));
  return normalized == "password" || normalized == "authentication_token" ||
      normalized == "authorization" || normalized == "access_token" ||
      normalized == "api_token" || normalized == "bearer_token" ||
      normalized == "ca_certificate_pem";
}

bool contains_forbidden_configuration_key(
    JsonVariantConst value,
    std::size_t depth = 0U) {
  if (depth > maximum_json_nesting) return true;
  if (value.is<JsonObjectConst>()) {
    for (const auto pair : value.as<JsonObjectConst>()) {
      if (forbidden_configuration_key(pair.key().c_str()) ||
          contains_forbidden_configuration_key(pair.value(), depth + 1U)) {
        return true;
      }
    }
  } else if (value.is<JsonArrayConst>()) {
    for (const auto child : value.as<JsonArrayConst>()) {
      if (contains_forbidden_configuration_key(child, depth + 1U)) return true;
    }
  }
  return false;
}

Response payload_response(
    const std::string& payload,
    bool enforce_configuration_redaction = false) {
  if (payload.empty() || payload.size() > maximum_snapshot_json_bytes) {
    return error_response(
        500,
        "invalid_snapshot",
        "The application snapshot is empty or exceeds its configured bound");
  }
  JsonDocument source;
  const auto parsed = deserializeJson(
      source,
      payload,
      DeserializationOption::NestingLimit(maximum_json_nesting));
  if (parsed) {
    return error_response(
        500,
        "invalid_snapshot",
        "The application snapshot is not valid bounded JSON");
  }
  if (enforce_configuration_redaction &&
      contains_forbidden_configuration_key(source.as<JsonVariantConst>())) {
    return error_response(
        500,
        "unsafe_configuration_snapshot",
        "The configuration view contained a forbidden credential field");
  }
  JsonDocument document;
  document["api_version"] = version;
  document["ok"] = true;
  document["data"].set(source.as<JsonVariantConst>());
  std::string body;
  serializeJson(document, body);
  if (body.size() > maximum_response_body_bytes) {
    return error_response(
        500,
        "response_too_large",
        "The API response exceeds its configured bound");
  }
  return base_response(200, std::move(body));
}

bool valid_header_name(const std::string& name) {
  if (name.empty() || name.size() > 64U) return false;
  return std::all_of(name.begin(), name.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '-' || character == '_';
  });
}

bool valid_header_value(const std::string& value) {
  if (value.size() > 512U) return false;
  return std::none_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return character == '\r' || character == '\n' || byte == 0U ||
        (byte < 0x20U && character != '\t');
  });
}

core::Result<std::optional<std::string>> unique_header(
    const Request& request,
    const char* requested_name,
    bool trim_value = true) {
  std::optional<std::string> result;
  const auto expected = lower_ascii(requested_name);
  for (const auto& header : request.headers) {
    if (lower_ascii(header.name) != expected) continue;
    if (result.has_value()) {
      return core::Result<std::optional<std::string>>::failure(
          invalid_request(std::string("duplicate ") + requested_name + " header"));
    }
    result = trim_value ? trim_ascii(header.value) : header.value;
  }
  return core::Result<std::optional<std::string>>::success(std::move(result));
}

core::Result<void> validate_request_shape(const Request& request) {
  if (request.path.empty() || request.path.size() > maximum_request_path_bytes ||
      request.path.front() != '/' ||
      request.path.find('?') != std::string::npos ||
      request.path.find('#') != std::string::npos ||
      std::any_of(request.path.begin(), request.path.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x21U || byte == 0x7FU;
      })) {
    return core::Result<void>::failure(
        invalid_request("request path is invalid or too long"));
  }
  if (request.headers.size() > maximum_request_headers ||
      request.body.size() > maximum_request_body_bytes) {
    return core::Result<void>::failure(
        invalid_request("request headers or body exceed the global limit"));
  }
  std::size_t header_bytes = 0U;
  for (const auto& header : request.headers) {
    header_bytes += header.name.size() + header.value.size() + 4U;
    if (!valid_header_name(header.name) || !valid_header_value(header.value) ||
        header_bytes > maximum_request_header_bytes) {
      return core::Result<void>::failure(
          invalid_request("request headers are invalid or too large"));
    }
  }
  return core::Result<void>::success();
}

bool path_matches(const char* pattern_text, const std::string& path) {
  const std::string pattern(pattern_text);
  const auto marker = pattern.find("{id}");
  if (marker == std::string::npos) return path == pattern;
  const auto leading = pattern.substr(0U, marker);
  const auto trailing = pattern.substr(marker + 4U);
  if (path.size() <= leading.size() + trailing.size() ||
      path.compare(0U, leading.size(), leading) != 0 ||
      path.compare(path.size() - trailing.size(), trailing.size(), trailing) != 0) {
    return false;
  }
  const auto value = path.substr(
      leading.size(), path.size() - leading.size() - trailing.size());
  return !value.empty() && value.find('/') == std::string::npos;
}

std::string allowed_methods(const std::vector<const RouteMetadata*>& matches) {
  std::string result;
  for (const auto* route : matches) {
    const std::string method = to_string(route->method);
    if (result.find(method) != std::string::npos) continue;
    if (!result.empty()) result += ", ";
    result += method;
  }
  return result;
}

bool valid_content_type(std::string value) {
  value = lower_ascii(trim_ascii(std::move(value)));
  value.erase(
      std::remove_if(value.begin(), value.end(), [](char character) {
        return character == ' ' || character == '\t';
      }),
      value.end());
  return value == "application/json" ||
      value == "application/json;charset=utf-8";
}

bool valid_idempotency_key(const std::string& value) {
  return !value.empty() && value.size() <= 64U &&
      std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' || character == '_' ||
            character == '.' || character == ':';
      });
}

bool read_required_positive_uint64(
    JsonObjectConst object,
    const char* key,
    std::uint64_t& destination) {
  const auto value = object[key];
  if (value.is<bool>() || !value.is<std::uint64_t>()) return false;
  destination = value.as<std::uint64_t>();
  return destination > 0U;
}

core::Result<std::string> validate_mutation_headers(const Request& request) {
  const auto content_type = unique_header(request, "Content-Type");
  if (!content_type.ok()) {
    return core::Result<std::string>::failure(content_type.error());
  }
  if (!content_type.value().has_value() ||
      !valid_content_type(*content_type.value())) {
    return core::Result<std::string>::failure(
        invalid_request("mutation requests require application/json"));
  }
  const auto source = unique_header(request, "X-OpenTag-Request");
  if (!source.ok()) return core::Result<std::string>::failure(source.error());
  if (!source.value().has_value() || *source.value() != "web") {
    return core::Result<std::string>::failure(
        invalid_request("mutation request source header is missing or invalid"));
  }
  const auto idempotency = unique_header(request, "Idempotency-Key");
  if (!idempotency.ok()) {
    return core::Result<std::string>::failure(idempotency.error());
  }
  if (!idempotency.value().has_value() ||
      !valid_idempotency_key(*idempotency.value())) {
    return core::Result<std::string>::failure(
        invalid_request("Idempotency-Key is missing or invalid"));
  }
  return core::Result<std::string>::success(*idempotency.value());
}

core::Result<JsonDocument> parse_object_body(const Request& request) {
  if (request.body.empty()) {
    return core::Result<JsonDocument>::failure(
        invalid_request("request JSON body is required"));
  }
  JsonDocument document;
  const auto parsed = deserializeJson(
      document,
      request.body,
      DeserializationOption::NestingLimit(maximum_json_nesting));
  if (parsed || !document.is<JsonObjectConst>()) {
    return core::Result<JsonDocument>::failure(
        invalid_request("request body must be one valid JSON object"));
  }
  return core::Result<JsonDocument>::success(std::move(document));
}

bool has_key(JsonObjectConst object, const char* key) {
  for (const auto pair : object) {
    if (std::string_view(pair.key().c_str()) == key) return true;
  }
  return false;
}

bool keys_allowed(
    JsonObjectConst object,
    std::initializer_list<const char*> allowed) {
  for (const auto pair : object) {
    const std::string_view key(pair.key().c_str());
    const auto found = std::any_of(allowed.begin(), allowed.end(), [&](const char* item) {
      return key == item;
    });
    if (!found) return false;
  }
  return true;
}

bool safe_text_value(const std::string& value, bool multiline = false) {
  return std::none_of(value.begin(), value.end(), [&](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte == 0U || (byte < 0x20U &&
        !(multiline && (character == '\r' || character == '\n' || character == '\t')));
  });
}

bool valid_web_access_token(const std::string& value) {
  if (value.empty()) return true;
  if (value.size() < 16U || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') ||
        character == '-' || character == '.' ||
        character == '_' || character == '~';
  });
}

bool read_optional_string(
    JsonObjectConst object,
    const char* key,
    std::size_t maximum,
    std::optional<std::string>& output,
    bool allow_empty = true,
    bool multiline = false) {
  if (!has_key(object, key)) return true;
  const auto value = object[key];
  if (!value.is<const char*>()) return false;
  std::string decoded(value.as<const char*>());
  if (decoded.size() > maximum || (!allow_empty && decoded.empty()) ||
      !safe_text_value(decoded, multiline)) {
    return false;
  }
  output = std::move(decoded);
  return true;
}

bool read_optional_bool(
    JsonObjectConst object,
    const char* key,
    std::optional<bool>& output) {
  if (!has_key(object, key)) return true;
  if (!object[key].is<bool>()) return false;
  output = object[key].as<bool>();
  return true;
}

template <typename T>
bool read_optional_unsigned(
    JsonObjectConst object,
    const char* key,
    std::uint64_t minimum,
    std::uint64_t maximum,
    std::optional<T>& output) {
  if (!has_key(object, key)) return true;
  if (!object[key].is<std::uint64_t>()) return false;
  const auto value = object[key].as<std::uint64_t>();
  if (value < minimum || value > maximum) return false;
  output = static_cast<T>(value);
  return true;
}

bool read_optional_float(
    JsonObjectConst object,
    const char* key,
    float minimum,
    float maximum,
    std::optional<float>& output) {
  if (!has_key(object, key)) return true;
  if (object[key].is<bool>() || !object[key].is<float>()) return false;
  const auto value = object[key].as<float>();
  if (!std::isfinite(value) || value < minimum || value > maximum) return false;
  output = value;
  return true;
}

bool valid_printer_state(const std::string& value) {
  static constexpr std::array<const char*, 10U> states = {{
      "unknown", "idle", "printing", "paused", "attention", "finished",
      "stopped", "error", "offline", "not_configured",
  }};
  return std::any_of(states.begin(), states.end(), [&](const char* state) {
    return value == state;
  });
}

core::Result<std::int32_t> parse_bounded_id(
    const std::string& value,
    std::int32_t maximum) {
  if (value.empty() || (value.size() > 1U && value.front() == '0') ||
      !std::all_of(value.begin(), value.end(), [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
      })) {
    return core::Result<std::int32_t>::failure(
        invalid_request("path ID must be a canonical decimal integer"));
  }
  std::uint64_t parsed = 0U;
  for (const auto character : value) {
    parsed = parsed * 10U + static_cast<std::uint64_t>(character - '0');
    if (parsed > static_cast<std::uint64_t>(maximum)) {
      return core::Result<std::int32_t>::failure(
          invalid_request("path ID is outside the supported range"));
    }
  }
  return core::Result<std::int32_t>::success(static_cast<std::int32_t>(parsed));
}

core::Result<std::uint64_t> parse_operation_id(const std::string& path) {
  constexpr std::string_view operation_prefix = "/api/v1/operations/";
  const auto value = path.substr(operation_prefix.size());
  if (value.empty() || value.front() == '0' ||
      !std::all_of(value.begin(), value.end(), [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
      })) {
    return core::Result<std::uint64_t>::failure(
        invalid_request("operation ID must be a positive canonical integer"));
  }
  std::uint64_t result = 0U;
  for (const auto character : value) {
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
      return core::Result<std::uint64_t>::failure(
          invalid_request("operation ID is outside the supported range"));
    }
    result = result * 10U + digit;
  }
  return core::Result<std::uint64_t>::success(result);
}

core::Result<ToolheadMutationPreconditions> parse_preconditions(
    JsonObjectConst object,
    bool require_current_spool) {
  ToolheadMutationPreconditions result;
  if (!has_key(object, "printer_id") ||
      !object["printer_id"].is<const char*>() ||
      !has_key(object, "expected_current_spool_id") ||
      !has_key(object, "expected_printer_state") ||
      !object["expected_printer_state"].is<const char*>() ||
      !has_key(object, "spool_generation") ||
      !object["spool_generation"].is<std::uint64_t>() ||
      !has_key(object, "printer_revision") ||
      !object["printer_revision"].is<std::uint64_t>() ||
      !has_key(object, "advanced_override") ||
      !object["advanced_override"].is<bool>()) {
    return core::Result<ToolheadMutationPreconditions>::failure(
        invalid_request("toolhead mutation preconditions are missing or invalid"));
  }
  result.printer_id = object["printer_id"].as<const char*>();
  result.expected_printer_state =
      object["expected_printer_state"].as<const char*>();
  if (result.printer_id.empty() || result.printer_id.size() > 128U ||
      !safe_text_value(result.printer_id) ||
      !valid_printer_state(result.expected_printer_state)) {
    return core::Result<ToolheadMutationPreconditions>::failure(
        invalid_request("toolhead printer identity or state is invalid"));
  }
  const auto current = object["expected_current_spool_id"];
  if (current.isNull()) {
    if (require_current_spool) {
      return core::Result<ToolheadMutationPreconditions>::failure(
          invalid_request("unassignment requires the expected current spool ID"));
    }
  } else if (!current.is<std::int32_t>() || current.as<std::int32_t>() <= 0) {
    return core::Result<ToolheadMutationPreconditions>::failure(
        invalid_request("expected current spool ID must be positive or null"));
  } else {
    result.expected_current_spool_id = current.as<std::int32_t>();
  }
  result.spool_generation = object["spool_generation"].as<std::uint64_t>();
  result.printer_revision = object["printer_revision"].as<std::uint64_t>();
  result.advanced_override = object["advanced_override"].as<bool>();
  return core::Result<ToolheadMutationPreconditions>::success(std::move(result));
}

core::Result<ConfigurationPatchMutation> parse_configuration_patch(
    JsonObjectConst root) {
  if (!keys_allowed(
          root,
          {"expected_revision", "device", "wifi", "web", "spoolman", "filabridge",
           "scale_profile", "toolheads", "reconciliation"}) ||
      !has_key(root, "expected_revision") ||
      !root["expected_revision"].is<std::uint64_t>()) {
    return core::Result<ConfigurationPatchMutation>::failure(
        invalid_request("configuration patch fields or expected revision are invalid"));
  }
  ConfigurationPatchMutation result;
  result.expected_revision = root["expected_revision"].as<std::uint64_t>();
  std::size_t changed_sections = 0U;

  if (has_key(root, "device")) {
    if (!root["device"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("device patch must be an object"));
    }
    const auto object = root["device"].as<JsonObjectConst>();
    DevicePatch patch;
    if (object.size() == 0U ||
        !keys_allowed(object, {"hostname", "brightness_percent", "dim_after_ms", "sleep_after_ms", "update_channel"}) ||
        !read_optional_string(object, "hostname", 63U, patch.hostname, false) ||
        !read_optional_unsigned(object, "brightness_percent", 5U, 100U, patch.brightness_percent) ||
        !read_optional_unsigned(object, "dim_after_ms", 1U, 86400000U, patch.dim_after_ms) ||
        !read_optional_unsigned(object, "sleep_after_ms", 1U, 86400000U, patch.sleep_after_ms) ||
        !read_optional_string(object, "update_channel", 16U, patch.update_channel, false)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("device patch contains invalid fields or values"));
    }
    if (patch.update_channel.has_value() && *patch.update_channel != "stable" &&
        *patch.update_channel != "beta" && *patch.update_channel != "development") {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("device update channel is invalid"));
    }
    result.device = std::move(patch);
    ++changed_sections;
  }

  if (has_key(root, "wifi")) {
    if (!root["wifi"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("Wi-Fi patch must be an object"));
    }
    const auto object = root["wifi"].as<JsonObjectConst>();
    WifiPatch patch;
    if (object.size() == 0U ||
        !keys_allowed(object, {"ssid", "password", "auto_reconnect", "connect_timeout_ms", "reconnect_initial_ms", "reconnect_max_ms"}) ||
        !read_optional_string(object, "ssid", 32U, patch.ssid) ||
        !read_optional_string(object, "password", 64U, patch.password) ||
        !read_optional_bool(object, "auto_reconnect", patch.auto_reconnect) ||
        !read_optional_unsigned(object, "connect_timeout_ms", 1000U, 60000U, patch.connect_timeout_ms) ||
        !read_optional_unsigned(object, "reconnect_initial_ms", 500U, 60000U, patch.reconnect_initial_ms) ||
        !read_optional_unsigned(object, "reconnect_max_ms", 500U, 600000U, patch.reconnect_max_ms)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("Wi-Fi patch contains invalid fields or values"));
    }
    result.wifi = std::move(patch);
    ++changed_sections;
  }

  if (has_key(root, "web")) {
    if (!root["web"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("web patch must be an object"));
    }
    const auto object = root["web"].as<JsonObjectConst>();
    WebPatch patch;
    if (object.size() == 0U ||
        !keys_allowed(object, {"access_token"}) ||
        !read_optional_string(
            object, "access_token", 128U, patch.access_token, true) ||
        !patch.access_token.has_value() ||
        !valid_web_access_token(*patch.access_token)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("web patch contains an invalid access token"));
    }
    result.web = std::move(patch);
    ++changed_sections;
  }

  if (has_key(root, "spoolman")) {
    if (!root["spoolman"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("Spoolman patch must be an object"));
    }
    const auto object = root["spoolman"].as<JsonObjectConst>();
    SpoolmanPatch patch;
    if (object.size() == 0U ||
        !keys_allowed(object, {"url", "authentication_token", "identity_field", "nfc_uid_field", "ca_certificate_pem"}) ||
        !read_optional_string(object, "url", 256U, patch.url) ||
        !read_optional_string(object, "authentication_token", 512U, patch.authentication_token) ||
        !read_optional_string(object, "identity_field", 64U, patch.identity_field, false) ||
        !read_optional_string(object, "nfc_uid_field", 64U, patch.nfc_uid_field, false) ||
        !read_optional_string(object, "ca_certificate_pem", 4096U, patch.ca_certificate_pem, true, true)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("Spoolman patch contains invalid fields or values"));
    }
    result.spoolman = std::move(patch);
    ++changed_sections;
  }

  if (has_key(root, "filabridge")) {
    if (!root["filabridge"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("FilaBridge patch must be an object"));
    }
    const auto object = root["filabridge"].as<JsonObjectConst>();
    FilaBridgePatch patch;
    if (object.size() == 0U ||
        !keys_allowed(object, {"url", "authentication_token", "selected_printer_id", "ca_certificate_pem"}) ||
        !read_optional_string(object, "url", 256U, patch.url) ||
        !read_optional_string(object, "authentication_token", 512U, patch.authentication_token) ||
        !read_optional_string(object, "selected_printer_id", 128U, patch.selected_printer_id) ||
        !read_optional_string(object, "ca_certificate_pem", 4096U, patch.ca_certificate_pem, true, true)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("FilaBridge patch contains invalid fields or values"));
    }
    result.filabridge = std::move(patch);
    ++changed_sections;
  }

  if (has_key(root, "scale_profile")) {
    if (!root["scale_profile"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("scale profile patch must be an object"));
    }
    const auto object = root["scale_profile"].as<JsonObjectConst>();
    ScaleProfilePatch patch;
    std::optional<std::string> preferred_model;
    if (object.size() == 0U ||
        !keys_allowed(object, {"id", "model", "load_cell_model", "rated_capacity_grams", "overload_ratio"}) ||
        !read_optional_string(object, "id", 32U, patch.id, false) ||
        !read_optional_string(object, "model", 32U, patch.model, false) ||
        !read_optional_string(object, "load_cell_model", 32U, preferred_model, false) ||
        !read_optional_unsigned(object, "rated_capacity_grams", 1U, 100000U, patch.rated_capacity_grams) ||
        !read_optional_float(object, "overload_ratio", 1.01F, 2.0F, patch.overload_ratio)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("scale profile patch contains invalid fields or values"));
    }
    if (preferred_model.has_value()) patch.model = std::move(preferred_model);
    if ((patch.id.has_value() && *patch.id != "yzc-133-2kg" &&
         *patch.id != "yzc-133-5kg") ||
        (patch.model.has_value() && *patch.model != "YZC-133") ||
        (patch.rated_capacity_grams.has_value() &&
         *patch.rated_capacity_grams != 2000U &&
         *patch.rated_capacity_grams != 5000U) ||
        (patch.id == std::optional<std::string>{"yzc-133-2kg"} &&
         patch.rated_capacity_grams.has_value() &&
         *patch.rated_capacity_grams != 2000U) ||
        (patch.id == std::optional<std::string>{"yzc-133-5kg"} &&
         patch.rated_capacity_grams.has_value() &&
         *patch.rated_capacity_grams != 5000U)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("scale profile supports only YZC-133 2 kg or 5 kg"));
    }
    result.scale_profile = std::move(patch);
    ++changed_sections;
  }

  if (has_key(root, "toolheads")) {
    if (!root["toolheads"].is<JsonArrayConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("toolheads patch must be an array"));
    }
    const auto array = root["toolheads"].as<JsonArrayConst>();
    if (array.size() > 8U) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("toolheads patch exceeds the profile limit"));
    }
    std::vector<ToolheadProfilePatch> profiles;
    std::set<std::int32_t> backend_ids;
    profiles.reserve(array.size());
    for (const auto value : array) {
      if (!value.is<JsonObjectConst>()) {
        return core::Result<ConfigurationPatchMutation>::failure(
            invalid_request("each toolhead profile must be an object"));
      }
      const auto object = value.as<JsonObjectConst>();
      if (!keys_allowed(object, {"backend_id", "display_name", "nozzle_diameter_mm", "enabled", "nozzle_material", "maximum_temperature_c", "notes"}) ||
          !has_key(object, "backend_id") || !object["backend_id"].is<std::int32_t>() ||
          !has_key(object, "display_name") || !object["display_name"].is<const char*>() ||
          !has_key(object, "nozzle_diameter_mm") || object["nozzle_diameter_mm"].is<bool>() || !object["nozzle_diameter_mm"].is<float>() ||
          !has_key(object, "enabled") || !object["enabled"].is<bool>() ||
          !has_key(object, "nozzle_material") || !object["nozzle_material"].is<const char*>() ||
          !has_key(object, "maximum_temperature_c") || !object["maximum_temperature_c"].is<std::uint16_t>()) {
        return core::Result<ConfigurationPatchMutation>::failure(
            invalid_request("toolhead profile fields or types are invalid"));
      }
      ToolheadProfilePatch profile;
      profile.backend_id = object["backend_id"].as<std::int32_t>();
      profile.display_name = object["display_name"].as<const char*>();
      profile.nozzle_diameter_mm = object["nozzle_diameter_mm"].as<float>();
      profile.enabled = object["enabled"].as<bool>();
      profile.nozzle_material = object["nozzle_material"].as<const char*>();
      profile.maximum_temperature_c =
          object["maximum_temperature_c"].as<std::uint16_t>();
      if (has_key(object, "notes")) {
        if (!object["notes"].is<const char*>()) {
          return core::Result<ConfigurationPatchMutation>::failure(
              invalid_request("toolhead notes must be text"));
        }
        profile.notes = object["notes"].as<const char*>();
      }
      if (profile.backend_id < 0 || profile.backend_id > 31 ||
          profile.display_name.empty() || profile.display_name.size() > 32U ||
          !safe_text_value(profile.display_name) ||
          !std::isfinite(profile.nozzle_diameter_mm) ||
          profile.nozzle_diameter_mm < 0.1F ||
          profile.nozzle_diameter_mm > 2.0F ||
          profile.nozzle_material.empty() ||
          profile.nozzle_material.size() > 32U ||
          !safe_text_value(profile.nozzle_material) ||
          profile.maximum_temperature_c < 100U ||
          profile.maximum_temperature_c > 500U ||
          profile.notes.size() > 256U || !safe_text_value(profile.notes) ||
          !backend_ids.insert(profile.backend_id).second) {
        return core::Result<ConfigurationPatchMutation>::failure(
            invalid_request("toolhead profile values are invalid or duplicated"));
      }
      profiles.push_back(std::move(profile));
    }
    result.toolheads = std::move(profiles);
    ++changed_sections;
  }

  if (has_key(root, "reconciliation")) {
    if (!root["reconciliation"].is<JsonObjectConst>()) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("reconciliation patch must be an object"));
    }
    const auto object = root["reconciliation"].as<JsonObjectConst>();
    ReconciliationPatch patch;
    if (object.size() == 0U ||
        !keys_allowed(object, {"normal_tolerance_grams", "warning_tolerance_grams"}) ||
        !read_optional_float(object, "normal_tolerance_grams", 0.0F, 1000.0F, patch.normal_tolerance_grams) ||
        !read_optional_float(object, "warning_tolerance_grams", 0.0F, 1000.0F, patch.warning_tolerance_grams) ||
        (patch.normal_tolerance_grams.has_value() &&
         patch.warning_tolerance_grams.has_value() &&
         *patch.warning_tolerance_grams < *patch.normal_tolerance_grams)) {
      return core::Result<ConfigurationPatchMutation>::failure(
          invalid_request("reconciliation patch contains invalid values"));
    }
    result.reconciliation = std::move(patch);
    ++changed_sections;
  }

  if (changed_sections == 0U) {
    return core::Result<ConfigurationPatchMutation>::failure(
        invalid_request("configuration patch does not contain any changes"));
  }
  return core::Result<ConfigurationPatchMutation>::success(std::move(result));
}

core::Result<Mutation> parse_mutation(
    const Request& request,
    const std::string& idempotency_key) {
  const auto parsed = parse_object_body(request);
  if (!parsed.ok()) return core::Result<Mutation>::failure(parsed.error());
  const auto object = parsed.value().as<JsonObjectConst>();
  Mutation mutation;
  mutation.idempotency_key = idempotency_key;

  if (request.path == "/api/v1/scale/tare" ||
      request.path == "/api/v1/nfc/read" ||
      request.path == "/api/v1/backends/test" ||
      request.path == "/api/v1/network/scan" ||
      request.path == "/api/v1/network/setup-mode") {
    if (object.size() != 0U) {
      return core::Result<Mutation>::failure(
          invalid_request("this operation requires an empty JSON object"));
    }
    if (request.path == "/api/v1/scale/tare") {
      mutation.kind = MutationKind::scale_tare;
    } else if (request.path == "/api/v1/nfc/read") {
      mutation.kind = MutationKind::nfc_read;
    } else if (request.path == "/api/v1/backends/test") {
      mutation.kind = MutationKind::backend_test;
    } else if (request.path == "/api/v1/network/scan") {
      mutation.kind = MutationKind::network_scan;
    } else {
      mutation.kind = MutationKind::network_setup_mode;
    }
    mutation.payload = EmptyMutation{};
    return core::Result<Mutation>::success(std::move(mutation));
  }

  if (request.path == "/api/v1/network/connect") {
    if (!keys_allowed(
            object,
            {"expected_revision", "ssid", "password", "hostname",
             "access_token"}) ||
        !has_key(object, "expected_revision") ||
        !object["expected_revision"].is<std::uint64_t>() ||
        !has_key(object, "ssid") || !object["ssid"].is<const char*>()) {
      return core::Result<Mutation>::failure(
          invalid_request("network connection fields or types are invalid"));
    }
    NetworkConnectMutation payload;
    payload.expected_revision =
        object["expected_revision"].as<std::uint64_t>();
    payload.ssid = object["ssid"].as<const char*>();
    if (payload.ssid.empty() || payload.ssid.size() > 32U ||
        !safe_text_value(payload.ssid) ||
        !read_optional_string(
            object, "password", 64U, payload.password) ||
        !read_optional_string(
            object, "hostname", 63U, payload.hostname, false) ||
        !read_optional_string(
            object, "access_token", 128U, payload.access_token, false) ||
        (payload.access_token.has_value() &&
         !valid_web_access_token(*payload.access_token))) {
      return core::Result<Mutation>::failure(
          invalid_request("network connection values are invalid"));
    }
    mutation.kind = MutationKind::network_connect;
    mutation.payload = std::move(payload);
    return core::Result<Mutation>::success(std::move(mutation));
  }

  if (request.path == "/api/v1/scale/calibrate") {
    if (!keys_allowed(object, {"reference_grams"}) || object.size() != 1U ||
        object["reference_grams"].is<bool>() ||
        !object["reference_grams"].is<float>()) {
      return core::Result<Mutation>::failure(
          invalid_request("calibration requires only numeric reference_grams"));
    }
    const auto reference = object["reference_grams"].as<float>();
    if (!std::isfinite(reference) || reference <= 0.0F || reference > 5000.0F) {
      return core::Result<Mutation>::failure(
          invalid_request("calibration reference must be between 0 and 5000 grams"));
    }
    mutation.kind = MutationKind::scale_calibration;
    mutation.payload = ScaleCalibrationMutation{reference};
    return core::Result<Mutation>::success(std::move(mutation));
  }

  if (request.path == "/api/v1/config") {
    const auto patch = parse_configuration_patch(object);
    if (!patch.ok()) return core::Result<Mutation>::failure(patch.error());
    mutation.kind = MutationKind::configuration_patch;
    mutation.payload = patch.value();
    return core::Result<Mutation>::success(std::move(mutation));
  }

  if (request.path == "/api/v1/update/reboot" ||
      request.path == "/api/v1/update/cancel") {
    if (!keys_allowed(
            object,
            {"upload_operation_id", "expected_generation",
             "expected_sha256", "confirmation"}) ||
        object.size() != 4U ||
        !object["expected_sha256"].is<const char*>() ||
        !object["confirmation"].is<const char*>()) {
      return core::Result<Mutation>::failure(invalid_request(
          "update control fields or types are invalid"));
    }

    UpdateControlMutation payload;
    if (!read_required_positive_uint64(
            object, "upload_operation_id", payload.upload_operation_id) ||
        !read_required_positive_uint64(
            object, "expected_generation", payload.expected_generation)) {
      return core::Result<Mutation>::failure(invalid_request(
          "update control operation and generation must be positive integers"));
    }
    payload.expected_sha256 = object["expected_sha256"].as<const char*>();
    payload.confirmation = object["confirmation"].as<const char*>();
    const bool reboot = request.path == "/api/v1/update/reboot";
    const char* expected_confirmation =
        reboot ? "REBOOT INTO UPDATE" : "CANCEL UPDATE";
    if (!valid_sha256_hex(payload.expected_sha256) ||
        payload.confirmation != expected_confirmation) {
      return core::Result<Mutation>::failure(invalid_request(
          "update control digest or confirmation is invalid"));
    }
    mutation.kind = reboot ? MutationKind::update_reboot
                           : MutationKind::update_cancel;
    mutation.payload = std::move(payload);
    return core::Result<Mutation>::success(std::move(mutation));
  }

  if (request.path == "/api/v1/device/reboot" ||
      request.path == "/api/v1/device/factory-reset") {
    if (!keys_allowed(object, {"confirmation"}) || object.size() != 1U ||
        !object["confirmation"].is<const char*>()) {
      return core::Result<Mutation>::failure(
          invalid_request("device control requires one confirmation string"));
    }
    const std::string confirmation = object["confirmation"].as<const char*>();
    const bool reboot = request.path == "/api/v1/device/reboot";
    const std::string expected = reboot ? "REBOOT" : "FACTORY RESET";
    if (confirmation != expected) {
      return core::Result<Mutation>::failure(
          invalid_request("device control confirmation did not match"));
    }
    mutation.kind = reboot ? MutationKind::reboot : MutationKind::factory_reset;
    mutation.payload = DeviceControlMutation{confirmation};
    return core::Result<Mutation>::success(std::move(mutation));
  }

  constexpr std::string_view toolhead_prefix = "/api/v1/toolheads/";
  const bool assignment = request.path.size() > 7U &&
      request.path.compare(request.path.size() - 7U, 7U, "/assign") == 0;
  const auto suffix_size = assignment ? 7U : 9U;
  const auto id_text = request.path.substr(
      toolhead_prefix.size(),
      request.path.size() - toolhead_prefix.size() - suffix_size);
  const auto toolhead_id = parse_bounded_id(id_text, 4);
  if (!toolhead_id.ok()) {
    return core::Result<Mutation>::failure(toolhead_id.error());
  }
  if (assignment) {
    if (!keys_allowed(
            object,
            {"printer_id", "expected_spool_id", "expected_current_spool_id",
             "expected_printer_state", "spool_generation", "printer_revision",
             "replace_occupied_confirmed", "advanced_override"}) ||
        object.size() != 8U || !object["expected_spool_id"].is<std::int32_t>() ||
        object["expected_spool_id"].as<std::int32_t>() <= 0 ||
        !object["replace_occupied_confirmed"].is<bool>()) {
      return core::Result<Mutation>::failure(
          invalid_request("assignment fields or types are invalid"));
    }
    const auto preconditions = parse_preconditions(object, false);
    if (!preconditions.ok()) {
      return core::Result<Mutation>::failure(preconditions.error());
    }
    ToolheadAssignmentMutation payload;
    payload.backend_toolhead_id = toolhead_id.value();
    payload.expected_spool_id = object["expected_spool_id"].as<std::int32_t>();
    payload.preconditions = preconditions.value();
    payload.replace_occupied_confirmed =
        object["replace_occupied_confirmed"].as<bool>();
    mutation.kind = MutationKind::toolhead_assignment;
    mutation.payload = std::move(payload);
  } else {
    if (!keys_allowed(
            object,
            {"printer_id", "expected_current_spool_id", "expected_printer_state",
             "spool_generation", "printer_revision", "advanced_override"}) ||
        object.size() != 6U) {
      return core::Result<Mutation>::failure(
          invalid_request("unassignment fields or types are invalid"));
    }
    const auto preconditions = parse_preconditions(object, true);
    if (!preconditions.ok()) {
      return core::Result<Mutation>::failure(preconditions.error());
    }
    ToolheadUnassignmentMutation payload;
    payload.backend_toolhead_id = toolhead_id.value();
    payload.preconditions = preconditions.value();
    mutation.kind = MutationKind::toolhead_unassignment;
    mutation.payload = std::move(payload);
  }
  return core::Result<Mutation>::success(std::move(mutation));
}

std::optional<Resource> resource_for_path(const std::string& path) {
  if (path == "/api/v1/status") return Resource::status;
  if (path == "/api/v1/device") return Resource::device;
  if (path == "/api/v1/health") return Resource::health;
  if (path == "/api/v1/network") return Resource::network;
  if (path == "/api/v1/scale") return Resource::scale;
  if (path == "/api/v1/nfc") return Resource::nfc;
  if (path == "/api/v1/nfc/tag") return Resource::nfc_tag;
  if (path == "/api/v1/spool") return Resource::spool;
  if (path == "/api/v1/printers") return Resource::printers;
  if (path == "/api/v1/toolheads") return Resource::toolheads;
  if (path == "/api/v1/config") return Resource::redacted_configuration;
  if (path == "/api/v1/diagnostics") return Resource::diagnostics;
  if (path == "/api/v1/logs") return Resource::logs;
  if (path == "/api/v1/update") return Resource::update;
  return std::nullopt;
}

}  // namespace

Response response_for_context_error(const core::Error& error) {
  return context_error(error);
}

bool valid_sha256_hex(std::string_view value) {
  return value.size() == 64U &&
      std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
      });
}

bool parse_canonical_generation(
    std::string_view value,
    std::uint64_t& generation) {
  if (value.empty() || value.size() > 20U ||
      (value.size() > 1U && value.front() == '0')) {
    return false;
  }
  std::uint64_t decoded = 0U;
  for (const auto character : value) {
    if (character < '0' || character > '9') return false;
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (decoded >
        (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
      return false;
    }
    decoded = decoded * 10U + digit;
  }
  generation = decoded;
  return true;
}

const char* to_string(Method method) {
  switch (method) {
    case Method::get: return "GET";
    case Method::post: return "POST";
    case Method::patch: return "PATCH";
    case Method::put: return "PUT";
    case Method::delete_method: return "DELETE";
    case Method::head: return "HEAD";
    case Method::options: return "OPTIONS";
    case Method::unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

Response Router::handle(const Request& request) {
  // Firmware upload is declared in the route catalog so method discovery and
  // transport bounds remain centralized, but binary images must never enter
  // this buffered JSON router. LocalWebServer owns the exact streaming route.
  if (request.method == Method::post &&
      request.path == "/api/v1/update/upload") {
    return error_response(
        500,
        "streaming_transport_required",
        "Firmware images must use the dedicated streaming upload transport");
  }

  const auto shape = validate_request_shape(request);
  if (!shape.ok()) {
    const bool too_large = request.body.size() > maximum_request_body_bytes;
    return error_response(
        too_large ? 413 : 400,
        too_large ? "request_too_large" : "invalid_request",
        shape.error().message);
  }

  std::vector<const RouteMetadata*> matches;
  for (const auto& route : routes) {
    if (path_matches(route.path_pattern, request.path)) matches.push_back(&route);
  }
  if (matches.empty()) {
    const bool unsupported_version =
        request.path.rfind("/api/", 0U) == 0U &&
        request.path.rfind("/api/v1/", 0U) != 0U &&
        request.path != "/api/v1";
    return error_response(
        404,
        unsupported_version ? "unsupported_api_version" : "route_not_found",
        unsupported_version
            ? "Only the /api/v1 API version is supported"
            : "No API route matches this path");
  }
  const auto selected = std::find_if(
      matches.begin(), matches.end(), [&](const auto* route) {
        return route->method == request.method;
      });
  if (selected == matches.end()) {
    auto response = error_response(
        405, "method_not_allowed", "The route does not support this HTTP method");
    response.headers.push_back({"Allow", allowed_methods(matches)});
    return response;
  }
  const auto& route = **selected;
  if (!route.mutation && !request.body.empty()) {
    return error_response(
        400, "unexpected_body", "GET requests must not include a body");
  }
  if (request.body.size() > route.maximum_body_bytes) {
    return error_response(
        413, "request_too_large", "The request body exceeds the route limit");
  }

  if (route.mutation) {
    const auto authorization = unique_header(request, "Authorization", false);
    constexpr std::string_view bearer_prefix = "Bearer ";
    bool bearer_authorized = false;
    if (authorization.ok() && authorization.value().has_value()) {
      const std::string_view value(*authorization.value());
      if (value.size() > bearer_prefix.size() &&
          value.compare(0U, bearer_prefix.size(), bearer_prefix) == 0) {
        const auto token = value.substr(bearer_prefix.size());
        bearer_authorized =
            token.find_first_of(" \t") == std::string_view::npos &&
            context_.authorize_mutation(token);
      }
    }
    const bool provisioning_route =
        request.path == "/api/v1/network/scan" ||
        request.path == "/api/v1/network/connect";
    const bool provisioning_authorized =
        provisioning_route && request.provisioning_transport &&
        context_.authorize_provisioning();
    if (!bearer_authorized && !provisioning_authorized) {
      return authentication_required();
    }
  }

  if (request.method == Method::get &&
      request.path.rfind("/api/v1/operations/", 0U) == 0U) {
    const auto id = parse_operation_id(request.path);
    if (!id.ok()) {
      return error_response(400, "invalid_operation_id", id.error().message);
    }
    const auto operation = context_.operation_status_json(id.value());
    if (!operation.ok()) return context_error(operation.error());
    if (!operation.value().has_value()) {
      return error_response(404, "operation_not_found", "The operation is no longer available");
    }
    return payload_response(*operation.value());
  }

  if (!route.mutation) {
    const auto resource = resource_for_path(request.path);
    if (!resource.has_value()) {
      return error_response(500, "internal_route_error", "The API route is not mapped");
    }
    const auto snapshot = context_.snapshot_json(*resource);
    if (!snapshot.ok()) return context_error(snapshot.error());
    return payload_response(
        snapshot.value(), *resource == Resource::redacted_configuration);
  }

  const auto headers = validate_mutation_headers(request);
  if (!headers.ok()) {
    return error_response(400, "invalid_request_headers", headers.error().message);
  }
  const auto mutation = parse_mutation(request, headers.value());
  if (!mutation.ok()) {
    return error_response(400, "invalid_request", mutation.error().message);
  }
  auto command = mutation.value();
  command.provisioning_transport = request.provisioning_transport;
  command.payload_digest = mutation_payload_digest(
      command.kind, request.path, request.body);
  const auto submitted = context_.submit(command);
  if (!submitted.ok()) return context_error(submitted.error());
  if (submitted.value().operation_id == 0U) {
    return error_response(
        503, "operation_not_queued", "The operation queue did not accept the request", true);
  }
  return operation_response(
      mutation.value().kind, submitted.value().operation_id);
}

}  // namespace opentag::web::api

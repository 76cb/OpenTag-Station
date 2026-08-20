#include "integrations/filabridge/filabridge_adapter.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace opentag::integrations::filabridge {
namespace {

constexpr std::size_t maximum_json_bytes = 65536U;
constexpr std::size_t maximum_printers = 16U;
constexpr std::size_t maximum_toolheads = 10U;
constexpr const char* tested_version = "v1.2.2";

core::Error configuration_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

core::Error contract_error(const std::string& message) {
  return {core::ErrorCategory::api_changed, message, false};
}

core::Error unavailable_capability(const std::string& operation) {
  return {core::ErrorCategory::api_changed,
          "FilaBridge " + operation + " is disabled until its contract is verified",
          false};
}

core::Error response_error(std::int32_t status) {
  if (status == 401 || status == 403) {
    return {core::ErrorCategory::authentication,
            "FilaBridge authentication was rejected",
            false};
  }
  if (status == 409) {
    return {core::ErrorCategory::invalid_response,
            "FilaBridge rejected a conflicting assignment",
            false};
  }
  if (status >= 500) {
    return {core::ErrorCategory::backend_unavailable,
            "FilaBridge returned HTTP " + std::to_string(status),
            true};
  }
  if (status == 404) {
    return {core::ErrorCategory::api_changed,
            "FilaBridge endpoint or resource was not found",
            false};
  }
  return {core::ErrorCategory::invalid_response,
          "FilaBridge returned HTTP " + std::to_string(status),
          false};
}

core::Result<JsonDocument> parse_json(
    const std::string& body,
    const char* context) {
  if (body.empty() || body.size() > maximum_json_bytes) {
    return core::Result<JsonDocument>::failure(
        contract_error(std::string(context) + " response size is invalid"));
  }
  JsonDocument document;
  if (deserializeJson(document, body)) {
    return core::Result<JsonDocument>::failure(
        contract_error(std::string(context) + " returned invalid JSON"));
  }
  return core::Result<JsonDocument>::success(std::move(document));
}

domain::PrinterState parse_state(const std::string& value) {
  if (value == "IDLE") return domain::PrinterState::idle;
  if (value == "PRINTING") return domain::PrinterState::printing;
  if (value == "PAUSED") return domain::PrinterState::paused;
  if (value == "ATTENTION") return domain::PrinterState::attention;
  if (value == "FINISHED") return domain::PrinterState::finished;
  if (value == "STOPPED") return domain::PrinterState::stopped;
  if (value == "ERROR") return domain::PrinterState::error;
  if (value == "offline") return domain::PrinterState::offline;
  if (value == "not_configured") return domain::PrinterState::not_configured;
  return domain::PrinterState::unknown;
}

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.size() <= 128U &&
      std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20U && byte != 0x7FU;
      });
}

std::optional<int> parse_object_index(const char* key) {
  if (key == nullptr || *key == '\0') return std::nullopt;
  int result = 0;
  for (const auto* cursor = key; *cursor != '\0'; ++cursor) {
    if (!std::isdigit(static_cast<unsigned char>(*cursor))) return std::nullopt;
    result = result * 10 + (*cursor - '0');
    if (result >= static_cast<int>(maximum_toolheads)) return std::nullopt;
  }
  return result;
}

}  // namespace

void FilaBridgeAdapter::configure(config::FilaBridgeSettings settings) {
  settings_ = std::move(settings);
  status_ = {};
}

std::string FilaBridgeAdapter::endpoint(const std::string& path) const {
  auto base = settings_.url;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base + path;
}

core::Result<network::HttpResponse> FilaBridgeAdapter::request(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    std::size_t maximum_response_bytes) {
  if (settings_.url.empty()) {
    return core::Result<network::HttpResponse>::failure(
        configuration_error("FilaBridge URL is not configured"));
  }
  network::HttpRequest request;
  request.method = method;
  request.url = endpoint(path);
  request.body = body;
  request.ca_certificate_pem = settings_.ca_certificate_pem;
  request.connect_timeout_ms = 5000U;
  request.read_timeout_ms = 7000U;
  request.maximum_response_bytes = maximum_response_bytes;
  request.headers.emplace_back("Accept", "application/json");
  if (!body.empty()) request.headers.emplace_back("Content-Type", "application/json");
  if (!settings_.authentication_token.empty()) {
    const bool prefixed =
        settings_.authentication_token.rfind("Basic ", 0U) == 0U ||
        settings_.authentication_token.rfind("Bearer ", 0U) == 0U;
    request.headers.emplace_back(
        "Authorization",
        prefixed ? settings_.authentication_token
                 : "Bearer " + settings_.authentication_token);
  }
  auto response = transport_.perform(request);
  if (!response.ok()) {
    status_.connected = false;
    status_.healthy = false;
    status_.last_error = response.error();
    return response;
  }
  status_.connected = true;
  if (response.value().status_code < 200 || response.value().status_code >= 300) {
    const auto error = response_error(response.value().status_code);
    status_.last_error = error;
    return core::Result<network::HttpResponse>::failure(error);
  }
  status_.last_error.reset();
  return response;
}

core::Result<FilaBridgeStatus> FilaBridgeAdapter::probe() {
  status_ = {};
  const auto health = request("GET", "/healthz", {}, 2048U);
  if (!health.ok()) return core::Result<FilaBridgeStatus>::failure(health.error());
  const auto parsed = parse_json(health.value().body, "FilaBridge health");
  if (!parsed.ok() || !parsed.value()["status"].is<const char*>() ||
      std::string(parsed.value()["status"].as<const char*>()) != "ok") {
    const auto error = parsed.ok()
                           ? contract_error("FilaBridge health response is unexpected")
                           : parsed.error();
    status_.last_error = error;
    return core::Result<FilaBridgeStatus>::failure(error);
  }
  status_.healthy = true;
  status_.capabilities.add(BackendCapability::health);
  if (parsed.value()["version"].is<const char*>()) {
    status_.version = parsed.value()["version"].as<const char*>();
    status_.version_formally_tested =
        status_.version == tested_version || status_.version == "1.2.2";
    status_.capabilities.add(BackendCapability::runtime_version);
  }

  const auto printers = read_printers();
  if (printers.ok()) {
    status_.capabilities.add(BackendCapability::get_printers);
    status_.capabilities.add(BackendCapability::get_toolheads);
    status_.capabilities.add(BackendCapability::get_print_state);
    status_.last_error.reset();
  } else {
    status_.last_error = printers.error();
  }
  if (printers.ok() &&
      (status_.version_formally_tested || status_.version == "dev")) {
    status_.capabilities.add(BackendCapability::map_toolhead);
    status_.capabilities.add(BackendCapability::unmap_toolhead);
  }
  return core::Result<FilaBridgeStatus>::success(status_);
}

core::Result<std::vector<domain::Printer>> FilaBridgeAdapter::read_printers() {
  const auto configured_response = request("GET", "/api/printers", {}, 32768U);
  if (!configured_response.ok()) {
    return core::Result<std::vector<domain::Printer>>::failure(
        configured_response.error());
  }
  const auto configured = parse_json(
      configured_response.value().body, "FilaBridge printers");
  if (!configured.ok()) {
    return core::Result<std::vector<domain::Printer>>::failure(configured.error());
  }
  if (!configured.value()["printers"].is<JsonObjectConst>()) {
    return core::Result<std::vector<domain::Printer>>::failure(
        contract_error("FilaBridge printers response is missing its map"));
  }
  const auto configurations = configured.value()["printers"].as<JsonObjectConst>();
  if (configurations.size() > maximum_printers) {
    return core::Result<std::vector<domain::Printer>>::failure(
        contract_error("FilaBridge returned too many printers"));
  }

  std::vector<domain::Printer> result;
  result.reserve(configurations.size());
  for (JsonPairConst entry : configurations) {
    if (!entry.value().is<JsonObjectConst>()) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge printer entry is not an object"));
    }
    const auto input = entry.value().as<JsonObjectConst>();
    std::string id = entry.key().c_str();
    if (!valid_identifier(id) || !input["name"].is<const char*>() ||
        !input["toolheads"].is<int>()) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge printer entry is incomplete"));
    }
    domain::Printer printer;
    printer.id = std::move(id);
    printer.display_name = input["name"].as<const char*>();
    const auto toolhead_count = input["toolheads"].as<int>();
    if (printer.display_name.empty() || printer.display_name.size() > 64U ||
        toolhead_count < 1 || toolhead_count > static_cast<int>(maximum_toolheads)) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge printer entry exceeds limits"));
    }
    JsonObjectConst names;
    if (input["toolhead_names"].is<JsonObjectConst>()) {
      names = input["toolhead_names"].as<JsonObjectConst>();
    }
    for (int id_value = 0; id_value < toolhead_count; ++id_value) {
      auto toolhead = domain::Toolhead::from_zero_based_backend(
          printer.id, id_value);
      const auto name_key = std::to_string(id_value);
      const auto name = names[name_key.c_str()];
      if (name.is<const char*>()) {
        const std::string display_name = name.as<const char*>();
        if (display_name.empty() || display_name.size() > 64U) {
          return core::Result<std::vector<domain::Printer>>::failure(
              contract_error("FilaBridge toolhead name exceeds limits"));
        }
        toolhead.display_name = display_name;
      }
      printer.toolheads.push_back(std::move(toolhead));
    }
    result.push_back(std::move(printer));
  }

  const auto status_response = request("GET", "/api/status", {}, maximum_json_bytes);
  if (!status_response.ok()) {
    return core::Result<std::vector<domain::Printer>>::failure(status_response.error());
  }
  const auto status = parse_json(status_response.value().body, "FilaBridge status");
  if (!status.ok()) {
    return core::Result<std::vector<domain::Printer>>::failure(status.error());
  }
  if (!status.value()["printers"].is<JsonObjectConst>() ||
      !status.value()["toolhead_mappings"].is<JsonObjectConst>()) {
    return core::Result<std::vector<domain::Printer>>::failure(
        contract_error("FilaBridge status response is incomplete"));
  }
  const auto states = status.value()["printers"].as<JsonObjectConst>();
  const auto all_mappings = status.value()["toolhead_mappings"].as<JsonObjectConst>();
  for (auto& printer : result) {
    const auto state_value = states[printer.id];
    if (!state_value.is<JsonObjectConst>()) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge status is missing a configured printer"));
    }
    const auto state_object = state_value.as<JsonObjectConst>();
    if (!state_object["state"].is<const char*>()) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge printer state is not text"));
    }
    printer.raw_state = state_object["state"].as<const char*>();
    if (printer.raw_state.size() > 32U) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge printer state exceeds limits"));
    }
    printer.state = parse_state(printer.raw_state);
    const auto printer_mappings = all_mappings[printer.id];
    if (!printer_mappings.is<JsonObjectConst>()) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge status is missing toolhead mappings"));
    }
    const auto mappings = printer_mappings.as<JsonObjectConst>();
    if (mappings.size() != printer.toolheads.size()) {
      return core::Result<std::vector<domain::Printer>>::failure(
          contract_error("FilaBridge did not return a complete toolhead mapping list"));
    }
    for (JsonPairConst mapping_entry : mappings) {
      const auto key_index = parse_object_index(mapping_entry.key().c_str());
      if (!key_index.has_value() ||
          !mapping_entry.value().is<JsonObjectConst>()) {
        return core::Result<std::vector<domain::Printer>>::failure(
            contract_error("FilaBridge toolhead mapping key is invalid"));
      }
      const auto mapping = mapping_entry.value().as<JsonObjectConst>();
      if (!mapping["printer_name"].is<const char*>() ||
          std::string(mapping["printer_name"].as<const char*>()) !=
              printer.display_name ||
          !mapping["toolhead_id"].is<int>() ||
          !mapping["spool_id"].is<std::int32_t>() ||
          mapping["toolhead_id"].as<int>() != *key_index ||
          mapping["spool_id"].as<std::int32_t>() < 0 ||
          *key_index >= static_cast<int>(printer.toolheads.size())) {
        return core::Result<std::vector<domain::Printer>>::failure(
            contract_error("FilaBridge toolhead mapping is invalid"));
      }
      auto& toolhead = printer.toolheads[static_cast<std::size_t>(*key_index)];
      const auto spool_id = mapping["spool_id"].as<std::int32_t>();
      if (spool_id > 0) toolhead.assigned_spool = spool_id;
      if (mapping["display_name"].is<const char*>()) {
        const std::string display_name = mapping["display_name"].as<const char*>();
        if (!display_name.empty() && display_name.size() <= 64U) {
          toolhead.display_name = display_name;
        }
      }
    }
  }
  return core::Result<std::vector<domain::Printer>>::success(std::move(result));
}

core::Result<std::vector<domain::Printer>> FilaBridgeAdapter::list_printers() {
  auto result = read_printers();
  if (!result.ok()) status_.last_error = result.error();
  return result;
}

core::Result<std::vector<domain::Toolhead>> FilaBridgeAdapter::get_toolheads(
    const std::string& printer_id) {
  if (!valid_identifier(printer_id)) {
    return core::Result<std::vector<domain::Toolhead>>::failure(
        configuration_error("FilaBridge printer ID is invalid"));
  }
  const auto printers = read_printers();
  if (!printers.ok()) {
    return core::Result<std::vector<domain::Toolhead>>::failure(printers.error());
  }
  const auto found = std::find_if(
      printers.value().begin(), printers.value().end(), [&](const auto& printer) {
        return printer.id == printer_id;
      });
  if (found == printers.value().end()) {
    return core::Result<std::vector<domain::Toolhead>>::failure(
        {core::ErrorCategory::invalid_response,
         "FilaBridge stable printer ID was not found",
         false});
  }
  return core::Result<std::vector<domain::Toolhead>>::success(found->toolheads);
}

core::Result<void> FilaBridgeAdapter::mutate_mapping(
    const std::string& printer_id,
    int backend_toolhead_id,
    domain::SpoolId spool_id) {
  if (!valid_identifier(printer_id) || backend_toolhead_id < 0 ||
      backend_toolhead_id >= static_cast<int>(maximum_toolheads) || spool_id < 0) {
    return core::Result<void>::failure(
        configuration_error("FilaBridge assignment request is invalid"));
  }
  const auto needed = spool_id == 0 ? BackendCapability::unmap_toolhead
                                    : BackendCapability::map_toolhead;
  if (!status_.capabilities.has(needed)) {
    return core::Result<void>::failure(
        unavailable_capability(spool_id == 0 ? "unassignment" : "assignment"));
  }
  const auto printers = read_printers();
  if (!printers.ok()) return core::Result<void>::failure(printers.error());
  const auto found = std::find_if(
      printers.value().begin(), printers.value().end(), [&](const auto& printer) {
        return printer.id == printer_id;
      });
  if (found == printers.value().end() ||
      backend_toolhead_id >= static_cast<int>(found->toolheads.size())) {
    return core::Result<void>::failure(
        configuration_error("FilaBridge printer or toolhead no longer exists"));
  }
  JsonDocument body;
  body["printer_name"] = found->display_name;
  body["toolhead_id"] = backend_toolhead_id;
  body["spool_id"] = spool_id;
  std::string serialized;
  serializeJson(body, serialized);
  const auto response = request("POST", "/api/map_toolhead", serialized, 4096U);
  if (!response.ok()) return core::Result<void>::failure(response.error());
  return core::Result<void>::success();
}

core::Result<void> FilaBridgeAdapter::assign_spool(
    const std::string& printer_id,
    int backend_toolhead_id,
    domain::SpoolId spool_id) {
  if (spool_id <= 0) {
    return core::Result<void>::failure(
        configuration_error("FilaBridge assigned spool ID must be positive"));
  }
  return mutate_mapping(printer_id, backend_toolhead_id, spool_id);
}

core::Result<void> FilaBridgeAdapter::unassign_spool(
    const std::string& printer_id,
    int backend_toolhead_id) {
  return mutate_mapping(printer_id, backend_toolhead_id, 0);
}

}  // namespace opentag::integrations::filabridge

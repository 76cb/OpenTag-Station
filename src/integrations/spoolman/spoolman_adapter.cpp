#include "integrations/spoolman/spoolman_adapter.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace opentag::integrations::spoolman {
namespace {

constexpr std::size_t maximum_json_bytes = 65536U;
constexpr std::size_t page_size = 64U;
constexpr std::size_t maximum_extra_fields = 64U;
constexpr const char* tested_version = "0.26.1";

core::Error configuration_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

core::Error contract_error(const std::string& message) {
  return {core::ErrorCategory::api_changed, message, false};
}

core::Error unavailable_capability(const std::string& operation) {
  return {core::ErrorCategory::api_changed,
          "Spoolman " + operation + " is disabled until its contract is verified",
          false};
}

core::Error response_error(std::int32_t status) {
  if (status == 401 || status == 403) {
    return {core::ErrorCategory::authentication,
            "Spoolman authentication was rejected",
            false};
  }
  if (status >= 500) {
    return {core::ErrorCategory::backend_unavailable,
            "Spoolman returned HTTP " + std::to_string(status),
            true};
  }
  if (status == 404) {
    return {core::ErrorCategory::api_changed,
            "Spoolman endpoint or resource was not found",
            false};
  }
  return {core::ErrorCategory::invalid_response,
          "Spoolman returned HTTP " + std::to_string(status),
          false};
}

bool valid_extra_key(const std::string& key) {
  return !key.empty() && key.size() <= 64U &&
      std::all_of(key.begin(), key.end(), [](char value) {
        return (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '_';
      });
}

bool valid_nonnegative(float value) {
  return std::isfinite(value) && value >= 0.0F;
}

std::string url_encode(const std::string& value) {
  std::ostringstream output;
  output << std::uppercase << std::hex;
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~') {
      output << character;
    } else {
      output << '%' << std::setw(2) << std::setfill('0')
             << static_cast<unsigned>(byte);
    }
  }
  return output.str();
}

core::Result<JsonDocument> parse_json(
    const std::string& body,
    const char* context) {
  if (body.empty() || body.size() > maximum_json_bytes) {
    return core::Result<JsonDocument>::failure(
        contract_error(std::string(context) + " response size is invalid"));
  }
  JsonDocument document;
  const auto parsed = deserializeJson(document, body);
  if (parsed) {
    return core::Result<JsonDocument>::failure(
        contract_error(std::string(context) + " returned invalid JSON"));
  }
  return core::Result<JsonDocument>::success(std::move(document));
}

std::optional<std::string> optional_text(
    JsonVariantConst value,
    std::size_t maximum) {
  if (value.isNull()) return std::nullopt;
  if (!value.is<const char*>()) return std::nullopt;
  std::string result = value.as<const char*>();
  return result.size() <= maximum
             ? std::optional<std::string>(std::move(result))
             : std::nullopt;
}

core::Result<std::optional<float>> optional_weight(
    JsonVariantConst value,
    const char* field) {
  if (value.isNull()) {
    return core::Result<std::optional<float>>::success(std::nullopt);
  }
  if (!value.is<float>()) {
    return core::Result<std::optional<float>>::failure(
        contract_error(std::string("Spoolman field ") + field + " is not numeric"));
  }
  const auto number = value.as<float>();
  if (!valid_nonnegative(number)) {
    return core::Result<std::optional<float>>::failure(
        contract_error(std::string("Spoolman field ") + field + " is invalid"));
  }
  return core::Result<std::optional<float>>::success(number);
}

std::optional<domain::Color> parse_color(JsonVariantConst value) {
  if (!value.is<const char*>()) return std::nullopt;
  std::string text = value.as<const char*>();
  if (text.size() != 6U && text.size() != 8U) return std::nullopt;
  const auto nibble = [](char character) -> int {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
  };
  const auto component = [&](std::size_t offset) -> std::optional<std::uint8_t> {
    const auto high = nibble(text[offset]);
    const auto low = nibble(text[offset + 1U]);
    if (high < 0 || low < 0) return std::nullopt;
    return static_cast<std::uint8_t>((high << 4) | low);
  };
  const auto red = component(0U);
  const auto green = component(2U);
  const auto blue = component(4U);
  const auto alpha = text.size() == 8U
                         ? component(6U)
                         : std::optional<std::uint8_t>(255U);
  if (!red || !green || !blue || !alpha) return std::nullopt;
  return domain::Color{*red, *green, *blue, *alpha};
}

std::optional<std::string> decode_json_string(const std::string& encoded) {
  JsonDocument decoded;
  if (deserializeJson(decoded, encoded) || !decoded.is<const char*>()) {
    return std::nullopt;
  }
  std::string value = decoded.as<const char*>();
  return !value.empty() && value.size() <= 128U
             ? std::optional<std::string>(std::move(value))
             : std::nullopt;
}

core::Result<domain::Spool> parse_spool_object(
    JsonObjectConst input,
    const config::SpoolmanSettings& settings) {
  if (!input["id"].is<std::int32_t>() ||
      input["id"].as<std::int32_t>() <= 0 ||
      !input["used_weight"].is<float>() ||
      !input["filament"].is<JsonObjectConst>()) {
    return core::Result<domain::Spool>::failure(
        contract_error("Spoolman spool is missing required fields"));
  }
  const auto filament = input["filament"].as<JsonObjectConst>();
  if (!filament["id"].is<std::int32_t>() ||
      filament["id"].as<std::int32_t>() <= 0) {
    return core::Result<domain::Spool>::failure(
        contract_error("Spoolman filament is missing its ID"));
  }

  domain::Spool result;
  result.id = input["id"].as<std::int32_t>();
  result.filament_id = filament["id"].as<std::int32_t>();
  result.used_grams = input["used_weight"].as<float>();
  if (!valid_nonnegative(result.used_grams)) {
    return core::Result<domain::Spool>::failure(
        contract_error("Spoolman used_weight is invalid"));
  }

  const auto remaining = optional_weight(input["remaining_weight"], "remaining_weight");
  const auto initial = optional_weight(input["initial_weight"], "initial_weight");
  const auto spool_empty = optional_weight(input["spool_weight"], "spool_weight");
  const auto package_empty = optional_weight(filament["spool_weight"], "filament.spool_weight");
  if (!remaining.ok()) return core::Result<domain::Spool>::failure(remaining.error());
  if (!initial.ok()) return core::Result<domain::Spool>::failure(initial.error());
  if (!spool_empty.ok()) return core::Result<domain::Spool>::failure(spool_empty.error());
  if (!package_empty.ok()) return core::Result<domain::Spool>::failure(package_empty.error());
  result.remaining_grams = remaining.value();
  result.initial_grams = initial.value();
  result.empty_spool_grams = spool_empty.value();
  result.package_empty_spool_grams = package_empty.value();

  const auto filament_name = optional_text(filament["name"], 64U);
  const auto material = optional_text(filament["material"], 64U);
  const auto article = optional_text(filament["article_number"], 64U);
  result.subtype = filament_name.value_or("");
  result.material = material.value_or("");
  result.article_number = article;
  result.primary_color = parse_color(filament["color_hex"]);
  result.location = optional_text(input["location"], 64U);
  result.archived = input["archived"].is<bool>()
                        ? input["archived"].as<bool>()
                        : false;

  if (filament["vendor"].is<JsonObjectConst>()) {
    const auto vendor = filament["vendor"].as<JsonObjectConst>();
    result.vendor = optional_text(vendor["name"], 64U).value_or("");
    const auto vendor_empty = optional_weight(
        vendor["empty_spool_weight"], "vendor.empty_spool_weight");
    if (!vendor_empty.ok()) {
      return core::Result<domain::Spool>::failure(vendor_empty.error());
    }
    result.vendor_empty_spool_grams = vendor_empty.value();
  }
  if (!result.empty_spool_grams.has_value()) {
    result.empty_spool_grams = result.package_empty_spool_grams.has_value()
                                  ? result.package_empty_spool_grams
                                  : result.vendor_empty_spool_grams;
  }
  result.display_name = result.subtype.empty()
                            ? result.material.empty()
                                  ? "Spool #" + std::to_string(result.id)
                                  : result.material
                            : result.subtype;
  if (!result.vendor.empty()) result.display_name = result.vendor + " " + result.display_name;

  if (!input["extra"].isNull()) {
    if (!input["extra"].is<JsonObjectConst>()) {
      return core::Result<domain::Spool>::failure(
          contract_error("Spoolman extra fields are not an object"));
    }
    const auto extra = input["extra"].as<JsonObjectConst>();
    if (extra.size() > maximum_extra_fields) {
      return core::Result<domain::Spool>::failure(
          contract_error("Spoolman returned too many extra fields"));
    }
    for (JsonPairConst field : extra) {
      if (!field.value().is<const char*>()) {
        return core::Result<domain::Spool>::failure(
            contract_error("Spoolman extra field value is not JSON text"));
      }
      std::string key = field.key().c_str();
      std::string value = field.value().as<const char*>();
      if (!valid_extra_key(key) || value.size() > 1024U) {
        return core::Result<domain::Spool>::failure(
            contract_error("Spoolman extra field exceeds limits"));
      }
      result.extra_json.emplace(std::move(key), std::move(value));
    }
  }
  const auto identity = result.extra_json.find(settings.identity_field);
  if (identity != result.extra_json.end()) {
    result.openprinttag_instance_uuid = decode_json_string(identity->second);
  }
  const auto nfc = result.extra_json.find(settings.nfc_uid_field);
  if (nfc != result.extra_json.end()) result.nfc_uid = decode_json_string(nfc->second);
  return core::Result<domain::Spool>::success(std::move(result));
}

ExtraFieldKind parse_field_kind(const std::string& value) {
  if (value == "text") return ExtraFieldKind::text;
  if (value == "integer") return ExtraFieldKind::integer;
  if (value == "float") return ExtraFieldKind::float_number;
  if (value == "boolean") return ExtraFieldKind::boolean;
  if (value == "datetime") return ExtraFieldKind::datetime;
  if (value == "choice") return ExtraFieldKind::choice;
  return ExtraFieldKind::unknown;
}

}  // namespace

void SpoolmanAdapter::configure(config::SpoolmanSettings settings) {
  settings_ = std::move(settings);
  status_ = {};
}

std::string SpoolmanAdapter::endpoint(const std::string& path) const {
  auto base = settings_.url;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base + "/api/v1" + path;
}

core::Result<network::HttpResponse> SpoolmanAdapter::request(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    std::size_t maximum_response_bytes) {
  if (settings_.url.empty()) {
    return core::Result<network::HttpResponse>::failure(
        configuration_error("Spoolman URL is not configured"));
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
        prefixed
            ? settings_.authentication_token
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

core::Result<SpoolmanStatus> SpoolmanAdapter::probe() {
  status_ = {};
  auto health = request("GET", "/health", {}, 1024U);
  if (!health.ok()) return core::Result<SpoolmanStatus>::failure(health.error());
  auto health_json = parse_json(health.value().body, "Spoolman health");
  if (!health_json.ok() || !health_json.value()["status"].is<const char*>() ||
      std::string(health_json.value()["status"].as<const char*>()) != "healthy") {
    const auto error = health_json.ok()
                           ? contract_error("Spoolman health response is unexpected")
                           : health_json.error();
    status_.last_error = error;
    return core::Result<SpoolmanStatus>::failure(error);
  }
  status_.healthy = true;
  status_.capabilities.add(BackendCapability::health);

  auto info = request("GET", "/info", {}, 4096U);
  if (info.ok()) {
    auto info_json = parse_json(info.value().body, "Spoolman info");
    if (info_json.ok() && info_json.value()["version"].is<const char*>()) {
      status_.version = info_json.value()["version"].as<const char*>();
      status_.version_formally_tested = status_.version == tested_version;
      status_.capabilities.add(BackendCapability::runtime_version);
      if (info_json.value()["git_commit"].is<const char*>()) {
        status_.git_commit = info_json.value()["git_commit"].as<const char*>();
      }
    }
  }
  const auto reads = probe_read_capabilities();
  (void)reads;
  if (status_.version_formally_tested) {
    status_.capabilities.add(BackendCapability::get_spool);
    status_.capabilities.add(BackendCapability::update_remaining_weight);
    status_.capabilities.add(BackendCapability::create_spool);
    status_.capabilities.add(BackendCapability::update_extra_fields);
  }
  status_.last_error.reset();
  return core::Result<SpoolmanStatus>::success(status_);
}

core::Result<void> SpoolmanAdapter::probe_read_capabilities() {
  SpoolFilter filter;
  filter.maximum_results = 1U;
  const auto spools = find_spools(filter);
  if (spools.ok()) {
    status_.capabilities.add(BackendCapability::list_spools);
    status_.capabilities.add(BackendCapability::search_spools);
  }
  const auto locations = list_locations();
  if (locations.ok()) status_.capabilities.add(BackendCapability::list_locations);
  const auto fields = list_extra_fields();
  if (fields.ok()) status_.capabilities.add(BackendCapability::list_extra_fields);
  return core::Result<void>::success();
}

core::Result<std::vector<domain::Spool>> SpoolmanAdapter::parse_spool_list(
    const std::string& body) const {
  auto parsed = parse_json(body, "Spoolman spool list");
  if (!parsed.ok()) {
    return core::Result<std::vector<domain::Spool>>::failure(parsed.error());
  }
  if (!parsed.value().is<JsonArrayConst>()) {
    return core::Result<std::vector<domain::Spool>>::failure(
        contract_error("Spoolman spool list is not an array"));
  }
  const auto input = parsed.value().as<JsonArrayConst>();
  if (input.size() > page_size) {
    return core::Result<std::vector<domain::Spool>>::failure(
        contract_error("Spoolman spool page exceeds its requested limit"));
  }
  std::vector<domain::Spool> result;
  result.reserve(input.size());
  for (const auto value : input) {
    if (!value.is<JsonObjectConst>()) {
      return core::Result<std::vector<domain::Spool>>::failure(
          contract_error("Spoolman spool list contains a non-object"));
    }
    auto spool = parse_spool_object(value.as<JsonObjectConst>(), settings_);
    if (!spool.ok()) {
      return core::Result<std::vector<domain::Spool>>::failure(spool.error());
    }
    result.push_back(std::move(spool.value()));
  }
  return core::Result<std::vector<domain::Spool>>::success(std::move(result));
}

core::Result<domain::Spool> SpoolmanAdapter::parse_spool(
    const std::string& body) const {
  auto parsed = parse_json(body, "Spoolman spool");
  if (!parsed.ok()) return core::Result<domain::Spool>::failure(parsed.error());
  if (!parsed.value().is<JsonObjectConst>()) {
    return core::Result<domain::Spool>::failure(
        contract_error("Spoolman spool response is not an object"));
  }
  return parse_spool_object(parsed.value().as<JsonObjectConst>(), settings_);
}

core::Result<std::vector<domain::Spool>> SpoolmanAdapter::list_spools() {
  SpoolFilter filter;
  return find_spools(filter);
}

core::Result<std::vector<domain::Spool>> SpoolmanAdapter::find_spools(
    const SpoolFilter& filter) {
  if (filter.maximum_results == 0U || filter.maximum_results > 512U) {
    return core::Result<std::vector<domain::Spool>>::failure(
        configuration_error("Spoolman result limit is invalid"));
  }
  const auto valid_filter = [](const std::optional<std::string>& value) {
    return !value.has_value() || value->size() <= 64U;
  };
  if (!valid_filter(filter.filament_name) || !valid_filter(filter.material) ||
      !valid_filter(filter.vendor_name) || !valid_filter(filter.location) ||
      filter.extra_json.size() > 8U) {
    return core::Result<std::vector<domain::Spool>>::failure(
        configuration_error("Spoolman filter exceeds limits"));
  }

  std::string common = "?allow_archived=";
  common += filter.allow_archived ? "true" : "false";
  const auto add = [&](const char* name, const std::optional<std::string>& value) {
    if (value.has_value()) common += "&" + std::string(name) + "=" + url_encode(*value);
  };
  add("filament.name", filter.filament_name);
  add("filament.material", filter.material);
  add("filament.vendor.name", filter.vendor_name);
  add("location", filter.location);
  for (const auto& field : filter.extra_json) {
    if (!valid_extra_key(field.first) || field.second.size() > 1024U) {
      return core::Result<std::vector<domain::Spool>>::failure(
          configuration_error("Spoolman extra-field filter is invalid"));
    }
    common += "&extra." + field.first + "=" + url_encode(field.second);
  }

  std::vector<domain::Spool> result;
  while (result.size() < filter.maximum_results) {
    const auto limit = std::min(page_size, filter.maximum_results - result.size());
    const auto path = "/spool" + common + "&limit=" +
        std::to_string(limit) + "&offset=" + std::to_string(result.size());
    auto response = request("GET", path, {}, maximum_json_bytes);
    if (!response.ok()) {
      return core::Result<std::vector<domain::Spool>>::failure(response.error());
    }
    auto page = parse_spool_list(response.value().body);
    if (!page.ok()) {
      status_.last_error = page.error();
      return page;
    }
    const auto count = page.value().size();
    for (auto& spool : page.value()) result.push_back(std::move(spool));
    if (count < limit) break;
  }
  return core::Result<std::vector<domain::Spool>>::success(std::move(result));
}

core::Result<domain::Spool> SpoolmanAdapter::get_spool(domain::SpoolId id) {
  if (id <= 0) {
    return core::Result<domain::Spool>::failure(
        configuration_error("Spoolman spool ID is invalid"));
  }
  auto response = request("GET", "/spool/" + std::to_string(id));
  if (!response.ok()) return core::Result<domain::Spool>::failure(response.error());
  auto result = parse_spool(response.value().body);
  if (!result.ok()) status_.last_error = result.error();
  return result;
}

core::Result<domain::Spool> SpoolmanAdapter::create_spool(
    const CreateSpoolRequest& request_value) {
  if (!status_.capabilities.has(BackendCapability::create_spool)) {
    return core::Result<domain::Spool>::failure(
        unavailable_capability("spool creation"));
  }
  if (request_value.filament_id <= 0 ||
      (request_value.initial_grams.has_value() &&
       !valid_nonnegative(*request_value.initial_grams)) ||
      (request_value.remaining_grams.has_value() &&
       !valid_nonnegative(*request_value.remaining_grams)) ||
      (request_value.empty_spool_grams.has_value() &&
       !valid_nonnegative(*request_value.empty_spool_grams)) ||
      (request_value.location.has_value() && request_value.location->size() > 64U) ||
      request_value.extra_json.size() > maximum_extra_fields) {
    return core::Result<domain::Spool>::failure(
        configuration_error("Spoolman create-spool request is invalid"));
  }
  JsonDocument body;
  body["filament_id"] = request_value.filament_id;
  if (request_value.initial_grams) body["initial_weight"] = *request_value.initial_grams;
  if (request_value.remaining_grams) body["remaining_weight"] = *request_value.remaining_grams;
  if (request_value.empty_spool_grams) body["spool_weight"] = *request_value.empty_spool_grams;
  if (request_value.location) body["location"] = *request_value.location;
  auto extra = body["extra"].to<JsonObject>();
  for (const auto& field : request_value.extra_json) {
    if (!valid_extra_key(field.first) || field.second.size() > 1024U) {
      return core::Result<domain::Spool>::failure(
          configuration_error("Spoolman create-spool extra field is invalid"));
    }
    extra[field.first] = field.second;
  }
  std::string serialized;
  serializeJson(body, serialized);
  auto response = request("POST", "/spool", serialized);
  if (!response.ok()) return core::Result<domain::Spool>::failure(response.error());
  auto created = parse_spool(response.value().body);
  if (created.ok()) status_.capabilities.add(BackendCapability::create_spool);
  return created;
}

core::Result<domain::Spool> SpoolmanAdapter::set_remaining_weight(
    domain::SpoolId id,
    const RemainingWeightUpdate& update) {
  if (!status_.capabilities.has(
          BackendCapability::update_remaining_weight)) {
    return core::Result<domain::Spool>::failure(
        unavailable_capability("remaining-weight updates"));
  }
  if (id <= 0 || !valid_nonnegative(update.expected_used_grams) ||
      !valid_nonnegative(update.remaining_grams) ||
      !valid_nonnegative(update.concurrency_tolerance_grams) ||
      !valid_nonnegative(update.verification_tolerance_grams) ||
      update.concurrency_tolerance_grams > 10.0F ||
      update.verification_tolerance_grams > 10.0F) {
    return core::Result<domain::Spool>::failure(
        configuration_error("Spoolman weight update is invalid"));
  }
  const auto current = get_spool(id);
  if (!current.ok()) return current;
  if (std::fabs(current.value().used_grams - update.expected_used_grams) >
      update.concurrency_tolerance_grams) {
    return core::Result<domain::Spool>::failure(
        {core::ErrorCategory::invalid_response,
         "Spoolman usage changed after the reconciliation snapshot",
         false});
  }
  JsonDocument body;
  body["remaining_weight"] = update.remaining_grams;
  std::string serialized;
  serializeJson(body, serialized);
  auto response = request("PATCH", "/spool/" + std::to_string(id), serialized);
  if (!response.ok()) return core::Result<domain::Spool>::failure(response.error());

  const auto verified = get_spool(id);
  if (!verified.ok()) return verified;
  if (!verified.value().remaining_grams.has_value() ||
      std::fabs(*verified.value().remaining_grams - update.remaining_grams) >
          update.verification_tolerance_grams) {
    return core::Result<domain::Spool>::failure(
        {core::ErrorCategory::invalid_response,
         "Spoolman remaining-weight readback did not match",
         false});
  }
  status_.capabilities.add(BackendCapability::update_remaining_weight);
  return verified;
}

core::Result<std::vector<std::string>> SpoolmanAdapter::list_locations() {
  auto response = request("GET", "/location", {}, 8192U);
  if (!response.ok()) {
    return core::Result<std::vector<std::string>>::failure(response.error());
  }
  auto parsed = parse_json(response.value().body, "Spoolman locations");
  if (!parsed.ok()) {
    return core::Result<std::vector<std::string>>::failure(parsed.error());
  }
  if (!parsed.value().is<JsonArrayConst>() ||
      parsed.value().as<JsonArrayConst>().size() > 256U) {
    return core::Result<std::vector<std::string>>::failure(
        contract_error("Spoolman locations response is invalid"));
  }
  std::vector<std::string> result;
  for (const auto value : parsed.value().as<JsonArrayConst>()) {
    if (!value.is<const char*>()) {
      return core::Result<std::vector<std::string>>::failure(
          contract_error("Spoolman location is not text"));
    }
    std::string location = value.as<const char*>();
    if (location.empty() || location.size() > 64U) {
      return core::Result<std::vector<std::string>>::failure(
          contract_error("Spoolman location exceeds limits"));
    }
    result.push_back(std::move(location));
  }
  return core::Result<std::vector<std::string>>::success(std::move(result));
}

core::Result<std::vector<ExtraFieldDefinition>>
SpoolmanAdapter::list_extra_fields() {
  auto response = request("GET", "/field/spool", {}, 16384U);
  if (!response.ok()) {
    return core::Result<std::vector<ExtraFieldDefinition>>::failure(response.error());
  }
  auto parsed = parse_json(response.value().body, "Spoolman fields");
  if (!parsed.ok()) {
    return core::Result<std::vector<ExtraFieldDefinition>>::failure(parsed.error());
  }
  if (!parsed.value().is<JsonArrayConst>() ||
      parsed.value().as<JsonArrayConst>().size() > maximum_extra_fields) {
    return core::Result<std::vector<ExtraFieldDefinition>>::failure(
        contract_error("Spoolman extra-field response is invalid"));
  }
  std::vector<ExtraFieldDefinition> result;
  for (const auto value : parsed.value().as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) {
      return core::Result<std::vector<ExtraFieldDefinition>>::failure(
          contract_error("Spoolman field definition is not an object"));
    }
    const auto field = value.as<JsonObjectConst>();
    if (!field["key"].is<const char*>() || !field["name"].is<const char*>() ||
        !field["field_type"].is<const char*>()) {
      return core::Result<std::vector<ExtraFieldDefinition>>::failure(
          contract_error("Spoolman field definition is incomplete"));
    }
    ExtraFieldDefinition item;
    item.key = field["key"].as<const char*>();
    item.name = field["name"].as<const char*>();
    item.kind = parse_field_kind(field["field_type"].as<const char*>());
    item.multi_choice = field["multi_choice"].is<bool>()
                            ? field["multi_choice"].as<bool>()
                            : false;
    if (!valid_extra_key(item.key) || item.name.empty() || item.name.size() > 64U) {
      return core::Result<std::vector<ExtraFieldDefinition>>::failure(
          contract_error("Spoolman field definition exceeds limits"));
    }
    result.push_back(std::move(item));
  }
  return core::Result<std::vector<ExtraFieldDefinition>>::success(std::move(result));
}

core::Result<domain::Spool> SpoolmanAdapter::set_extra_field(
    domain::SpoolId id,
    const std::string& key,
    const std::optional<std::string>& json_encoded_value) {
  if (!status_.capabilities.has(BackendCapability::update_extra_fields)) {
    return core::Result<domain::Spool>::failure(
        unavailable_capability("extra-field updates"));
  }
  if (id <= 0 || !valid_extra_key(key) ||
      (json_encoded_value.has_value() && json_encoded_value->size() > 1024U)) {
    return core::Result<domain::Spool>::failure(
        configuration_error("Spoolman extra-field update is invalid"));
  }
  if (json_encoded_value.has_value()) {
    JsonDocument validation;
    if (deserializeJson(validation, *json_encoded_value)) {
      return core::Result<domain::Spool>::failure(
          configuration_error("Spoolman extra-field value is not valid JSON text"));
    }
  }
  JsonDocument body;
  auto extra = body["extra"].to<JsonObject>();
  if (json_encoded_value.has_value()) {
    extra[key] = *json_encoded_value;
  } else {
    extra[key] = nullptr;
  }
  std::string serialized;
  serializeJson(body, serialized);
  auto response = request("PATCH", "/spool/" + std::to_string(id), serialized);
  if (!response.ok()) return core::Result<domain::Spool>::failure(response.error());
  const auto verified = get_spool(id);
  if (!verified.ok()) return verified;
  const auto actual = verified.value().extra_json.find(key);
  const bool matches = json_encoded_value.has_value()
                           ? actual != verified.value().extra_json.end() &&
                               actual->second == *json_encoded_value
                           : actual == verified.value().extra_json.end();
  if (!matches) {
    return core::Result<domain::Spool>::failure(
        {core::ErrorCategory::invalid_response,
         "Spoolman extra-field readback did not match",
         false});
  }
  status_.capabilities.add(BackendCapability::update_extra_fields);
  return verified;
}

}  // namespace opentag::integrations::spoolman

#include "config/configuration_service.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "core/error.hpp"

namespace opentag::config {
namespace {

constexpr std::size_t maximum_document_bytes = 16384U;
constexpr std::uint32_t setup_step_mask = 0xFFU;

core::Error configuration_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

core::Error revision_conflict(
    std::uint64_t expected_revision,
    std::uint64_t current_revision) {
  return {
      core::ErrorCategory::conflict,
      "configuration revision conflict: expected " +
          std::to_string(expected_revision) + ", current " +
          std::to_string(current_revision),
      true};
}

bool valid_hostname(const std::string& value) {
  if (value.empty() || value.size() > 63U || value.front() == '-' ||
      value.back() == '-') {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9') || character == '-';
  });
}

bool valid_web_access_token(const std::string& value) {
  if (value.empty()) return true;
  if (value.size() < 16U || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '.' || character == '_' || character == '~';
  });
}

bool valid_url(const std::string& value) {
  if (value.empty()) return true;
  if (value.size() > 256U || value.find('@') != std::string::npos ||
      value.find('#') != std::string::npos ||
      std::any_of(value.begin(), value.end(), [](char character) {
        return static_cast<unsigned char>(character) < 0x20U || character == ' ';
      })) {
    return false;
  }
  const bool http = value.rfind("http://", 0U) == 0U;
  const bool https = value.rfind("https://", 0U) == 0U;
  const auto scheme_size = https ? 8U : 7U;
  if ((!http && !https) || value.size() <= scheme_size) return false;
  const auto authority_end = value.find_first_of("/?", scheme_size);
  const auto authority = value.substr(
      scheme_size,
      authority_end == std::string::npos
          ? std::string::npos
          : authority_end - scheme_size);
  if (authority.empty()) return false;
  const auto colon = authority.rfind(':');
  const auto host = colon == std::string::npos
                        ? authority
                        : authority.substr(0U, colon);
  if (host.empty() || host.size() > 253U || host.front() == '.' ||
      host.back() == '.' || host.front() == '-' || host.back() == '-' ||
      !std::all_of(host.begin(), host.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '.' || character == '-';
      })) {
    return false;
  }
  if (colon == std::string::npos) return true;
  const auto port = authority.substr(colon + 1U);
  if (port.empty() || port.size() > 5U ||
      !std::all_of(port.begin(), port.end(), [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
      })) {
    return false;
  }
  std::uint32_t numeric_port = 0U;
  for (const auto character : port) {
    numeric_port = numeric_port * 10U +
        static_cast<std::uint32_t>(character - '0');
  }
  return numeric_port > 0U && numeric_port <= 65535U;
}

std::string text_or(JsonVariantConst value, const std::string& fallback) {
  return value.is<const char*>()
             ? std::string(value.as<const char*>())
             : fallback;
}

template <typename T>
T number_or(JsonVariantConst value, T fallback) {
  return value.is<T>() ? value.as<T>() : fallback;
}

JsonObject object_at(JsonDocument& document, const char* key) {
  return document[key].is<JsonObject>()
             ? document[key].as<JsonObject>()
             : document[key].to<JsonObject>();
}

JsonObject object_at(JsonObject parent, const char* key) {
  return parent[key].is<JsonObject>()
             ? parent[key].as<JsonObject>()
             : parent[key].to<JsonObject>();
}

bool capacities_match(float left, float right) {
  return std::fabs(left - right) <= 0.01F;
}

void write_scale(JsonDocument& document, const Configuration& configuration) {
  auto profile = object_at(document, "scale_profile");
  profile["load_cell_model"] = configuration.scale_hardware.load_cell_model;
  profile["rated_capacity_grams"] =
      configuration.scale_hardware.rated_capacity_grams;
  profile["overload_ratio"] = configuration.scale_hardware.overload_ratio;

  if (!configuration.scale_calibration.has_value()) {
    document.remove("scale");
    return;
  }
  auto scale = object_at(document, "scale");
  scale["schema_version"] = configuration.scale_calibration->schema_version;
  scale["zero_offset_counts"] =
      configuration.scale_calibration->zero_offset_counts;
  scale["counts_per_gram"] = configuration.scale_calibration->counts_per_gram;
  scale["reference_grams"] = configuration.scale_calibration->reference_grams;
  scale["load_cell_capacity_grams"] =
      configuration.scale_calibration->load_cell_capacity_grams;
}

void write_known(JsonDocument& document, const Configuration& configuration) {
  document["schema_version"] = Configuration::current_schema;
  document["hardware_id"] = configuration.hardware_id;

  auto device = object_at(document, "device");
  device["hostname"] = configuration.device.hostname;
  device["brightness_percent"] = configuration.device.brightness_percent;
  device["dim_after_ms"] = configuration.device.dim_after_ms;
  device["sleep_after_ms"] = configuration.device.sleep_after_ms;
  device["update_channel"] = configuration.device.update_channel;

  auto wifi = object_at(document, "wifi");
  wifi["ssid"] = configuration.wifi.ssid;
  wifi["password"] = configuration.wifi.password;
  wifi["auto_reconnect"] = configuration.wifi.auto_reconnect;
  wifi["connect_timeout_ms"] = configuration.wifi.connect_timeout_ms;
  wifi["reconnect_initial_ms"] = configuration.wifi.reconnect_initial_ms;
  wifi["reconnect_max_ms"] = configuration.wifi.reconnect_max_ms;

  auto spoolman = object_at(document, "spoolman");
  spoolman["url"] = configuration.spoolman.url;
  spoolman["authentication_token"] = configuration.spoolman.authentication_token;
  spoolman["identity_field"] = configuration.spoolman.identity_field;
  spoolman["nfc_uid_field"] = configuration.spoolman.nfc_uid_field;
  spoolman["ca_certificate_pem"] = configuration.spoolman.ca_certificate_pem;

  auto filabridge = object_at(document, "filabridge");
  filabridge["url"] = configuration.filabridge.url;
  filabridge["authentication_token"] =
      configuration.filabridge.authentication_token;
  filabridge["selected_printer_id"] =
      configuration.filabridge.selected_printer_id;
  filabridge["ca_certificate_pem"] = configuration.filabridge.ca_certificate_pem;

  auto web = object_at(document, "web");
  web.remove("access_token_configured");
  web["access_token"] = configuration.web.access_token;

  write_scale(document, configuration);

  document.remove("toolheads");
  auto toolheads = document["toolheads"].to<JsonArray>();
  for (const auto& profile : configuration.toolheads) {
    auto output = toolheads.add<JsonObject>();
    output["backend_id"] = profile.backend_id;
    output["display_name"] = profile.display_name;
    output["nozzle_diameter_mm"] = profile.nozzle_diameter_mm;
    output["enabled"] = profile.enabled;
    output["nozzle_material"] = profile.nozzle_material;
    output["maximum_temperature_c"] = profile.maximum_temperature_c;
    output["notes"] = profile.notes;
  }

  document.remove("spool_identity_mappings");
  auto mappings = document["spool_identity_mappings"].to<JsonArray>();
  for (const auto& mapping : configuration.spool_identity_mappings) {
    auto output = mappings.add<JsonObject>();
    output["spool_id"] = mapping.spool_id;
    if (mapping.instance_uuid.has_value()) {
      output["instance_uuid"] = *mapping.instance_uuid;
    }
    if (mapping.nfc_uid.has_value()) output["nfc_uid"] = *mapping.nfc_uid;
  }

  auto reconciliation = object_at(document, "reconciliation");
  reconciliation["normal_tolerance_grams"] =
      configuration.reconciliation.normal_tolerance_grams;
  reconciliation["warning_tolerance_grams"] =
      configuration.reconciliation.warning_tolerance_grams;

  auto setup = object_at(document, "setup");
  setup["completed_steps"] = configuration.setup.completed_steps;
  setup["ready_confirmed"] = configuration.setup.ready_confirmed;
}

core::Result<void> migrate(JsonDocument& document, bool& migrated) {
  if (!document.is<JsonObject>()) {
    return core::Result<void>::failure(
        configuration_error("configuration root must be an object"));
  }
  auto schema = document["schema_version"].as<std::uint32_t>();
  if (schema < 1U || schema > Configuration::current_schema) {
    return core::Result<void>::failure(
        configuration_error("configuration schema is not supported"));
  }
  if (schema == 1U) {
    auto device = object_at(document, "device");
    if (!device["update_channel"].is<const char*>()) {
      device["update_channel"] = "stable";
    }
    auto wifi = object_at(document, "wifi");
    if (!wifi["auto_reconnect"].is<bool>()) wifi["auto_reconnect"] = true;
    if (!wifi["connect_timeout_ms"].is<std::uint32_t>()) {
      wifi["connect_timeout_ms"] = 15000U;
    }
    if (!wifi["reconnect_initial_ms"].is<std::uint32_t>()) {
      wifi["reconnect_initial_ms"] = 1000U;
    }
    if (!wifi["reconnect_max_ms"].is<std::uint32_t>()) {
      wifi["reconnect_max_ms"] = 60000U;
    }
    if (!document["toolheads"].is<JsonArray>()) {
      document["toolheads"].to<JsonArray>();
    }
    auto reconciliation = object_at(document, "reconciliation");
    if (!reconciliation["warning_tolerance_grams"].is<float>()) {
      const auto normal =
          number_or<float>(reconciliation["normal_tolerance_grams"], 5.0F);
      reconciliation["warning_tolerance_grams"] = std::max(normal, 20.0F);
    }
    object_at(document, "setup");
    document["schema_version"] = 2U;
    schema = 2U;
    migrated = true;
  }
  if (schema == 2U) {
    auto spoolman = object_at(document, "spoolman");
    if (!spoolman["nfc_uid_field"].is<const char*>()) {
      spoolman["nfc_uid_field"] = "nfc_uid";
    }
    if (!document["spool_identity_mappings"].is<JsonArray>()) {
      document["spool_identity_mappings"].to<JsonArray>();
    }
    document["schema_version"] = Configuration::current_schema;
    migrated = true;
  }
  return core::Result<void>::success();
}

core::Result<void> read_configuration(
    const JsonDocument& document,
    Configuration& result) {
  result = {};
  result.schema_version = document["schema_version"].as<std::uint32_t>();
  result.hardware_id = text_or(document["hardware_id"], result.hardware_id);

  const auto device = document["device"].as<JsonObjectConst>();
  result.device.hostname = text_or(device["hostname"], result.device.hostname);
  result.device.brightness_percent =
      number_or<std::uint8_t>(device["brightness_percent"], result.device.brightness_percent);
  result.device.dim_after_ms =
      number_or<std::uint32_t>(device["dim_after_ms"], result.device.dim_after_ms);
  result.device.sleep_after_ms =
      number_or<std::uint32_t>(device["sleep_after_ms"], result.device.sleep_after_ms);
  result.device.update_channel =
      text_or(device["update_channel"], result.device.update_channel);

  const auto wifi = document["wifi"].as<JsonObjectConst>();
  result.wifi.ssid = text_or(wifi["ssid"], result.wifi.ssid);
  result.wifi.password = text_or(wifi["password"], result.wifi.password);
  result.wifi.auto_reconnect = number_or<bool>(wifi["auto_reconnect"], true);
  result.wifi.connect_timeout_ms = number_or<std::uint32_t>(
      wifi["connect_timeout_ms"], result.wifi.connect_timeout_ms);
  result.wifi.reconnect_initial_ms = number_or<std::uint32_t>(
      wifi["reconnect_initial_ms"], result.wifi.reconnect_initial_ms);
  result.wifi.reconnect_max_ms = number_or<std::uint32_t>(
      wifi["reconnect_max_ms"], result.wifi.reconnect_max_ms);

  const auto spoolman = document["spoolman"].as<JsonObjectConst>();
  result.spoolman.url = text_or(spoolman["url"], result.spoolman.url);
  result.spoolman.authentication_token = text_or(
      spoolman["authentication_token"], result.spoolman.authentication_token);
  result.spoolman.identity_field =
      text_or(spoolman["identity_field"], result.spoolman.identity_field);
  result.spoolman.nfc_uid_field =
      text_or(spoolman["nfc_uid_field"], result.spoolman.nfc_uid_field);
  result.spoolman.ca_certificate_pem = text_or(
      spoolman["ca_certificate_pem"], result.spoolman.ca_certificate_pem);

  const auto filabridge = document["filabridge"].as<JsonObjectConst>();
  result.filabridge.url = text_or(filabridge["url"], result.filabridge.url);
  result.filabridge.authentication_token = text_or(
      filabridge["authentication_token"], result.filabridge.authentication_token);
  result.filabridge.selected_printer_id = text_or(
      filabridge["selected_printer_id"], result.filabridge.selected_printer_id);
  result.filabridge.ca_certificate_pem = text_or(
      filabridge["ca_certificate_pem"], result.filabridge.ca_certificate_pem);

  const auto web = document["web"].as<JsonObjectConst>();
  result.web.access_token =
      text_or(web["access_token"], result.web.access_token);

  const bool has_scale_profile =
      document["scale_profile"].is<JsonObjectConst>();
  if (has_scale_profile) {
    const auto profile = document["scale_profile"].as<JsonObjectConst>();
    result.scale_hardware.load_cell_model = text_or(
        profile["load_cell_model"], result.scale_hardware.load_cell_model);
    result.scale_hardware.rated_capacity_grams = number_or<float>(
        profile["rated_capacity_grams"],
        result.scale_hardware.rated_capacity_grams);
    result.scale_hardware.overload_ratio = number_or<float>(
        profile["overload_ratio"], result.scale_hardware.overload_ratio);
  }

  if (document["scale"].is<JsonObjectConst>()) {
    const auto scale = document["scale"].as<JsonObjectConst>();
    services::ScaleCalibration calibration;
    calibration.schema_version =
        number_or<std::uint32_t>(scale["schema_version"], 0U);
    calibration.zero_offset_counts =
        number_or<std::int32_t>(scale["zero_offset_counts"], 0);
    calibration.counts_per_gram =
        number_or<double>(scale["counts_per_gram"], 0.0);
    calibration.reference_grams =
        number_or<float>(scale["reference_grams"], 0.0F);
    calibration.load_cell_capacity_grams =
        number_or<float>(scale["load_cell_capacity_grams"], 0.0F);
    result.scale_calibration = calibration;
    if (!has_scale_profile) {
      result.scale_hardware.rated_capacity_grams =
          calibration.load_cell_capacity_grams;
    }
  }

  if (document["toolheads"].is<JsonArrayConst>()) {
    for (const auto value : document["toolheads"].as<JsonArrayConst>()) {
      if (!value.is<JsonObjectConst>()) {
        return core::Result<void>::failure(
            configuration_error("toolhead profile must be an object"));
      }
      const auto profile_json = value.as<JsonObjectConst>();
      ToolheadProfile profile;
      profile.backend_id = number_or<std::int32_t>(profile_json["backend_id"], -1);
      profile.display_name = text_or(profile_json["display_name"], "");
      profile.nozzle_diameter_mm = number_or<float>(
          profile_json["nozzle_diameter_mm"], profile.nozzle_diameter_mm);
      profile.enabled = number_or<bool>(profile_json["enabled"], true);
      profile.nozzle_material = text_or(
          profile_json["nozzle_material"], profile.nozzle_material);
      profile.maximum_temperature_c = number_or<std::uint16_t>(
          profile_json["maximum_temperature_c"],
          profile.maximum_temperature_c);
      profile.notes = text_or(profile_json["notes"], profile.notes);
      result.toolheads.push_back(std::move(profile));
    }
  }

  if (document["spool_identity_mappings"].is<JsonArrayConst>()) {
    const auto mappings = document["spool_identity_mappings"].as<JsonArrayConst>();
    if (mappings.size() > 64U) {
      return core::Result<void>::failure(
          configuration_error("too many spool identity mappings"));
    }
    for (const auto value : mappings) {
      if (!value.is<JsonObjectConst>()) {
        return core::Result<void>::failure(
            configuration_error("spool identity mapping must be an object"));
      }
      const auto input = value.as<JsonObjectConst>();
      domain::ConfirmedSpoolMapping mapping;
      mapping.spool_id = number_or<std::int32_t>(input["spool_id"], 0);
      if (input["instance_uuid"].is<const char*>()) {
        mapping.instance_uuid = input["instance_uuid"].as<const char*>();
      }
      if (input["nfc_uid"].is<const char*>()) {
        mapping.nfc_uid = input["nfc_uid"].as<const char*>();
      }
      result.spool_identity_mappings.push_back(std::move(mapping));
    }
  }

  const auto reconciliation = document["reconciliation"].as<JsonObjectConst>();
  result.reconciliation.normal_tolerance_grams = number_or<float>(
      reconciliation["normal_tolerance_grams"],
      result.reconciliation.normal_tolerance_grams);
  result.reconciliation.warning_tolerance_grams = number_or<float>(
      reconciliation["warning_tolerance_grams"],
      result.reconciliation.warning_tolerance_grams);

  const auto setup = document["setup"].as<JsonObjectConst>();
  result.setup.completed_steps =
      number_or<std::uint32_t>(setup["completed_steps"], 0U);
  result.setup.ready_confirmed =
      number_or<bool>(setup["ready_confirmed"], false);

  const auto valid = result.validate();
  if (!valid.ok()) return valid;
  return core::Result<void>::success();
}

core::Result<JsonDocument> parse_document(
    const std::string& input,
    bool& migrated,
    std::uint32_t& loaded_schema) {
  if (input.empty() || input.size() > maximum_document_bytes) {
    return core::Result<JsonDocument>::failure(
        configuration_error("configuration document size is invalid"));
  }
  JsonDocument document;
  const auto parsed = deserializeJson(document, input);
  if (parsed) {
    return core::Result<JsonDocument>::failure(
        configuration_error(std::string("configuration JSON is invalid: ") + parsed.c_str()));
  }
  loaded_schema = document["schema_version"].as<std::uint32_t>();
  const auto migration = migrate(document, migrated);
  if (!migration.ok()) {
    return core::Result<JsonDocument>::failure(migration.error());
  }
  return core::Result<JsonDocument>::success(std::move(document));
}

struct DecodedConfigurationDocument {
  JsonDocument document;
  Configuration configuration;
  bool migrated{false};
  std::uint32_t loaded_schema{0U};
};

core::Result<std::shared_ptr<DecodedConfigurationDocument>> decode_document(
    const std::string& input) {
  auto result = std::make_shared<DecodedConfigurationDocument>();
  auto parsed = parse_document(
      input, result->migrated, result->loaded_schema);
  if (!parsed.ok()) {
    return core::Result<std::shared_ptr<DecodedConfigurationDocument>>::failure(
        parsed.error());
  }
  const auto decoded = read_configuration(
      parsed.value(), result->configuration);
  if (!decoded.ok()) {
    return core::Result<std::shared_ptr<DecodedConfigurationDocument>>::failure(
        decoded.error());
  }
  result->document = std::move(parsed.value());
  return core::Result<std::shared_ptr<DecodedConfigurationDocument>>::success(
      std::move(result));
}

}  // namespace

struct ConfigurationService::Impl {
  JsonDocument document;
};

core::Result<void> Configuration::validate() const {
  if (schema_version != current_schema ||
      hardware_id != boards::Wt32Sc01PlusRevA::id) {
    return core::Result<void>::failure(
        configuration_error("configuration schema or hardware ID is incompatible"));
  }
  if (!valid_hostname(device.hostname) || device.brightness_percent < 5U ||
      device.brightness_percent > 100U || device.dim_after_ms == 0U ||
      device.sleep_after_ms < device.dim_after_ms ||
      (device.update_channel != "stable" && device.update_channel != "beta" &&
       device.update_channel != "development")) {
    return core::Result<void>::failure(
        configuration_error("device settings are invalid"));
  }
  if (wifi.ssid.size() > 32U || wifi.password.size() > 64U ||
      wifi.connect_timeout_ms < 1000U || wifi.connect_timeout_ms > 60000U ||
      wifi.reconnect_initial_ms < 500U || wifi.reconnect_initial_ms > 60000U ||
      wifi.reconnect_max_ms < wifi.reconnect_initial_ms ||
      wifi.reconnect_max_ms > 600000U) {
    return core::Result<void>::failure(
        configuration_error("Wi-Fi settings are invalid"));
  }
  if (!valid_url(spoolman.url) || !valid_url(filabridge.url) ||
      spoolman.authentication_token.size() > 512U ||
      filabridge.authentication_token.size() > 512U ||
      spoolman.ca_certificate_pem.size() > 4096U ||
      filabridge.ca_certificate_pem.size() > 4096U ||
      spoolman.identity_field.empty() || spoolman.identity_field.size() > 64U ||
      spoolman.nfc_uid_field.empty() || spoolman.nfc_uid_field.size() > 64U ||
      filabridge.selected_printer_id.size() > 128U) {
    return core::Result<void>::failure(
        configuration_error("backend settings are invalid"));
  }
  if (!valid_web_access_token(web.access_token)) {
    return core::Result<void>::failure(
        configuration_error("local web access token is invalid"));
  }
  const auto scale_hardware_valid = scale_hardware.validate();
  if (!scale_hardware_valid.ok()) return scale_hardware_valid;
  if (scale_calibration.has_value()) {
    const auto scale_valid = scale_calibration->validate();
    if (!scale_valid.ok()) return scale_valid;
    if (!capacities_match(
            scale_hardware.rated_capacity_grams,
            scale_calibration->load_cell_capacity_grams)) {
      return core::Result<void>::failure(configuration_error(
          "scale calibration capacity does not match hardware profile"));
    }
  }
  if (toolheads.size() > 8U) {
    return core::Result<void>::failure(
        configuration_error("too many toolhead profiles"));
  }
  std::set<std::int32_t> backend_ids;
  for (const auto& profile : toolheads) {
    if (profile.backend_id < 0 || profile.backend_id > 31 ||
        profile.display_name.empty() || profile.display_name.size() > 32U ||
        !std::isfinite(profile.nozzle_diameter_mm) ||
        profile.nozzle_diameter_mm < 0.1F || profile.nozzle_diameter_mm > 2.0F ||
        profile.nozzle_material.empty() || profile.nozzle_material.size() > 32U ||
        profile.maximum_temperature_c < 100U ||
        profile.maximum_temperature_c > 500U || profile.notes.size() > 256U ||
        !backend_ids.insert(profile.backend_id).second) {
      return core::Result<void>::failure(
          configuration_error("toolhead profile is invalid or duplicated"));
    }
  }
  if (spool_identity_mappings.size() > 64U) {
    return core::Result<void>::failure(
        configuration_error("too many spool identity mappings"));
  }
  std::set<std::string> instance_uuids;
  std::set<std::string> nfc_uids;
  for (const auto& mapping : spool_identity_mappings) {
    const bool instance_valid = mapping.instance_uuid.has_value() &&
        !mapping.instance_uuid->empty() && mapping.instance_uuid->size() <= 64U;
    const bool nfc_valid = mapping.nfc_uid.has_value() &&
        mapping.nfc_uid->size() == 16U &&
        std::all_of(mapping.nfc_uid->begin(), mapping.nfc_uid->end(), [](char value) {
          return std::isxdigit(static_cast<unsigned char>(value)) != 0;
        });
    if (mapping.spool_id <= 0 || (!instance_valid && !nfc_valid) ||
        (mapping.instance_uuid.has_value() && !instance_valid) ||
        (mapping.nfc_uid.has_value() && !nfc_valid) ||
        (mapping.instance_uuid.has_value() &&
         !instance_uuids.insert([&]() {
           auto value = *mapping.instance_uuid;
           std::transform(value.begin(), value.end(), value.begin(), [](char character) {
             return static_cast<char>(
                 std::tolower(static_cast<unsigned char>(character)));
           });
           return value;
         }()).second) ||
        (mapping.nfc_uid.has_value() &&
         !nfc_uids.insert([&]() {
           auto value = *mapping.nfc_uid;
           std::transform(value.begin(), value.end(), value.begin(), [](char character) {
             return static_cast<char>(
                 std::toupper(static_cast<unsigned char>(character)));
           });
           return value;
         }()).second)) {
      return core::Result<void>::failure(
          configuration_error("spool identity mapping is invalid or duplicated"));
    }
  }
  if (!std::isfinite(reconciliation.normal_tolerance_grams) ||
      !std::isfinite(reconciliation.warning_tolerance_grams) ||
      reconciliation.normal_tolerance_grams < 0.0F ||
      reconciliation.warning_tolerance_grams <
          reconciliation.normal_tolerance_grams ||
      reconciliation.warning_tolerance_grams > 1000.0F ||
      (setup.completed_steps & ~setup_step_mask) != 0U ||
      (setup.ready_confirmed &&
       (setup.completed_steps & (1U << 7U)) == 0U)) {
    return core::Result<void>::failure(
        configuration_error("reconciliation or setup settings are invalid"));
  }
  return core::Result<void>::success();
}

ConfigurationService::ConfigurationService(
    IConfigurationDocumentStore& document_store,
    services::IScaleCalibrationStore& legacy_scale_store)
    : document_store_(document_store),
      legacy_scale_store_(legacy_scale_store),
      impl_(std::make_unique<Impl>()) {}

ConfigurationService::~ConfigurationService() = default;

core::Result<void> ConfigurationService::initialize() {
  const std::lock_guard<std::mutex> lock(mutex_);
  configuration_ = {};
  status_ = {};
  impl_->document.clear();

  const auto stored = document_store_.load_configuration_document();
  std::shared_ptr<DecodedConfigurationDocument> loaded;
  std::optional<core::Error> load_error;
  bool primary_missing = false;
  if (!stored.ok()) {
    load_error = stored.error();
  } else if (stored.value().has_value()) {
    auto decoded = decode_document(*stored.value());
    if (decoded.ok()) {
      loaded = decoded.value();
    } else {
      load_error = decoded.error();
    }
  } else {
    primary_missing = true;
  }

  bool recovered_from_backup = false;
  if (loaded == nullptr) {
    const auto backup = document_store_.load_configuration_backup_document();
    if (backup.ok() && backup.value().has_value()) {
      auto decoded = decode_document(*backup.value());
      if (decoded.ok()) {
        loaded = decoded.value();
        recovered_from_backup = true;
      } else if (!load_error.has_value()) {
        load_error = decoded.error();
      }
    } else if (!backup.ok() && !load_error.has_value()) {
      load_error = backup.error();
    }
  }

  if (loaded != nullptr) {
    impl_->document = std::move(loaded->document);
    configuration_ = std::move(loaded->configuration);
    status_.initialized = true;
    status_.persistence_available = true;
    status_.migrated = loaded->migrated;
    status_.recovered_from_backup = recovered_from_backup;
    status_.loaded_schema = loaded->loaded_schema;
    if (loaded->migrated || recovered_from_backup) {
      const auto saved = persist_locked(configuration_, false);
      if (!saved.ok()) return saved;
    }
    ++revision_;
    return core::Result<void>::success();
  }

  if (primary_missing && !load_error.has_value()) {
    const auto legacy = legacy_scale_store_.load_scale_calibration();
    if (legacy.ok()) {
      configuration_.scale_calibration = legacy.value();
      if (legacy.value().has_value()) {
        configuration_.scale_hardware.rated_capacity_grams =
            legacy.value()->load_cell_capacity_grams;
      }
    } else {
      status_.last_error = legacy.error();
    }
    write_known(impl_->document, configuration_);
    status_.initialized = true;
    status_.loaded_schema = Configuration::current_schema;
    const auto saved = persist_locked(configuration_, false);
    if (!saved.ok()) return saved;
    ++revision_;
    return core::Result<void>::success();
  }

  const auto error = load_error.value_or(
      configuration_error("configuration document could not be loaded"));
  write_known(impl_->document, configuration_);
  status_.initialized = true;
  status_.persistence_available = false;
  status_.last_error = error;
  return core::Result<void>::failure(error);
}

Configuration ConfigurationService::snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return configuration_;
}

VersionedConfiguration ConfigurationService::versioned_snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return {configuration_, revision_};
}

std::uint64_t ConfigurationService::revision() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return revision_;
}

ConfigurationStatus ConfigurationService::status() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto result = status_;
  result.revision = revision_;
  return result;
}

core::Result<void> ConfigurationService::persist_locked(
    const Configuration& configuration,
    bool advance_revision) {
  const auto valid = configuration.validate();
  if (!valid.ok()) return valid;

  JsonDocument candidate;
  candidate.set(impl_->document);
  write_known(candidate, configuration);
  std::string serialized;
  serializeJson(candidate, serialized);
  if (serialized.empty() || serialized.size() > maximum_document_bytes) {
    const auto error = configuration_error("serialized configuration is too large");
    status_.last_error = error;
    return core::Result<void>::failure(error);
  }
  const bool hardware_identity_changed =
      configuration_.scale_hardware.load_cell_model !=
          configuration.scale_hardware.load_cell_model ||
      !capacities_match(
          configuration_.scale_hardware.rated_capacity_grams,
          configuration.scale_hardware.rated_capacity_grams);
  const bool clear_legacy_calibration =
      !configuration.scale_calibration.has_value() &&
      (configuration_.scale_calibration.has_value() ||
       hardware_identity_changed);
  if (clear_legacy_calibration) {
    const auto cleared = legacy_scale_store_.clear_scale_calibration();
    if (!cleared.ok()) {
      status_.last_error = cleared.error();
      return cleared;
    }
  }
  const auto saved = document_store_.save_configuration_document(serialized);
  if (!saved.ok()) {
    status_.persistence_available = false;
    status_.last_error = saved.error();
    return saved;
  }
  impl_->document = std::move(candidate);
  configuration_ = configuration;
  status_.persistence_available = true;
  status_.last_error.reset();
  if (advance_revision) ++revision_;
  return core::Result<void>::success();
}

core::Result<void> ConfigurationService::replace(
    const Configuration& configuration) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<void>::failure(
        configuration_error("configuration service is not initialized"));
  }
  return persist_locked(configuration);
}

core::Result<void> ConfigurationService::replace_if_revision(
    const Configuration& configuration,
    std::uint64_t expected_revision) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<void>::failure(
        configuration_error("configuration service is not initialized"));
  }
  if (expected_revision != revision_) {
    return core::Result<void>::failure(
        revision_conflict(expected_revision, revision_));
  }
  return persist_locked(configuration);
}

core::Result<std::string> ConfigurationService::export_json(
    bool include_credentials) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<std::string>::failure(
        configuration_error("configuration service is not initialized"));
  }
  JsonDocument exported;
  exported.set(impl_->document);
  write_known(exported, configuration_);
  if (!include_credentials) {
    auto wifi = object_at(exported, "wifi");
    wifi.remove("ssid");
    wifi.remove("password");
    wifi["credentials_configured"] = !configuration_.wifi.ssid.empty();
    auto spoolman = object_at(exported, "spoolman");
    spoolman.remove("authentication_token");
    spoolman.remove("ca_certificate_pem");
    spoolman["credentials_configured"] =
        !configuration_.spoolman.authentication_token.empty();
    spoolman["custom_ca_configured"] =
        !configuration_.spoolman.ca_certificate_pem.empty();
    auto filabridge = object_at(exported, "filabridge");
    filabridge.remove("authentication_token");
    filabridge.remove("ca_certificate_pem");
    filabridge["credentials_configured"] =
        !configuration_.filabridge.authentication_token.empty();
    filabridge["custom_ca_configured"] =
        !configuration_.filabridge.ca_certificate_pem.empty();
    auto web = object_at(exported, "web");
    web.remove("access_token");
    web["access_token_configured"] =
        !configuration_.web.access_token.empty();
  }
  std::string result;
  serializeJsonPretty(exported, result);
  return core::Result<std::string>::success(std::move(result));
}

core::Result<void> ConfigurationService::import_json(
    const std::string& document,
    bool accept_credentials) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<void>::failure(
        configuration_error("configuration service is not initialized"));
  }
  bool migrated = false;
  std::uint32_t loaded_schema = 0U;
  auto parsed = parse_document(document, migrated, loaded_schema);
  if (!parsed.ok()) return core::Result<void>::failure(parsed.error());
  Configuration imported;
  const auto decoded = read_configuration(parsed.value(), imported);
  if (!decoded.ok()) return core::Result<void>::failure(decoded.error());

  if (!accept_credentials) {
    imported.wifi.ssid = configuration_.wifi.ssid;
    imported.wifi.password = configuration_.wifi.password;
    imported.spoolman.authentication_token =
        configuration_.spoolman.authentication_token;
    imported.spoolman.ca_certificate_pem =
        configuration_.spoolman.ca_certificate_pem;
    imported.filabridge.authentication_token =
        configuration_.filabridge.authentication_token;
    imported.filabridge.ca_certificate_pem =
        configuration_.filabridge.ca_certificate_pem;
    imported.web.access_token = configuration_.web.access_token;
  }

  JsonDocument previous;
  previous.set(impl_->document);
  impl_->document = std::move(parsed.value());
  const auto saved = persist_locked(imported);
  if (!saved.ok()) impl_->document = std::move(previous);
  if (saved.ok()) {
    status_.migrated = migrated;
    status_.loaded_schema = loaded_schema;
  }
  return saved;
}

core::Result<std::optional<services::ScaleCalibration>>
ConfigurationService::load_scale_calibration() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        configuration_error("configuration service is not initialized"));
  }
  return core::Result<std::optional<services::ScaleCalibration>>::success(
      configuration_.scale_calibration);
}

core::Result<void> ConfigurationService::save_scale_calibration(
    const services::ScaleCalibration& calibration) {
  const auto valid = calibration.validate();
  if (!valid.ok()) return valid;

  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<void>::failure(
        configuration_error("configuration service is not initialized"));
  }

  auto updated = configuration_;
  updated.scale_calibration = calibration;
  const auto updated_valid = updated.validate();
  if (!updated_valid.ok()) return updated_valid;

  const auto legacy_saved = legacy_scale_store_.save_scale_calibration(calibration);
  if (!legacy_saved.ok()) {
    status_.last_error = legacy_saved.error();
    return legacy_saved;
  }

  // Keep the central document and runtime revision authoritative: a legacy
  // mirror failure must never commit only the central copy. If the following
  // document write fails, the same-profile mirror may be newer, but boot still
  // prefers the existing central document and no live/CAS state advances.
  return persist_locked(updated);
}

core::Result<void> ConfigurationService::clear_scale_calibration() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<void>::failure(
        configuration_error("configuration service is not initialized"));
  }
  if (!configuration_.scale_calibration.has_value()) {
    const auto cleared = legacy_scale_store_.clear_scale_calibration();
    if (!cleared.ok()) status_.last_error = cleared.error();
    return cleared;
  }

  auto updated = configuration_;
  updated.scale_calibration.reset();
  return persist_locked(updated);
}

core::Result<std::vector<domain::ConfirmedSpoolMapping>>
ConfigurationService::load_spool_identity_mappings() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<std::vector<domain::ConfirmedSpoolMapping>>::failure(
        configuration_error("configuration service is not initialized"));
  }
  return core::Result<std::vector<domain::ConfirmedSpoolMapping>>::success(
      configuration_.spool_identity_mappings);
}

core::Result<void> ConfigurationService::confirm_spool_identity_mapping(
    const domain::ConfirmedSpoolMapping& mapping) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!status_.initialized) {
    return core::Result<void>::failure(
        configuration_error("configuration service is not initialized"));
  }
  auto normalized = mapping;
  if (normalized.instance_uuid.has_value()) {
    std::transform(
        normalized.instance_uuid->begin(), normalized.instance_uuid->end(),
        normalized.instance_uuid->begin(), [](char character) {
          return static_cast<char>(
              std::tolower(static_cast<unsigned char>(character)));
        });
  }
  if (normalized.nfc_uid.has_value()) {
    std::transform(
        normalized.nfc_uid->begin(), normalized.nfc_uid->end(),
        normalized.nfc_uid->begin(), [](char character) {
          return static_cast<char>(
              std::toupper(static_cast<unsigned char>(character)));
        });
  }
  auto updated = configuration_;
  auto& mappings = updated.spool_identity_mappings;
  for (const auto& existing : mappings) {
    const auto equal_case_insensitive = [](const auto& left, const auto& right) {
      return left.has_value() && right.has_value() &&
          left->size() == right->size() &&
          std::equal(left->begin(), left->end(), right->begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                std::tolower(static_cast<unsigned char>(b));
          });
    };
    if ((equal_case_insensitive(
             normalized.instance_uuid, existing.instance_uuid) &&
         existing.spool_id != normalized.spool_id) ||
        (equal_case_insensitive(normalized.nfc_uid, existing.nfc_uid) &&
         existing.spool_id != normalized.spool_id)) {
      return core::Result<void>::failure(
          configuration_error("identity is already mapped to a different spool"));
    }
  }
  auto same_spool = std::find_if(
      mappings.begin(), mappings.end(), [&](const auto& existing) {
        return existing.spool_id == normalized.spool_id;
      });
  if (same_spool != mappings.end()) {
    if (normalized.instance_uuid.has_value()) {
      same_spool->instance_uuid = normalized.instance_uuid;
    }
    if (normalized.nfc_uid.has_value()) same_spool->nfc_uid = normalized.nfc_uid;
  } else {
    mappings.push_back(normalized);
  }
  return persist_locked(updated);
}

}  // namespace opentag::config

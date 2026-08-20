#include "web/configuration_patch.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace opentag::web {
namespace {

core::Error revision_conflict(
    std::uint64_t expected_revision,
    std::uint64_t current_revision) {
  return {
      core::ErrorCategory::conflict,
      "Configuration revision is stale: expected " +
          std::to_string(expected_revision) + ", current " +
          std::to_string(current_revision) + "; reload and retry",
      false};
}

core::Error invalid_profile(const char* message) {
  return {core::ErrorCategory::configuration, message, false};
}

core::Result<void> apply_profile_id(
    const api::ScaleProfilePatch& patch,
    services::ScaleHardwareSettings& settings) {
  if (!patch.id.has_value()) return core::Result<void>::success();

  float capacity_grams = 0.0F;
  if (*patch.id == "yzc-133-2kg") {
    capacity_grams = 2000.0F;
  } else if (*patch.id == "yzc-133-5kg") {
    capacity_grams = 5000.0F;
  } else {
    return core::Result<void>::failure(
        invalid_profile("scale profile identifier is unsupported"));
  }
  if (patch.model.has_value() && *patch.model != "YZC-133") {
    return core::Result<void>::failure(
        invalid_profile("scale profile model conflicts with its identifier"));
  }
  if (patch.rated_capacity_grams.has_value() &&
      std::fabs(
          static_cast<float>(*patch.rated_capacity_grams) - capacity_grams) >
          0.01F) {
    return core::Result<void>::failure(invalid_profile(
        "scale profile capacity conflicts with its identifier"));
  }
  settings.load_cell_model = "YZC-133";
  settings.rated_capacity_grams = capacity_grams;
  return core::Result<void>::success();
}

}  // namespace

core::Result<config::Configuration> apply_configuration_patch(
    const config::VersionedConfiguration& current,
    const api::ConfigurationPatchMutation& patch) {
  if (current.revision != patch.expected_revision) {
    return core::Result<config::Configuration>::failure(
        revision_conflict(patch.expected_revision, current.revision));
  }

  auto proposed = current.configuration;
  if (patch.device.has_value()) {
    const auto& value = *patch.device;
    if (value.hostname.has_value()) proposed.device.hostname = *value.hostname;
    if (value.brightness_percent.has_value()) {
      proposed.device.brightness_percent = *value.brightness_percent;
    }
    if (value.dim_after_ms.has_value()) {
      proposed.device.dim_after_ms = *value.dim_after_ms;
    }
    if (value.sleep_after_ms.has_value()) {
      proposed.device.sleep_after_ms = *value.sleep_after_ms;
    }
    if (value.update_channel.has_value()) {
      proposed.device.update_channel = *value.update_channel;
    }
  }

  if (patch.wifi.has_value()) {
    const auto& value = *patch.wifi;
    if (value.ssid.has_value()) proposed.wifi.ssid = *value.ssid;
    if (value.password.has_value()) proposed.wifi.password = *value.password;
    if (value.auto_reconnect.has_value()) {
      proposed.wifi.auto_reconnect = *value.auto_reconnect;
    }
    if (value.connect_timeout_ms.has_value()) {
      proposed.wifi.connect_timeout_ms = *value.connect_timeout_ms;
    }
    if (value.reconnect_initial_ms.has_value()) {
      proposed.wifi.reconnect_initial_ms = *value.reconnect_initial_ms;
    }
    if (value.reconnect_max_ms.has_value()) {
      proposed.wifi.reconnect_max_ms = *value.reconnect_max_ms;
    }
  }

  if (patch.web.has_value()) {
    const auto& value = *patch.web;
    if (value.access_token.has_value()) {
      proposed.web.access_token = *value.access_token;
    }
  }

  if (patch.spoolman.has_value()) {
    const auto& value = *patch.spoolman;
    if (value.url.has_value()) proposed.spoolman.url = *value.url;
    if (value.authentication_token.has_value()) {
      proposed.spoolman.authentication_token = *value.authentication_token;
    }
    if (value.identity_field.has_value()) {
      proposed.spoolman.identity_field = *value.identity_field;
    }
    if (value.nfc_uid_field.has_value()) {
      proposed.spoolman.nfc_uid_field = *value.nfc_uid_field;
    }
    if (value.ca_certificate_pem.has_value()) {
      proposed.spoolman.ca_certificate_pem = *value.ca_certificate_pem;
    }
  }

  if (patch.filabridge.has_value()) {
    const auto& value = *patch.filabridge;
    if (value.url.has_value()) proposed.filabridge.url = *value.url;
    if (value.authentication_token.has_value()) {
      proposed.filabridge.authentication_token = *value.authentication_token;
    }
    if (value.selected_printer_id.has_value()) {
      proposed.filabridge.selected_printer_id = *value.selected_printer_id;
    }
    if (value.ca_certificate_pem.has_value()) {
      proposed.filabridge.ca_certificate_pem = *value.ca_certificate_pem;
    }
  }

  if (patch.scale_profile.has_value()) {
    const auto before = proposed.scale_hardware;
    const auto& value = *patch.scale_profile;
    const auto profile_id = apply_profile_id(value, proposed.scale_hardware);
    if (!profile_id.ok()) {
      return core::Result<config::Configuration>::failure(profile_id.error());
    }
    if (value.model.has_value()) {
      proposed.scale_hardware.load_cell_model = *value.model;
    }
    if (value.rated_capacity_grams.has_value()) {
      proposed.scale_hardware.rated_capacity_grams =
          static_cast<float>(*value.rated_capacity_grams);
    }
    if (value.overload_ratio.has_value()) {
      proposed.scale_hardware.overload_ratio = *value.overload_ratio;
    }
    if (before.load_cell_model != proposed.scale_hardware.load_cell_model ||
        before.rated_capacity_grams !=
            proposed.scale_hardware.rated_capacity_grams) {
      proposed.scale_calibration.reset();
    }
  }

  if (patch.toolheads.has_value()) {
    proposed.toolheads.clear();
    proposed.toolheads.reserve(patch.toolheads->size());
    for (const auto& value : *patch.toolheads) {
      proposed.toolheads.push_back(
          {value.backend_id,
           value.display_name,
           value.nozzle_diameter_mm,
           value.enabled,
           value.nozzle_material,
           value.maximum_temperature_c,
           value.notes});
    }
  }

  if (patch.reconciliation.has_value()) {
    const auto& value = *patch.reconciliation;
    if (value.normal_tolerance_grams.has_value()) {
      proposed.reconciliation.normal_tolerance_grams =
          *value.normal_tolerance_grams;
    }
    if (value.warning_tolerance_grams.has_value()) {
      proposed.reconciliation.warning_tolerance_grams =
          *value.warning_tolerance_grams;
    }
  }

  const auto valid = proposed.validate();
  if (!valid.ok()) {
    return core::Result<config::Configuration>::failure(valid.error());
  }
  return core::Result<config::Configuration>::success(std::move(proposed));
}

}  // namespace opentag::web

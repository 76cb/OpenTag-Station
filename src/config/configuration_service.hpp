#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "services/scale_service.hpp"
#include "services/spool_identity_store.hpp"

namespace opentag::config {

struct DeviceSettings {
  std::string hostname{"opentag-station"};
  std::uint8_t brightness_percent{80U};
  std::uint32_t dim_after_ms{120000U};
  std::uint32_t sleep_after_ms{300000U};
  std::string update_channel{"stable"};
};

struct WifiSettings {
  std::string ssid;
  std::string password;
  bool auto_reconnect{true};
  std::uint32_t connect_timeout_ms{15000U};
  std::uint32_t reconnect_initial_ms{1000U};
  std::uint32_t reconnect_max_ms{60000U};
};

struct SpoolmanSettings {
  std::string url;
  std::string authentication_token;
  std::string identity_field{"opentag_instance_uuid"};
  std::string nfc_uid_field{"nfc_uid"};
  std::string ca_certificate_pem;
};

struct FilaBridgeSettings {
  std::string url;
  std::string authentication_token;
  std::string selected_printer_id;
  std::string ca_certificate_pem;
};

struct WebSettings {
  std::string access_token;
};

struct ToolheadProfile {
  std::int32_t backend_id{0};
  std::string display_name;
  float nozzle_diameter_mm{0.4F};
  bool enabled{true};
  std::string nozzle_material{"brass"};
  std::uint16_t maximum_temperature_c{300U};
  std::string notes;
};

struct ReconciliationSettings {
  float normal_tolerance_grams{5.0F};
  float warning_tolerance_grams{20.0F};
};

struct SetupProgress {
  std::uint32_t completed_steps{0U};
  bool ready_confirmed{false};
};

struct Configuration {
  static constexpr std::uint32_t current_schema = 3U;

  std::uint32_t schema_version{current_schema};
  std::string hardware_id{"wt32-sc01-plus-rev-a"};
  DeviceSettings device;
  WifiSettings wifi;
  SpoolmanSettings spoolman;
  FilaBridgeSettings filabridge;
  WebSettings web;
  services::ScaleHardwareSettings scale_hardware;
  std::optional<services::ScaleCalibration> scale_calibration;
  std::vector<ToolheadProfile> toolheads;
  std::vector<domain::ConfirmedSpoolMapping> spool_identity_mappings;
  ReconciliationSettings reconciliation;
  SetupProgress setup;

  [[nodiscard]] core::Result<void> validate() const;
};

class IConfigurationDocumentStore {
 public:
  virtual ~IConfigurationDocumentStore() = default;
  [[nodiscard]] virtual core::Result<std::optional<std::string>>
  load_configuration_document() = 0;
  [[nodiscard]] virtual core::Result<std::optional<std::string>>
  load_configuration_backup_document() = 0;
  [[nodiscard]] virtual core::Result<void> save_configuration_document(
      const std::string& document) = 0;
};

struct ConfigurationStatus {
  bool initialized{false};
  bool persistence_available{true};
  bool migrated{false};
  bool recovered_from_backup{false};
  std::uint32_t loaded_schema{0U};
  std::uint64_t revision{0U};
  std::optional<core::Error> last_error;
};

struct VersionedConfiguration {
  Configuration configuration;
  std::uint64_t revision{0U};
};

struct LocalInterfaceSettingsSnapshot {
  std::string hostname;
  WebSettings web;
  std::uint64_t revision{0U};
};

class ConfigurationService final : public services::IScaleCalibrationStore,
                                   public services::ISpoolIdentityMappingStore {
 public:
  ConfigurationService(
      IConfigurationDocumentStore& document_store,
      services::IScaleCalibrationStore& legacy_scale_store);
  ~ConfigurationService();

  ConfigurationService(const ConfigurationService&) = delete;
  ConfigurationService& operator=(const ConfigurationService&) = delete;

  [[nodiscard]] core::Result<void> initialize();
  [[nodiscard]] Configuration snapshot() const;
  [[nodiscard]] LocalInterfaceSettingsSnapshot
      local_interface_settings_snapshot() const;
  [[nodiscard]] VersionedConfiguration versioned_snapshot() const;
  [[nodiscard]] std::uint64_t revision() const;
  [[nodiscard]] ConfigurationStatus status() const;
  [[nodiscard]] core::Result<void> replace(const Configuration& configuration);
  [[nodiscard]] core::Result<void> replace_if_revision(
      const Configuration& configuration,
      std::uint64_t expected_revision);
  [[nodiscard]] core::Result<std::string> export_json(
      bool include_credentials) const;
  [[nodiscard]] core::Result<void> import_json(
      const std::string& document,
      bool accept_credentials);

  [[nodiscard]] core::Result<std::optional<services::ScaleCalibration>>
  load_scale_calibration() override;
  [[nodiscard]] core::Result<void> save_scale_calibration(
      const services::ScaleCalibration& calibration) override;
  [[nodiscard]] core::Result<void> clear_scale_calibration() override;
  [[nodiscard]] core::Result<std::vector<domain::ConfirmedSpoolMapping>>
  load_spool_identity_mappings() override;
  [[nodiscard]] core::Result<void> confirm_spool_identity_mapping(
      const domain::ConfirmedSpoolMapping& mapping) override;

 private:
  struct Impl;

  [[nodiscard]] core::Result<void> persist_locked(
      const Configuration& configuration,
      bool advance_revision = true);

  IConfigurationDocumentStore& document_store_;
  services::IScaleCalibrationStore& legacy_scale_store_;
  mutable std::mutex mutex_;
  Configuration configuration_;
  ConfigurationStatus status_;
  std::uint64_t revision_{0U};
  std::unique_ptr<Impl> impl_;
};

}  // namespace opentag::config

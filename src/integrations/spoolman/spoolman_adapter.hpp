#pragma once

#include <optional>
#include <string>
#include <utility>

#include "config/configuration_service.hpp"
#include "integrations/inventory.hpp"
#include "network/http_transport.hpp"

namespace opentag::integrations::spoolman {

struct SpoolmanStatus {
  bool connected{false};
  bool healthy{false};
  bool version_formally_tested{false};
  std::string version;
  std::string git_commit;
  BackendCapabilities capabilities;
  std::optional<core::Error> last_error;
};

class SpoolmanAdapter final : public ISpoolInventory {
 public:
  SpoolmanAdapter(
      network::IHttpTransport& transport,
      config::SpoolmanSettings settings)
      : transport_(transport), settings_(std::move(settings)) {}

  void configure(config::SpoolmanSettings settings);
  [[nodiscard]] core::Result<SpoolmanStatus> probe();
  [[nodiscard]] SpoolmanStatus status() const { return status_; }

  [[nodiscard]] core::Result<std::vector<domain::Spool>> list_spools() override;
  [[nodiscard]] core::Result<std::vector<domain::Spool>> find_spools(
      const SpoolFilter& filter) override;
  [[nodiscard]] core::Result<domain::Spool> get_spool(
      domain::SpoolId id) override;
  [[nodiscard]] core::Result<domain::Spool> create_spool(
      const CreateSpoolRequest& request) override;
  [[nodiscard]] core::Result<domain::Spool> set_remaining_weight(
      domain::SpoolId id,
      const RemainingWeightUpdate& update) override;
  [[nodiscard]] core::Result<std::vector<std::string>> list_locations() override;
  [[nodiscard]] core::Result<std::vector<ExtraFieldDefinition>>
  list_extra_fields() override;
  [[nodiscard]] BackendCapabilities capabilities() const override {
    return status_.capabilities;
  }

  [[nodiscard]] core::Result<domain::Spool> set_extra_field(
      domain::SpoolId id,
      const std::string& key,
      const std::optional<std::string>& json_encoded_value);

 private:
  [[nodiscard]] core::Result<network::HttpResponse> request(
      const std::string& method,
      const std::string& endpoint,
      const std::string& body = {},
      std::size_t maximum_response_bytes = 32768U);
  [[nodiscard]] core::Result<std::vector<domain::Spool>> parse_spool_list(
      const std::string& body) const;
  [[nodiscard]] core::Result<domain::Spool> parse_spool(
      const std::string& body) const;
  [[nodiscard]] core::Result<void> probe_read_capabilities();
  [[nodiscard]] std::string endpoint(const std::string& path) const;

  network::IHttpTransport& transport_;
  config::SpoolmanSettings settings_;
  SpoolmanStatus status_;
};

}  // namespace opentag::integrations::spoolman

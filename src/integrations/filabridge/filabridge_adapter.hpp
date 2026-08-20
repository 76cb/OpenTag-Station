#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/configuration_service.hpp"
#include "core/result.hpp"
#include "integrations/printer_assignment.hpp"
#include "network/http_transport.hpp"

namespace opentag::integrations::filabridge {

struct FilaBridgeStatus {
  bool connected{false};
  bool healthy{false};
  bool version_formally_tested{false};
  std::string version;
  BackendCapabilities capabilities;
  std::optional<core::Error> last_error;
};

class FilaBridgeAdapter final : public IPrinterAssignmentService {
 public:
  FilaBridgeAdapter(
      network::IHttpTransport& transport,
      config::FilaBridgeSettings settings)
      : transport_(transport), settings_(std::move(settings)) {}

  void configure(config::FilaBridgeSettings settings);
  [[nodiscard]] core::Result<FilaBridgeStatus> probe();
  [[nodiscard]] FilaBridgeStatus status() const { return status_; }

  [[nodiscard]] core::Result<std::vector<domain::Printer>> list_printers() override;
  [[nodiscard]] core::Result<std::vector<domain::Toolhead>> get_toolheads(
      const std::string& printer_id) override;
  [[nodiscard]] core::Result<void> assign_spool(
      const std::string& printer_id,
      int backend_toolhead_id,
      domain::SpoolId spool_id) override;
  [[nodiscard]] core::Result<void> unassign_spool(
      const std::string& printer_id,
      int backend_toolhead_id) override;
  [[nodiscard]] BackendCapabilities capabilities() const override {
    return status_.capabilities;
  }

 private:
  [[nodiscard]] core::Result<network::HttpResponse> request(
      const std::string& method,
      const std::string& path,
      const std::string& body = {},
      std::size_t maximum_response_bytes = 32768U);
  [[nodiscard]] core::Result<std::vector<domain::Printer>> read_printers();
  [[nodiscard]] core::Result<void> mutate_mapping(
      const std::string& printer_id,
      int backend_toolhead_id,
      domain::SpoolId spool_id);
  [[nodiscard]] std::string endpoint(const std::string& path) const;

  network::IHttpTransport& transport_;
  config::FilaBridgeSettings settings_;
  FilaBridgeStatus status_;
};

}  // namespace opentag::integrations::filabridge

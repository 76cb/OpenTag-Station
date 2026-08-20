#pragma once

#include <string>
#include <vector>

#include "core/result.hpp"
#include "domain/printer.hpp"
#include "integrations/backend_capabilities.hpp"

namespace opentag::integrations {

class IPrinterAssignmentService {
 public:
  virtual ~IPrinterAssignmentService() = default;
  virtual core::Result<std::vector<domain::Printer>> list_printers() = 0;
  virtual core::Result<std::vector<domain::Toolhead>> get_toolheads(const std::string& printer_id) = 0;
  virtual core::Result<void> assign_spool(
      const std::string& printer_id,
      int backend_toolhead_id,
      domain::SpoolId spool_id) = 0;
  virtual core::Result<void> unassign_spool(
      const std::string& printer_id,
      int backend_toolhead_id) = 0;
  [[nodiscard]] virtual BackendCapabilities capabilities() const = 0;
};

}  // namespace opentag::integrations

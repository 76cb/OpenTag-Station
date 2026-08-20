#pragma once

#include <optional>
#include <string>

#include "core/result.hpp"
#include "domain/printer.hpp"
#include "domain/spool.hpp"
#include "integrations/printer_assignment.hpp"

namespace opentag::services {

enum class AssignmentOutcome {
  verified,
  already_in_expected_state,
  replacement_confirmation_required,
  active_print_override_required,
  printer_state_override_required,
};

struct ToolheadMutationPrecondition {
  bool supplied{false};
  std::optional<domain::SpoolId> expected_previous_spool_id;
  domain::PrinterState expected_printer_state{domain::PrinterState::unknown};
};

struct AssignmentRequest {
  std::string printer_id;
  int backend_toolhead_id{0};
  domain::SpoolId spool_id{0};
  bool replace_occupied_confirmed{false};
  bool advanced_active_print_override{false};
  ToolheadMutationPrecondition precondition;
};

struct UnassignmentRequest {
  std::string printer_id;
  int backend_toolhead_id{0};
  bool advanced_active_print_override{false};
  ToolheadMutationPrecondition precondition;
};

struct AssignmentResult {
  AssignmentOutcome outcome{AssignmentOutcome::verified};
  domain::PrinterState printer_state{domain::PrinterState::unknown};
  std::optional<domain::SpoolId> previous_spool_id;
  std::optional<domain::SpoolId> expected_spool_id;

  [[nodiscard]] bool verified() const {
    return outcome == AssignmentOutcome::verified ||
        outcome == AssignmentOutcome::already_in_expected_state;
  }
};

class ToolheadAssignmentService final {
 public:
  explicit ToolheadAssignmentService(
      integrations::IPrinterAssignmentService& backend)
      : backend_(backend) {}

  [[nodiscard]] core::Result<AssignmentResult> assign(
      const AssignmentRequest& request);
  [[nodiscard]] core::Result<AssignmentResult> unassign(
      const UnassignmentRequest& request);

 private:
  [[nodiscard]] core::Result<std::pair<domain::Printer, domain::Toolhead>>
  revalidate(
      const std::string& printer_id,
      int backend_toolhead_id);
  [[nodiscard]] core::Result<std::optional<domain::SpoolId>> readback(
      const std::string& printer_id,
      int backend_toolhead_id);

  integrations::IPrinterAssignmentService& backend_;
};

}  // namespace opentag::services

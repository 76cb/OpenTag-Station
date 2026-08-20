#include "services/toolhead_assignment_service.hpp"

#include <algorithm>
#include <utility>

namespace opentag::services {
namespace {

core::Error request_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

core::Error stale_error(const std::string& message) {
  return {core::ErrorCategory::conflict, message, false};
}

core::Error verification_error(const std::string& message) {
  return {core::ErrorCategory::invalid_response, message, false};
}

bool precondition_matches(
    const ToolheadMutationPrecondition& precondition,
    const domain::Printer& printer,
    const domain::Toolhead& toolhead) {
  return !precondition.supplied ||
      (precondition.expected_previous_spool_id == toolhead.assigned_spool &&
       precondition.expected_printer_state == printer.state);
}

bool printer_state_is_unverified(domain::PrinterState state) {
  return state == domain::PrinterState::unknown ||
      state == domain::PrinterState::offline ||
      state == domain::PrinterState::not_configured;
}

}  // namespace

core::Result<std::pair<domain::Printer, domain::Toolhead>>
ToolheadAssignmentService::revalidate(
    const std::string& printer_id,
    int backend_toolhead_id) {
  if (printer_id.empty() || backend_toolhead_id < 0) {
    return core::Result<std::pair<domain::Printer, domain::Toolhead>>::failure(
        request_error("Toolhead assignment target is invalid"));
  }
  const auto printers = backend_.list_printers();
  if (!printers.ok()) {
    return core::Result<std::pair<domain::Printer, domain::Toolhead>>::failure(
        printers.error());
  }
  const auto printer = std::find_if(
      printers.value().begin(), printers.value().end(),
      [&](const auto& candidate) { return candidate.id == printer_id; });
  if (printer == printers.value().end()) {
    return core::Result<std::pair<domain::Printer, domain::Toolhead>>::failure(
        stale_error("Selected printer no longer exists in FilaBridge"));
  }
  const auto toolhead = std::find_if(
      printer->toolheads.begin(), printer->toolheads.end(),
      [&](const auto& candidate) {
        return candidate.backend_id == backend_toolhead_id;
      });
  if (toolhead == printer->toolheads.end()) {
    return core::Result<std::pair<domain::Printer, domain::Toolhead>>::failure(
        stale_error("Selected toolhead no longer exists in FilaBridge"));
  }
  return core::Result<std::pair<domain::Printer, domain::Toolhead>>::success(
      {*printer, *toolhead});
}

core::Result<std::optional<domain::SpoolId>> ToolheadAssignmentService::readback(
    const std::string& printer_id,
    int backend_toolhead_id) {
  const auto toolheads = backend_.get_toolheads(printer_id);
  if (!toolheads.ok()) {
    return core::Result<std::optional<domain::SpoolId>>::failure(toolheads.error());
  }
  const auto toolhead = std::find_if(
      toolheads.value().begin(), toolheads.value().end(),
      [&](const auto& candidate) {
        return candidate.backend_id == backend_toolhead_id;
      });
  if (toolhead == toolheads.value().end()) {
    return core::Result<std::optional<domain::SpoolId>>::failure(
        stale_error("Toolhead disappeared during assignment verification"));
  }
  return core::Result<std::optional<domain::SpoolId>>::success(
      toolhead->assigned_spool);
}

core::Result<AssignmentResult> ToolheadAssignmentService::assign(
    const AssignmentRequest& request) {
  if (request.spool_id <= 0) {
    return core::Result<AssignmentResult>::failure(
        request_error("Assigned spool ID must be positive"));
  }
  const auto current = revalidate(
      request.printer_id, request.backend_toolhead_id);
  if (!current.ok()) {
    return core::Result<AssignmentResult>::failure(current.error());
  }
  const auto& printer = current.value().first;
  const auto& toolhead = current.value().second;
  AssignmentResult result;
  result.printer_state = printer.state;
  result.previous_spool_id = toolhead.assigned_spool;
  result.expected_spool_id = request.spool_id;

  if (!precondition_matches(request.precondition, printer, toolhead)) {
    return core::Result<AssignmentResult>::failure(stale_error(
        "Toolhead assignment precondition is stale; refresh before confirming"));
  }

  if (toolhead.assigned_spool == request.spool_id) {
    result.outcome = AssignmentOutcome::already_in_expected_state;
    return core::Result<AssignmentResult>::success(std::move(result));
  }
  if (domain::is_active_print_state(printer.state) &&
      !request.advanced_active_print_override) {
    result.outcome = AssignmentOutcome::active_print_override_required;
    return core::Result<AssignmentResult>::success(std::move(result));
  }
  if (printer_state_is_unverified(printer.state) &&
      !request.advanced_active_print_override) {
    result.outcome = AssignmentOutcome::printer_state_override_required;
    return core::Result<AssignmentResult>::success(std::move(result));
  }
  if (toolhead.assigned_spool.has_value() &&
      !request.replace_occupied_confirmed) {
    result.outcome = AssignmentOutcome::replacement_confirmation_required;
    return core::Result<AssignmentResult>::success(std::move(result));
  }

  const auto mutation = backend_.assign_spool(
      request.printer_id, request.backend_toolhead_id, request.spool_id);
  if (!mutation.ok()) {
    return core::Result<AssignmentResult>::failure(mutation.error());
  }
  const auto actual = readback(request.printer_id, request.backend_toolhead_id);
  if (!actual.ok()) {
    return core::Result<AssignmentResult>::failure(actual.error());
  }
  if (!actual.value().has_value() || *actual.value() != request.spool_id) {
    return core::Result<AssignmentResult>::failure(verification_error(
        "FilaBridge assignment verification failed: mapped spool does not match"));
  }
  result.outcome = AssignmentOutcome::verified;
  return core::Result<AssignmentResult>::success(std::move(result));
}

core::Result<AssignmentResult> ToolheadAssignmentService::unassign(
    const UnassignmentRequest& request) {
  const auto current = revalidate(
      request.printer_id, request.backend_toolhead_id);
  if (!current.ok()) {
    return core::Result<AssignmentResult>::failure(current.error());
  }
  const auto& printer = current.value().first;
  const auto& toolhead = current.value().second;
  AssignmentResult result;
  result.printer_state = printer.state;
  result.previous_spool_id = toolhead.assigned_spool;

  if (!precondition_matches(request.precondition, printer, toolhead)) {
    return core::Result<AssignmentResult>::failure(stale_error(
        "Toolhead unassignment precondition is stale; refresh before confirming"));
  }

  if (!toolhead.assigned_spool.has_value()) {
    result.outcome = AssignmentOutcome::already_in_expected_state;
    return core::Result<AssignmentResult>::success(std::move(result));
  }
  if (domain::is_active_print_state(printer.state) &&
      !request.advanced_active_print_override) {
    result.outcome = AssignmentOutcome::active_print_override_required;
    return core::Result<AssignmentResult>::success(std::move(result));
  }
  if (printer_state_is_unverified(printer.state) &&
      !request.advanced_active_print_override) {
    result.outcome = AssignmentOutcome::printer_state_override_required;
    return core::Result<AssignmentResult>::success(std::move(result));
  }

  const auto mutation = backend_.unassign_spool(
      request.printer_id, request.backend_toolhead_id);
  if (!mutation.ok()) {
    return core::Result<AssignmentResult>::failure(mutation.error());
  }
  const auto actual = readback(request.printer_id, request.backend_toolhead_id);
  if (!actual.ok()) {
    return core::Result<AssignmentResult>::failure(actual.error());
  }
  if (actual.value().has_value()) {
    return core::Result<AssignmentResult>::failure(verification_error(
        "FilaBridge unassignment verification failed: toolhead is still mapped"));
  }
  result.outcome = AssignmentOutcome::verified;
  return core::Result<AssignmentResult>::success(std::move(result));
}

}  // namespace opentag::services

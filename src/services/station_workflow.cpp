#include "services/station_workflow.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opentag::services {
namespace {

bool connection_failure(const core::Error& error) {
  return error.category == core::ErrorCategory::network ||
      error.category == core::ErrorCategory::authentication ||
      error.category == core::ErrorCategory::backend_unavailable;
}

core::Error assignment_unavailable() {
  return {
      core::ErrorCategory::backend_unavailable,
      "FilaBridge is offline; toolhead assignment was not queued",
      true,
  };
}

core::Error assignment_read_only() {
  return {
      core::ErrorCategory::api_changed,
      "FilaBridge is connected read-only; mapping capability is unavailable and no assignment was queued",
      false,
  };
}

core::Error stale_request(const std::string& message) {
  return {core::ErrorCategory::conflict, message, false};
}

}  // namespace

void StationWorkflow::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = {};
  state_.spool_generation = next_spool_generation_++;
  state_.printer_revision = next_printer_revision_++;
}

WorkflowSnapshot StationWorkflow::accept_identified_spool(
    const nfc::openprinttag::MaterialRecord& material,
    const nfc::nfcv::Uid& uid,
    domain::WeightReading physical_weight,
    domain::EmptyWeightCandidates supplemental_empty_weights,
    ReconciliationTolerances tolerances) {
  WorkflowSnapshot pending;
  pending.openprinttag_available = true;
  pending.material = material;
  pending.uid = uid;
  pending.physical_weight = physical_weight;
  pending.weight_snapshot.physical = physical_weight;
  pending.stage = physical_weight.stable
                      ? WorkflowStage::resolving_spool
                      : WorkflowStage::waiting_for_stable_weight;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending.spool_generation = next_spool_generation_++;
    const auto filabridge = state_.filabridge;
    const auto filabridge_assignment_available =
        state_.filabridge_assignment_available;
    const auto printers = state_.printers;
    const auto printer_revision = state_.printer_revision;
    const auto filabridge_error = state_.filabridge_error;
    state_ = pending;
    state_.filabridge = filabridge;
    state_.filabridge_assignment_available =
        filabridge_assignment_available;
    state_.printers = printers;
    state_.printer_revision = printer_revision;
    state_.filabridge_error = filabridge_error;
  }
  if (!physical_weight.stable) return snapshot();

  const auto identity = identity_from_openprinttag(material, uid);
  const auto resolution = spool_resolver_.resolve(identity);
  if (!resolution.ok()) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.stage = WorkflowStage::spool_resolution_unavailable;
    state_.spoolman = connection_failure(resolution.error())
                          ? BackendAvailability::offline
                          : BackendAvailability::unknown;
    state_.spoolman_error = resolution.error();
    return state_;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.spoolman = BackendAvailability::online;
    state_.spoolman_error.reset();
    state_.spool_candidates = resolution.value().candidates;
    if (resolution.value().status == SpoolResolutionStatus::ambiguous ||
        resolution.value().status == SpoolResolutionStatus::conflict) {
      state_.stage = WorkflowStage::spool_selection_required;
      return state_;
    }
    if (resolution.value().status != SpoolResolutionStatus::matched ||
        resolution.value().match() == nullptr) {
      state_.stage = WorkflowStage::spool_not_found;
      return state_;
    }

    state_.spool = *resolution.value().match();
    if (material.empty_container_weight.has_value() &&
        std::isfinite(*material.empty_container_weight)) {
      supplemental_empty_weights.openprinttag_grams =
          static_cast<float>(*material.empty_container_weight);
    }
    supplemental_empty_weights.spoolman_spool_grams =
        state_.spool->empty_spool_grams;
    supplemental_empty_weights.package_default_grams =
        state_.spool->package_empty_spool_grams;
    supplemental_empty_weights.vendor_default_grams =
        state_.spool->vendor_empty_spool_grams;
    const auto empty = domain::EmptyWeightResolver::resolve(
        supplemental_empty_weights);
    if (empty.has_value()) {
      state_.weight_snapshot.empty_spool_grams = empty->grams;
      state_.weight_snapshot.empty_weight_source = empty->source;
    }
    state_.weight_snapshot.spoolman_remaining_grams =
        state_.spool->remaining_grams;
    const auto tag_remaining = material.remaining_weight();
    if (tag_remaining.has_value() && std::isfinite(*tag_remaining) &&
        *tag_remaining >= 0.0) {
      state_.weight_snapshot.tag_remaining_grams =
          static_cast<float>(*tag_remaining);
    }
    state_.reconciliation = WeightReconciler::compare(
        state_.weight_snapshot, tolerances);
    state_.stage = WorkflowStage::spool_ready;
    return state_;
  }
}

WorkflowSnapshot StationWorkflow::refresh_printers() {
  const auto printers = printer_backend_.list_printers();
  const auto capabilities = printer_backend_.capabilities();
  std::lock_guard<std::mutex> lock(mutex_);
  if (!printers.ok()) {
    state_.filabridge = connection_failure(printers.error())
                            ? BackendAvailability::offline
                            : BackendAvailability::online;
    state_.filabridge_assignment_available = false;
    state_.filabridge_error = printers.error();
    state_.printers.clear();
    state_.printer_revision = next_printer_revision_++;
    return state_;
  }
  state_.filabridge = BackendAvailability::online;
  state_.filabridge_assignment_available =
      capabilities.has(integrations::BackendCapability::map_toolhead) &&
      capabilities.has(integrations::BackendCapability::unmap_toolhead);
  state_.filabridge_error.reset();
  state_.printers = printers.value();
  state_.printer_revision = next_printer_revision_++;
  return state_;
}

void StationWorkflow::set_spoolman_probe(
    bool online,
    std::optional<core::Error> error) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.spoolman = online ? BackendAvailability::online
                           : BackendAvailability::offline;
  state_.spoolman_error = std::move(error);
}

void StationWorkflow::set_filabridge_probe(
    bool online,
    bool assignment_available,
    std::optional<core::Error> error) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.filabridge = online ? BackendAvailability::online
                             : BackendAvailability::offline;
  state_.filabridge_assignment_available =
      online && assignment_available;
  state_.filabridge_error = std::move(error);
  if (!online) state_.printers.clear();
  if (!online) state_.printer_revision = next_printer_revision_++;
}

core::Result<AssignmentResult> StationWorkflow::assign(
    const std::string& printer_id,
    int backend_toolhead_id,
    bool replace_occupied_confirmed,
    bool advanced_active_print_override,
    const std::vector<config::ToolheadProfile>& profiles,
    std::optional<std::uint64_t> expected_spool_generation,
    std::optional<domain::SpoolId> expected_spool_id,
    ToolheadMutationPrecondition precondition,
    std::optional<std::uint64_t> expected_printer_revision) {
  domain::SpoolId spool_id = 0;
  nfc::openprinttag::MaterialRecord material;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.assignment_error.reset();
    state_.last_assignment.reset();
    state_.compatibility_advisories.clear();
    if ((expected_spool_generation.has_value() &&
         *expected_spool_generation != state_.spool_generation) ||
        (expected_spool_id.has_value() &&
         (!state_.spool.has_value() || state_.spool->id != *expected_spool_id)) ||
        (expected_printer_revision.has_value() &&
         *expected_printer_revision != state_.printer_revision)) {
      const auto error = stale_request(
          "Spool or printer snapshot changed while the assignment was pending; refresh and retry");
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
    if (!state_.spool.has_value()) {
      const core::Error error{
          core::ErrorCategory::configuration,
          "A resolved Spoolman spool is required before assignment",
          false,
      };
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
    if (state_.filabridge == BackendAvailability::offline) {
      const auto error = assignment_unavailable();
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
    if (!state_.filabridge_assignment_available) {
      const auto error = assignment_read_only();
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
    spool_id = state_.spool->id;
    material = state_.material;
    const auto profile = std::find_if(
        profiles.begin(), profiles.end(), [&](const auto& candidate) {
          return candidate.backend_id == backend_toolhead_id;
        });
    if (profile != profiles.end()) {
      if (!profile->enabled) {
        const core::Error error{
            core::ErrorCategory::configuration,
            "Selected local toolhead profile is disabled",
            false,
        };
        state_.assignment_error = error;
        return core::Result<AssignmentResult>::failure(error);
      }
      state_.compatibility_advisories =
          MaterialCompatibilityService::evaluate(material, *profile);
    }
  }

  AssignmentRequest request;
  request.printer_id = printer_id;
  request.backend_toolhead_id = backend_toolhead_id;
  request.spool_id = spool_id;
  request.replace_occupied_confirmed = replace_occupied_confirmed;
  request.advanced_active_print_override = advanced_active_print_override;
  request.precondition = std::move(precondition);
  const auto result = assignment_service_.assign(request);

  std::lock_guard<std::mutex> lock(mutex_);
  if (!result.ok()) {
    state_.assignment_error = result.error();
    if (connection_failure(result.error())) {
      state_.filabridge = BackendAvailability::offline;
      state_.filabridge_assignment_available = false;
    }
    return core::Result<AssignmentResult>::failure(result.error());
  }
  state_.last_assignment = result.value();
  if (result.value().verified()) {
    for (auto& printer : state_.printers) {
      if (printer.id != printer_id) continue;
      for (auto& toolhead : printer.toolheads) {
        if (toolhead.backend_id == backend_toolhead_id) {
          toolhead.assigned_spool = spool_id;
        }
      }
    }
    state_.stage = WorkflowStage::assignment_complete;
    state_.printer_revision = next_printer_revision_++;
  }
  return result;
}

core::Result<AssignmentResult> StationWorkflow::unassign(
    const std::string& printer_id,
    int backend_toolhead_id,
    bool advanced_active_print_override,
    std::optional<std::uint64_t> expected_spool_generation,
    ToolheadMutationPrecondition precondition,
    std::optional<std::uint64_t> expected_printer_revision) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.assignment_error.reset();
    state_.last_assignment.reset();
    if ((expected_spool_generation.has_value() &&
         *expected_spool_generation != state_.spool_generation) ||
        (expected_printer_revision.has_value() &&
         *expected_printer_revision != state_.printer_revision)) {
      const auto error = stale_request(
          "Spool or printer snapshot changed while the unassignment was pending; refresh and retry");
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
    if (state_.filabridge == BackendAvailability::offline) {
      const auto error = assignment_unavailable();
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
    if (!state_.filabridge_assignment_available) {
      const auto error = assignment_read_only();
      state_.assignment_error = error;
      return core::Result<AssignmentResult>::failure(error);
    }
  }

  UnassignmentRequest request;
  request.printer_id = printer_id;
  request.backend_toolhead_id = backend_toolhead_id;
  request.advanced_active_print_override = advanced_active_print_override;
  request.precondition = std::move(precondition);
  const auto result = assignment_service_.unassign(request);

  std::lock_guard<std::mutex> lock(mutex_);
  if (!result.ok()) {
    state_.assignment_error = result.error();
    if (connection_failure(result.error())) {
      state_.filabridge = BackendAvailability::offline;
      state_.filabridge_assignment_available = false;
    }
    return core::Result<AssignmentResult>::failure(result.error());
  }
  state_.last_assignment = result.value();
  if (result.value().verified()) {
    for (auto& printer : state_.printers) {
      if (printer.id != printer_id) continue;
      for (auto& toolhead : printer.toolheads) {
        if (toolhead.backend_id == backend_toolhead_id) {
          toolhead.assigned_spool.reset();
        }
      }
    }
    state_.printer_revision = next_printer_revision_++;
  }
  return result;
}

WorkflowSnapshot StationWorkflow::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

}  // namespace opentag::services

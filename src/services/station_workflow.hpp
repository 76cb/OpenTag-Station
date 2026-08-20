#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "domain/printer.hpp"
#include "domain/spool.hpp"
#include "domain/weight.hpp"
#include "integrations/printer_assignment.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "services/material_compatibility_service.hpp"
#include "services/spool_identity_resolver.hpp"
#include "services/toolhead_assignment_service.hpp"
#include "services/weight_reconciler.hpp"

namespace opentag::services {

enum class WorkflowStage {
  awaiting_spool,
  waiting_for_stable_weight,
  resolving_spool,
  spool_resolution_unavailable,
  spool_not_found,
  spool_selection_required,
  spool_ready,
  assignment_complete,
};

enum class BackendAvailability {
  unknown,
  online,
  offline,
};

struct WorkflowSnapshot {
  std::uint64_t spool_generation{0U};
  std::uint64_t printer_revision{0U};
  WorkflowStage stage{WorkflowStage::awaiting_spool};
  bool openprinttag_available{false};
  nfc::openprinttag::MaterialRecord material;
  nfc::nfcv::Uid uid;
  domain::WeightReading physical_weight;
  std::optional<domain::Spool> spool;
  std::vector<domain::Spool> spool_candidates;
  domain::WeightSnapshot weight_snapshot;
  ReconciliationResult reconciliation;
  BackendAvailability spoolman{BackendAvailability::unknown};
  BackendAvailability filabridge{BackendAvailability::unknown};
  bool filabridge_assignment_available{false};
  std::vector<domain::Printer> printers;
  std::optional<core::Error> spoolman_error;
  std::optional<core::Error> filabridge_error;
  std::optional<core::Error> assignment_error;
  std::optional<AssignmentResult> last_assignment;
  std::vector<CompatibilityAdvisory> compatibility_advisories;
};

class StationWorkflow final {
 public:
  StationWorkflow(
      ISpoolIdentityResolver& spool_resolver,
      integrations::IPrinterAssignmentService& printer_backend)
      : spool_resolver_(spool_resolver),
        printer_backend_(printer_backend),
        assignment_service_(printer_backend) {}

  void clear();
  [[nodiscard]] WorkflowSnapshot accept_identified_spool(
      const nfc::openprinttag::MaterialRecord& material,
      const nfc::nfcv::Uid& uid,
      domain::WeightReading physical_weight,
      domain::EmptyWeightCandidates supplemental_empty_weights,
      ReconciliationTolerances tolerances);
  [[nodiscard]] WorkflowSnapshot refresh_printers();
  void set_spoolman_probe(
      bool online,
      std::optional<core::Error> error = std::nullopt);
  void set_filabridge_probe(
      bool online,
      bool assignment_available,
      std::optional<core::Error> error = std::nullopt);
  [[nodiscard]] core::Result<AssignmentResult> assign(
      const std::string& printer_id,
      int backend_toolhead_id,
      bool replace_occupied_confirmed,
      bool advanced_active_print_override,
      const std::vector<config::ToolheadProfile>& profiles,
      std::optional<std::uint64_t> expected_spool_generation = std::nullopt,
      std::optional<domain::SpoolId> expected_spool_id = std::nullopt,
      ToolheadMutationPrecondition precondition = {},
      std::optional<std::uint64_t> expected_printer_revision = std::nullopt);
  [[nodiscard]] core::Result<AssignmentResult> unassign(
      const std::string& printer_id,
      int backend_toolhead_id,
      bool advanced_active_print_override,
      std::optional<std::uint64_t> expected_spool_generation = std::nullopt,
      ToolheadMutationPrecondition precondition = {},
      std::optional<std::uint64_t> expected_printer_revision = std::nullopt);
  [[nodiscard]] WorkflowSnapshot snapshot() const;

 private:
  mutable std::mutex mutex_;
  ISpoolIdentityResolver& spool_resolver_;
  integrations::IPrinterAssignmentService& printer_backend_;
  ToolheadAssignmentService assignment_service_;
  WorkflowSnapshot state_;
  std::uint64_t next_spool_generation_{1U};
  std::uint64_t next_printer_revision_{1U};
};

}  // namespace opentag::services

#include <unity.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/configuration_service.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "domain/printer.hpp"
#include "domain/spool.hpp"
#include "integrations/backend_capabilities.hpp"
#include "integrations/printer_assignment.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "services/spool_identity_resolver.hpp"
#include "services/station_workflow.hpp"

namespace {

using opentag::config::ToolheadProfile;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::domain::Printer;
using opentag::domain::PrinterState;
using opentag::domain::Spool;
using opentag::domain::SpoolId;
using opentag::domain::Toolhead;
using opentag::integrations::BackendCapabilities;
using opentag::integrations::BackendCapability;
using opentag::integrations::IPrinterAssignmentService;
using opentag::nfc::nfcv::Uid;
using opentag::nfc::openprinttag::MaterialRecord;
using opentag::services::AssignmentOutcome;
using opentag::services::BackendAvailability;
using opentag::services::ISpoolIdentityResolver;
using opentag::services::ReconciliationDecision;
using opentag::services::SpoolMatchSource;
using opentag::services::SpoolResolution;
using opentag::services::SpoolResolutionStatus;
using opentag::services::StationWorkflow;
using opentag::services::WorkflowStage;

class FakeResolver final : public ISpoolIdentityResolver {
 public:
  Result<SpoolResolution> next = Result<SpoolResolution>::success({});
  int calls{0};

  Result<SpoolResolution> resolve(
      const opentag::domain::SpoolIdentity&) override {
    ++calls;
    return next;
  }
};

Printer xl(std::optional<SpoolId> assigned = std::nullopt) {
  Printer result;
  result.id = "xl-stable-id";
  result.display_name = "Workshop XL";
  result.state = PrinterState::idle;
  for (int index = 0; index < 5; ++index) {
    auto toolhead = Toolhead::from_zero_based_backend(result.id, index);
    if (index == 2) toolhead.assigned_spool = assigned;
    result.toolheads.push_back(toolhead);
  }
  return result;
}

class FakePrinterBackend final : public IPrinterAssignmentService {
 public:
  std::vector<Printer> printers{xl()};
  bool offline{false};
  bool mapping_supported{true};
  int assignment_calls{0};

  Result<std::vector<Printer>> list_printers() override {
    if (offline) {
      return Result<std::vector<Printer>>::failure(
          {ErrorCategory::network, "FilaBridge unreachable", true});
    }
    return Result<std::vector<Printer>>::success(printers);
  }

  Result<std::vector<Toolhead>> get_toolheads(
      const std::string& printer_id) override {
    for (const auto& printer : printers) {
      if (printer.id == printer_id) {
        return Result<std::vector<Toolhead>>::success(printer.toolheads);
      }
    }
    return Result<std::vector<Toolhead>>::failure(
        {ErrorCategory::invalid_response, "missing printer", false});
  }

  Result<void> assign_spool(
      const std::string& printer_id,
      int backend_toolhead_id,
      SpoolId spool_id) override {
    ++assignment_calls;
    for (auto& printer : printers) {
      if (printer.id != printer_id) continue;
      for (auto& toolhead : printer.toolheads) {
        if (toolhead.backend_id == backend_toolhead_id) {
          toolhead.assigned_spool = spool_id;
          return Result<void>::success();
        }
      }
    }
    return Result<void>::failure(
        {ErrorCategory::invalid_response, "missing toolhead", false});
  }

  Result<void> unassign_spool(
      const std::string& printer_id,
      int backend_toolhead_id) override {
    for (auto& printer : printers) {
      if (printer.id != printer_id) continue;
      for (auto& toolhead : printer.toolheads) {
        if (toolhead.backend_id == backend_toolhead_id) {
          toolhead.assigned_spool.reset();
          return Result<void>::success();
        }
      }
    }
    return Result<void>::failure(
        {ErrorCategory::invalid_response, "missing toolhead", false});
  }

  BackendCapabilities capabilities() const override {
    BackendCapabilities result;
    result.add(BackendCapability::get_printers);
    result.add(BackendCapability::get_toolheads);
    if (mapping_supported) {
      result.add(BackendCapability::map_toolhead);
      result.add(BackendCapability::unmap_toolhead);
    }
    return result;
  }
};

Spool spool() {
  Spool result;
  result.id = 147;
  result.display_name = "Prusament PETG Orange";
  result.vendor = "Prusament";
  result.material = "PETG";
  result.remaining_grams = 695.0F;
  result.empty_spool_grams = 210.0F;
  return result;
}

MaterialRecord material() {
  MaterialRecord result;
  result.material_name = "PA-CF Orange";
  result.material_abbreviation = "PA-CF";
  result.empty_container_weight = 200.0;
  result.actual_netto_full_weight = 1000.0;
  result.consumed_weight = 300.0;
  return result;
}

ToolheadProfile profile() {
  ToolheadProfile result;
  result.backend_id = 2;
  result.display_name = "T3";
  result.nozzle_material = "brass";
  return result;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_complete_decoded_tag_to_verified_t3_assignment_slice() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched,
      SpoolMatchSource::configured_identity_field,
      {spool()},
  });
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);

  auto state = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});
  TEST_ASSERT_EQUAL(static_cast<int>(WorkflowStage::spool_ready), static_cast<int>(state.stage));
  TEST_ASSERT_TRUE(state.openprinttag_available);
  TEST_ASSERT_TRUE(state.spool.has_value());
  TEST_ASSERT_EQUAL_INT32(147, state.spool->id);
  TEST_ASSERT_TRUE(state.weight_snapshot.empty_spool_grams.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 200.0F, *state.weight_snapshot.empty_spool_grams);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 700.0F, *state.reconciliation.measured_remaining_grams);
  TEST_ASSERT_EQUAL(
      static_cast<int>(ReconciliationDecision::within_tolerance),
      static_cast<int>(state.reconciliation.decision));

  state = workflow.refresh_printers();
  TEST_ASSERT_EQUAL(static_cast<int>(BackendAvailability::online), static_cast<int>(state.filabridge));
  TEST_ASSERT_EQUAL_UINT(5U, state.printers.front().toolheads.size());

  const auto assigned = workflow.assign(
      "xl-stable-id", 2, false, false, {profile()});

  TEST_ASSERT_TRUE(assigned.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(AssignmentOutcome::verified),
      static_cast<int>(assigned.value().outcome));
  state = workflow.snapshot();
  TEST_ASSERT_EQUAL(
      static_cast<int>(WorkflowStage::assignment_complete),
      static_cast<int>(state.stage));
  TEST_ASSERT_EQUAL_INT32(147, *state.printers.front().toolheads[2].assigned_spool);
  TEST_ASSERT_EQUAL_UINT(1U, state.compatibility_advisories.size());
  TEST_ASSERT_EQUAL_INT(1, printers.assignment_calls);
}

void test_unstable_scale_defers_spoolman_resolution() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);

  const auto state = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, false}, {}, {5.0F, 20.0F});

  TEST_ASSERT_EQUAL(
      static_cast<int>(WorkflowStage::waiting_for_stable_weight),
      static_cast<int>(state.stage));
  TEST_ASSERT_TRUE(state.openprinttag_available);
  TEST_ASSERT_EQUAL_INT(0, resolver.calls);
}

void test_spoolman_outage_preserves_tag_and_physical_weight() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::failure(
      {ErrorCategory::network, "Spoolman unreachable", true});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);

  const auto state = workflow.accept_identified_spool(
      material(), Uid{}, {549.0F, true}, {}, {5.0F, 20.0F});

  TEST_ASSERT_EQUAL(
      static_cast<int>(WorkflowStage::spool_resolution_unavailable),
      static_cast<int>(state.stage));
  TEST_ASSERT_EQUAL(
      static_cast<int>(BackendAvailability::offline),
      static_cast<int>(state.spoolman));
  TEST_ASSERT_TRUE(state.openprinttag_available);
  TEST_ASSERT_EQUAL_STRING("PA-CF Orange", state.material.material_name->c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 549.0F, state.physical_weight.gross_grams);
}

void test_ambiguous_spools_are_never_selected_silently() {
  FakeResolver resolver;
  auto second = spool();
  second.id = 204;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::ambiguous,
      SpoolMatchSource::metadata,
      {spool(), second},
  });
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);

  const auto state = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});

  TEST_ASSERT_EQUAL(
      static_cast<int>(WorkflowStage::spool_selection_required),
      static_cast<int>(state.stage));
  TEST_ASSERT_FALSE(state.spool.has_value());
  TEST_ASSERT_EQUAL_UINT(2U, state.spool_candidates.size());
}

void test_filabridge_outage_keeps_identification_and_never_queues_assignment() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched,
      SpoolMatchSource::metadata,
      {spool()},
  });
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto identified = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});
  TEST_ASSERT_TRUE(identified.spool.has_value());
  printers.offline = true;

  const auto offline = workflow.refresh_printers();
  const auto assignment = workflow.assign(
      "xl-stable-id", 2, false, false, {profile()});

  TEST_ASSERT_EQUAL(
      static_cast<int>(BackendAvailability::offline),
      static_cast<int>(offline.filabridge));
  TEST_ASSERT_TRUE(offline.spool.has_value());
  TEST_ASSERT_TRUE(offline.openprinttag_available);
  TEST_ASSERT_FALSE(assignment.ok());
  TEST_ASSERT_EQUAL_INT(0, printers.assignment_calls);
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, assignment.error().message.find("not queued"));
}

void test_unknown_filabridge_version_stays_online_read_only() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched,
      SpoolMatchSource::metadata,
      {spool()},
  });
  FakePrinterBackend printers;
  printers.mapping_supported = false;
  StationWorkflow workflow(resolver, printers);
  const auto identified = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});
  TEST_ASSERT_TRUE(identified.spool.has_value());

  const auto connected = workflow.refresh_printers();
  const auto assignment = workflow.assign(
      "xl-stable-id", 2, false, false, {profile()});

  TEST_ASSERT_EQUAL(
      static_cast<int>(BackendAvailability::online),
      static_cast<int>(connected.filabridge));
  TEST_ASSERT_FALSE(connected.filabridge_assignment_available);
  TEST_ASSERT_FALSE(assignment.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::api_changed),
      static_cast<int>(assignment.error().category));
  TEST_ASSERT_EQUAL_INT(0, printers.assignment_calls);
  const auto after = workflow.snapshot();
  TEST_ASSERT_EQUAL(
      static_cast<int>(BackendAvailability::online),
      static_cast<int>(after.filabridge));
}

void test_disabled_local_toolhead_profile_blocks_assignment() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched,
      SpoolMatchSource::metadata,
      {spool()},
  });
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto identified = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});
  TEST_ASSERT_TRUE(identified.spool.has_value());
  const auto connected = workflow.refresh_printers();
  TEST_ASSERT_TRUE(connected.filabridge_assignment_available);
  auto disabled = profile();
  disabled.enabled = false;

  const auto assignment = workflow.assign(
      "xl-stable-id", 2, false, false, {disabled});

  TEST_ASSERT_FALSE(assignment.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::configuration),
      static_cast<int>(assignment.error().category));
  TEST_ASSERT_EQUAL_INT(0, printers.assignment_calls);
}

void test_workflow_unassignment_uses_shared_verified_safety_path() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  printers.printers = {xl(147)};
  StationWorkflow workflow(resolver, printers);
  const auto connected = workflow.refresh_printers();
  opentag::services::ToolheadMutationPrecondition precondition;
  precondition.supplied = true;
  precondition.expected_previous_spool_id = 147;
  precondition.expected_printer_state = PrinterState::idle;

  const auto result = workflow.unassign(
      "xl-stable-id",
      2,
      false,
      connected.spool_generation,
      precondition);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_TRUE(result.value().verified());
  TEST_ASSERT_FALSE(
      workflow.snapshot().printers.front().toolheads[2].assigned_spool.has_value());
}

void test_stale_workflow_generation_rejects_assignment() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched,
      SpoolMatchSource::metadata,
      {spool()},
  });
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto identified = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});
  const auto connected = workflow.refresh_printers();

  const auto result = workflow.assign(
      "xl-stable-id",
      2,
      false,
      false,
      {profile()},
      identified.spool_generation + 1U,
      147);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(0, printers.assignment_calls);
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, result.error().message.find("changed"));
  TEST_ASSERT_TRUE(connected.filabridge_assignment_available);
}

void test_stale_printer_revision_rejects_assignment() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched,
      SpoolMatchSource::metadata,
      {spool()},
  });
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto identified = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, true}, {}, {5.0F, 20.0F});
  const auto connected = workflow.refresh_printers();

  const auto result = workflow.assign(
      "xl-stable-id",
      2,
      false,
      false,
      {profile()},
      identified.spool_generation,
      147,
      {},
      connected.printer_revision + 1U);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(0, printers.assignment_calls);
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, result.error().message.find("changed"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_complete_decoded_tag_to_verified_t3_assignment_slice);
  RUN_TEST(test_unstable_scale_defers_spoolman_resolution);
  RUN_TEST(test_spoolman_outage_preserves_tag_and_physical_weight);
  RUN_TEST(test_ambiguous_spools_are_never_selected_silently);
  RUN_TEST(test_filabridge_outage_keeps_identification_and_never_queues_assignment);
  RUN_TEST(test_unknown_filabridge_version_stays_online_read_only);
  RUN_TEST(test_disabled_local_toolhead_profile_blocks_assignment);
  RUN_TEST(test_workflow_unassignment_uses_shared_verified_safety_path);
  RUN_TEST(test_stale_workflow_generation_rejects_assignment);
  RUN_TEST(test_stale_printer_revision_rejects_assignment);
  return UNITY_END();
}

#include <unity.h>

#include <cmath>
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
#include "services/nfc_workflow_handoff.hpp"
#include <functional>

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
  std::function<void()> during_resolve;

  Result<SpoolResolution> resolve(
      const opentag::domain::SpoolIdentity&) override {
    ++calls;
    if (during_resolve) during_resolve();
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
  std::function<void()> during_mapping;

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
    if (during_mapping) during_mapping();
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
    if (during_mapping) during_mapping();
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
  result.empty_spool_grams = 200.0F;
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

void test_unstable_scale_still_resolves_spool() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched, SpoolMatchSource::configured_identity_field,
      {spool()}});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);

  const auto state = workflow.accept_identified_spool(
      material(), Uid{}, {900.0F, false}, {}, {5.0F, 20.0F});

  TEST_ASSERT_EQUAL(
      static_cast<int>(WorkflowStage::spool_ready),
      static_cast<int>(state.stage));
  TEST_ASSERT_TRUE(state.openprinttag_available);
  TEST_ASSERT_EQUAL_INT(1, resolver.calls);
  TEST_ASSERT_FALSE(state.physical_weight.stable);
  TEST_ASSERT_FALSE(state.reconciliation.measured_remaining_grams.has_value());
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

void test_nfc_handoff_submits_once_without_weight_and_clears() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  int submissions = 0;
  bool submitted_weight_stable = true;
  opentag::services::NfcWorkflowHandoff handoff(workflow,
      [&](const opentag::nfc::IdentifiedTag& tag, opentag::domain::WeightReading weight, std::uint64_t generation) {
        ++submissions;
        submitted_weight_stable = weight.stable;
        (void)workflow.accept_identified_spool(tag.decoded.material,tag.uid,weight,{}, {},generation);
        return true;
      });
  auto tag = std::make_shared<opentag::nfc::IdentifiedTag>();
  tag->generation = 1;
  opentag::nfc::ReadSnapshot s;
  s.tag = tag;
  handoff.observe(s,std::nullopt);
  const auto generation = workflow.snapshot().spool_generation;
  TEST_ASSERT_TRUE(workflow.snapshot().openprinttag_available);
  // Looked up at once, without a weight.
  TEST_ASSERT_EQUAL(1,submissions);
  TEST_ASSERT_FALSE(submitted_weight_stable);
  handoff.observe(s,opentag::domain::WeightReading{900,false});
  TEST_ASSERT_FALSE(handoff.weight_attached());
  for (int i=0;i<20;++i) handoff.observe(s,opentag::domain::WeightReading{900,true});
  TEST_ASSERT_EQUAL(1,submissions);
  TEST_ASSERT_TRUE(handoff.weight_attached());
  TEST_ASSERT_TRUE(workflow.snapshot().physical_weight.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 900.0F, workflow.snapshot().physical_weight.gross_grams);
  TEST_ASSERT_EQUAL(generation,workflow.snapshot().spool_generation);
  s.tag.reset();
  handoff.observe(s,std::nullopt);
  TEST_ASSERT_FALSE(workflow.snapshot().openprinttag_available);
  const auto cleared = workflow.snapshot().spool_generation;
  handoff.observe(s,std::nullopt);
  TEST_ASSERT_EQUAL(cleared,workflow.snapshot().spool_generation);
}

void test_uncalibrated_scale_still_resolves_spool() {
  // An uncalibrated scale or a failed automatic weigh never supplies a weight.
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched, SpoolMatchSource::configured_identity_field,
      {spool()}});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  int submissions = 0;
  opentag::services::NfcWorkflowHandoff handoff(workflow,
      [&](const opentag::nfc::IdentifiedTag& tag, opentag::domain::WeightReading weight, std::uint64_t generation) {
        ++submissions;
        (void)workflow.accept_identified_spool(tag.decoded.material,tag.uid,weight,{}, {5.0F, 20.0F},generation);
        return true;
      });
  auto tag = std::make_shared<opentag::nfc::IdentifiedTag>();
  tag->generation = 3;
  tag->decoded.material = material();
  opentag::nfc::ReadSnapshot s;
  s.tag = tag;
  for (std::uint32_t t = 0; t < 60000U; t += 250U) handoff.observe(s, std::nullopt, t);
  TEST_ASSERT_EQUAL(1, submissions);
  auto state = workflow.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_ready), static_cast<int>(state.stage));
  TEST_ASSERT_TRUE(state.spool.has_value());
  TEST_ASSERT_EQUAL_INT32(147, state.spool->id);
  TEST_ASSERT_FALSE(state.reconciliation.measured_remaining_grams.has_value());
  // A weight measured later is attached for reconciliation only.
  handoff.observe(s, opentag::domain::WeightReading{900, true}, 60000U);
  TEST_ASSERT_EQUAL(1, submissions);
  state = workflow.snapshot();
  TEST_ASSERT_TRUE(state.reconciliation.measured_remaining_grams.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 700.0F, *state.reconciliation.measured_remaining_grams);
}

void test_lookup_repeats_once_spoolman_returns() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::failure(
      {ErrorCategory::network, "Spoolman unreachable", true});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  // Queue model: submissions are queued and run later, like the backend owner.
  std::vector<std::pair<opentag::nfc::IdentifiedTag, std::uint64_t>> queue;
  opentag::services::NfcWorkflowHandoff handoff(workflow,
      [&](const opentag::nfc::IdentifiedTag& tag, opentag::domain::WeightReading, std::uint64_t generation) {
        queue.emplace_back(tag, generation);
        return true;
      });
  auto drain = [&] {
    for (auto& entry : queue)
      (void)workflow.accept_identified_spool(entry.first.decoded.material, entry.first.uid,
          {NAN, false}, {}, {5.0F, 20.0F}, entry.second);
    queue.clear();
  };
  auto tag = std::make_shared<opentag::nfc::IdentifiedTag>();
  tag->generation = 5;
  tag->decoded.material = material();
  opentag::nfc::ReadSnapshot s;
  s.tag = tag;
  handoff.observe(s, std::nullopt, 0U);
  TEST_ASSERT_EQUAL_UINT(1U, queue.size());
  drain();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_resolution_unavailable),
      static_cast<int>(workflow.snapshot().stage));
  // Spoolman still offline: no retry however long the tag stays.
  for (std::uint32_t t = 250U; t < 60000U; t += 250U) handoff.observe(s, std::nullopt, t);
  TEST_ASSERT_EQUAL_UINT(0U, queue.size());
  TEST_ASSERT_EQUAL_INT(1, resolver.calls);
  // The backend probe reports Spoolman reachable again: one lookup is queued.
  workflow.set_spoolman_probe(true);
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched, SpoolMatchSource::configured_identity_field,
      {spool()}});
  for (std::uint32_t t = 60000U; t < 90000U; t += 250U) handoff.observe(s, std::nullopt, t);
  TEST_ASSERT_EQUAL_UINT(1U, queue.size());  // never two concurrent lookups
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::resolving_spool),
      static_cast<int>(workflow.snapshot().stage));
  drain();
  TEST_ASSERT_EQUAL_INT(2, resolver.calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_ready),
      static_cast<int>(workflow.snapshot().stage));
  for (std::uint32_t t = 90000U; t < 150000U; t += 250U) handoff.observe(s, std::nullopt, t);
  TEST_ASSERT_EQUAL_UINT(0U, queue.size());
}

void test_lookup_retry_is_spaced_and_requeued_when_queue_is_full() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::failure(
      {ErrorCategory::invalid_response, "Spoolman answered 500", true});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  int submissions = 0;
  bool accept = true;
  opentag::services::NfcWorkflowHandoff handoff(workflow,
      [&](const opentag::nfc::IdentifiedTag& tag, opentag::domain::WeightReading weight, std::uint64_t generation) {
        ++submissions;
        if (!accept) return false;
        (void)workflow.accept_identified_spool(tag.decoded.material, tag.uid, weight, {}, {}, generation);
        return true;
      });
  auto tag = std::make_shared<opentag::nfc::IdentifiedTag>();
  tag->generation = 6;
  opentag::nfc::ReadSnapshot s;
  s.tag = tag;
  handoff.observe(s, std::nullopt, 1000U);
  TEST_ASSERT_EQUAL(1, submissions);
  // Even with every probe reporting Spoolman online, a failing lookup is
  // repeated at most once per retry interval (at 11, 21, 31 and 41 s).
  for (std::uint32_t t = 1000U; t <= 41000U; t += 250U) {
    workflow.set_spoolman_probe(true);
    handoff.observe(s, std::nullopt, t);
  }
  TEST_ASSERT_EQUAL(5, submissions);
  // A retry that cannot be queued shows the failure again and is tried later.
  accept = false;
  workflow.set_spoolman_probe(true);
  handoff.observe(s, std::nullopt, 52000U);
  TEST_ASSERT_EQUAL(6, submissions);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_resolution_unavailable),
      static_cast<int>(workflow.snapshot().stage));
}

void test_scan_report_is_claimed_once_per_placement() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto generation = workflow.begin_identified_spool(material(), Uid{});
  TEST_ASSERT_TRUE(workflow.claim_scan_report(generation));
  TEST_ASSERT_FALSE(workflow.claim_scan_report(generation));  // retried lookup
  TEST_ASSERT_FALSE(workflow.claim_scan_report(0U));
  workflow.clear();
  TEST_ASSERT_FALSE(workflow.claim_scan_report(generation));  // tag removed
  const auto next = workflow.begin_identified_spool(material(), Uid{});
  TEST_ASSERT_TRUE(workflow.claim_scan_report(next));
}

void test_failed_resolution_workspace_never_stays_resolving() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto generation = workflow.begin_identified_spool(material(), Uid{});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::resolving_spool),
      static_cast<int>(workflow.snapshot().stage));
  workflow.fail_resolution(generation, {ErrorCategory::backend_unavailable, "no workspace", true});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_resolution_unavailable),
      static_cast<int>(workflow.snapshot().stage));
  workflow.set_spoolman_probe(true);
  TEST_ASSERT_TRUE(workflow.begin_lookup_retry(generation));
  TEST_ASSERT_FALSE(workflow.begin_lookup_retry(generation));
}

void test_removed_tag_rejects_queued_and_inflight_resolution() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver,printers);
  const auto record = material();
  const auto generation = workflow.begin_identified_spool(record,Uid{});
  workflow.clear();
  (void)workflow.accept_identified_spool(record,Uid{},{900,true},{},{},generation);
  TEST_ASSERT_EQUAL(0,resolver.calls);
  const auto next = workflow.begin_identified_spool(record,Uid{});
  resolver.during_resolve = [&](){workflow.clear();};
  (void)workflow.accept_identified_spool(record,Uid{},{900,true},{},{},next);
  TEST_ASSERT_EQUAL(1,resolver.calls);
  TEST_ASSERT_FALSE(workflow.snapshot().openprinttag_available);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::awaiting_spool),static_cast<int>(workflow.snapshot().stage));
}

void test_mapping_completion_does_not_restore_removed_or_replaced_tag() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched, SpoolMatchSource::configured_identity_field,
      {spool()}});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  (void)workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {});
  (void)workflow.refresh_printers();
  printers.during_mapping = [&] { workflow.clear(); };
  const auto assigned = workflow.assign("xl-stable-id", 2, false, false, {});
  TEST_ASSERT_TRUE(assigned.ok());
  TEST_ASSERT_TRUE(assigned.value().verified());
  TEST_ASSERT_FALSE(workflow.snapshot().openprinttag_available);
  TEST_ASSERT_FALSE(workflow.snapshot().last_assignment.has_value());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::awaiting_spool),
      static_cast<int>(workflow.snapshot().stage));

  (void)workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {});
  (void)workflow.refresh_printers();
  Uid replacement{};
  replacement.bytes[7] = 1;
  printers.during_mapping = [&] { workflow.begin_identified_spool(material(), replacement); };
  const auto unassigned = workflow.unassign("xl-stable-id", 2, false);
  TEST_ASSERT_TRUE(unassigned.ok());
  TEST_ASSERT_TRUE(unassigned.value().verified());
  TEST_ASSERT_TRUE(workflow.snapshot().uid == replacement);
  TEST_ASSERT_FALSE(workflow.snapshot().last_assignment.has_value());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::resolving_spool),
      static_cast<int>(workflow.snapshot().stage));
}

void test_removal_preserves_discovery_and_reinsertion_can_assign() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({SpoolResolutionStatus::matched,
      SpoolMatchSource::configured_identity_field, {spool()}});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  (void)workflow.refresh_printers();
  workflow.clear();
  TEST_ASSERT_EQUAL_UINT(1, workflow.snapshot().printers.size());
  TEST_ASSERT_TRUE(workflow.snapshot().filabridge_assignment_available);
  (void)workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {});
  const auto assigned = workflow.assign("xl-stable-id", 2, false, false, {});
  TEST_ASSERT_TRUE(assigned.ok());
  TEST_ASSERT_TRUE(assigned.value().verified());
}
void test_explicit_spool_confirmation_is_generation_fenced() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto generation = workflow.begin_identified_spool(material(), Uid{});
  const auto selected = spool();
  const auto confirmed = workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {}, generation, &selected);
  TEST_ASSERT_TRUE(confirmed.spool.has_value());
  TEST_ASSERT_EQUAL(0, resolver.calls);
  workflow.clear();
  (void)workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {}, generation, &selected);
  TEST_ASSERT_FALSE(workflow.snapshot().spool.has_value());
}
void assert_readback_tolerances(opentag::services::ReconciliationTolerances tolerances) {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched, SpoolMatchSource::configured_identity_field, {spool()}});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  const auto original = workflow.accept_identified_spool(
      material(), Uid{}, {900, true}, {}, tolerances);
  TEST_ASSERT_TRUE(original.spool.has_value());
  const std::pair<float, ReconciliationDecision> boundaries[] = {
      {tolerances.normal_grams, ReconciliationDecision::within_tolerance},
      {tolerances.normal_grams + 1, ReconciliationDecision::warning},
      {tolerances.warning_grams, ReconciliationDecision::warning},
      {tolerances.warning_grams + 1, ReconciliationDecision::confirmation_required}};
  for (const auto& boundary : boundaries) {
    // Successful Spoolman readback agrees with the captured physical weight.
    // The unchanged tag is still 700 g; its difference exposes both thresholds.
    auto verified = spool();
    verified.remaining_grams = 700 + boundary.first;
    verified.used_grams = 1000 - *verified.remaining_grams;
    workflow.apply_weight_readback(original.spool_generation, verified,
                                   900 + boundary.first, tolerances);
    const auto state = workflow.snapshot();
    TEST_ASSERT_EQUAL_FLOAT(*verified.remaining_grams, *state.spool->remaining_grams);
    TEST_ASSERT_EQUAL_FLOAT(0, *state.reconciliation.spoolman_difference_grams);
    TEST_ASSERT_EQUAL_FLOAT(boundary.first, *state.reconciliation.tag_difference_grams);
    TEST_ASSERT_EQUAL(static_cast<int>(boundary.second), static_cast<int>(state.reconciliation.decision));
  }
  // Reconciliation is local: no repeated resolution or backend assignment.
  TEST_ASSERT_EQUAL(1, resolver.calls);
  TEST_ASSERT_EQUAL(0, printers.assignment_calls);
}
void test_readback_preserves_custom_two_and_ten_gram_tolerances() {
  assert_readback_tolerances({2, 10});
}
void test_readback_preserves_default_five_and_twenty_gram_tolerances() {
  const opentag::services::ReconciliationTolerances defaults;
  TEST_ASSERT_EQUAL_FLOAT(5, defaults.normal_grams);
  TEST_ASSERT_EQUAL_FLOAT(20, defaults.warning_grams);
  assert_readback_tolerances(defaults);
}
void test_verified_mapping_updates_the_printer_cache_after_tag_removal() {
  FakeResolver resolver;
  resolver.next = Result<SpoolResolution>::success({
      SpoolResolutionStatus::matched, SpoolMatchSource::configured_identity_field,
      {spool()}});
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  (void)workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {});
  (void)workflow.refresh_printers();
  const auto revision_before = workflow.snapshot().printer_revision;
  printers.during_mapping = [&] { workflow.clear(); };
  const auto assigned = workflow.assign("xl-stable-id", 2, false, false, {});
  TEST_ASSERT_TRUE(assigned.ok());
  TEST_ASSERT_TRUE(assigned.value().verified());

  // FilaBridge now maps the toolhead; the cached printer must say so even
  // though the tag left the station while the request was in flight.
  auto snapshot = workflow.snapshot();
  TEST_ASSERT_EQUAL_UINT(1U, snapshot.printers.size());
  TEST_ASSERT_TRUE(snapshot.printers[0].toolheads[2].assigned_spool.has_value());
  TEST_ASSERT_EQUAL_INT(147, *snapshot.printers[0].toolheads[2].assigned_spool);
  TEST_ASSERT_TRUE(snapshot.printer_revision != revision_before);
  // The removed tag's own result is still not applied.
  TEST_ASSERT_FALSE(snapshot.last_assignment.has_value());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::awaiting_spool),
      static_cast<int>(snapshot.stage));

  (void)workflow.accept_identified_spool(material(), Uid{}, {900, true}, {}, {});
  const auto revision_assigned = workflow.snapshot().printer_revision;
  printers.during_mapping = [&] { workflow.clear(); };
  const auto unassigned = workflow.unassign("xl-stable-id", 2, false);
  TEST_ASSERT_TRUE(unassigned.ok());
  TEST_ASSERT_TRUE(unassigned.value().verified());
  snapshot = workflow.snapshot();
  TEST_ASSERT_FALSE(snapshot.printers[0].toolheads[2].assigned_spool.has_value());
  TEST_ASSERT_TRUE(snapshot.printer_revision != revision_assigned);
  TEST_ASSERT_FALSE(snapshot.last_assignment.has_value());
}

void test_blank_tag_linked_in_spoolman_is_shown_as_its_spool() {
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  Spool linked;
  linked.id = 31;
  linked.filament_id = 4;
  linked.display_name = "Acme PLA Blue";
  linked.remaining_grams = 640.0F;
  resolver.next = Result<SpoolResolution>::success(
      {SpoolResolutionStatus::matched, opentag::services::SpoolMatchSource::native_tag, {linked}});
  int submissions = 0;
  opentag::services::NfcWorkflowHandoff handoff(workflow,
      [&](const opentag::nfc::IdentifiedTag& tag, opentag::domain::WeightReading weight, std::uint64_t generation) {
        ++submissions;
        TEST_ASSERT_FALSE(tag.decoded.material.material_name.has_value());
        // While it is looked up the tag stays shown as blank.
        TEST_ASSERT_FALSE(workflow.snapshot().openprinttag_available);
        TEST_ASSERT_TRUE(workflow.snapshot().uid_only);
        (void)workflow.accept_identified_spool(tag.decoded.material, tag.uid, weight, {}, {}, generation);
        return true;
      });
  opentag::nfc::ReadSnapshot s;
  s.state = opentag::nfc::ReadState::blank;
  s.present = true;
  s.blank_compatible = true;
  s.uid = Uid{{0xE0, 0x04, 1, 2, 3, 4, 5, 6}};
  s.generation = 4;
  handoff.observe(s, std::nullopt);
  TEST_ASSERT_EQUAL(1, submissions);
  const auto snapshot = workflow.snapshot();
  TEST_ASSERT_TRUE(snapshot.openprinttag_available);
  TEST_ASSERT_TRUE(snapshot.spool.has_value());
  TEST_ASSERT_EQUAL(31, snapshot.spool->id);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_ready), static_cast<int>(snapshot.stage));
  // Removal clears it like any other tag.
  s.present = false;
  s.uid.reset();
  handoff.observe(s, std::nullopt);
  TEST_ASSERT_FALSE(workflow.snapshot().openprinttag_available);
  TEST_ASSERT_FALSE(workflow.snapshot().spool.has_value());
}

void test_unlinked_blank_or_other_tag_stays_hidden() {
  for (auto status : {SpoolResolutionStatus::not_found, SpoolResolutionStatus::ambiguous,
                      SpoolResolutionStatus::conflict}) {
    FakeResolver resolver;
    FakePrinterBackend printers;
    StationWorkflow workflow(resolver, printers);
    Spool candidate;
    candidate.id = 8;
    resolver.next = Result<SpoolResolution>::success(
        {status, opentag::services::SpoolMatchSource::native_tag, {candidate, candidate}});
    const Uid uid{{0xE0, 0x04, 9, 9, 9, 9, 9, 9}};
    const auto generation = workflow.begin_identified_spool(material(), uid, true);
    const auto result = workflow.accept_identified_spool(material(), uid, {900, true}, {}, {}, generation);
    TEST_ASSERT_EQUAL(1, resolver.calls);
    TEST_ASSERT_FALSE(result.openprinttag_available);
    TEST_ASSERT_FALSE(result.spool.has_value());
    TEST_ASSERT_TRUE(result.spool_candidates.empty());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkflowStage::spool_not_found), static_cast<int>(result.stage));
  }
  // A tag with other (non-OpenPrintTag) data is looked up the same way.
  FakeResolver resolver;
  FakePrinterBackend printers;
  StationWorkflow workflow(resolver, printers);
  int submissions = 0;
  opentag::services::NfcWorkflowHandoff handoff(workflow,
      [&](const opentag::nfc::IdentifiedTag&, opentag::domain::WeightReading, std::uint64_t) {
        ++submissions;
        return true;
      });
  opentag::nfc::ReadSnapshot s;
  s.state = opentag::nfc::ReadState::unsupported;
  s.present = true;
  s.uid = Uid{{0xE0, 0x04, 1, 2, 3, 4, 5, 6}};
  s.generation = 2;
  handoff.observe(s, opentag::domain::WeightReading{900, true});
  TEST_ASSERT_EQUAL(1, submissions);
  // Still being read: nothing is looked up yet.
  opentag::nfc::ReadSnapshot reading;
  reading.state = opentag::nfc::ReadState::stabilizing;
  reading.present = true;
  reading.uid = s.uid;
  StationWorkflow quiet(resolver, printers);
  int quiet_submissions = 0;
  opentag::services::NfcWorkflowHandoff idle(quiet,
      [&](const opentag::nfc::IdentifiedTag&, opentag::domain::WeightReading, std::uint64_t) {
        ++quiet_submissions;
        return true;
      });
  idle.observe(reading, opentag::domain::WeightReading{900, true});
  TEST_ASSERT_EQUAL(0, quiet_submissions);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_readback_preserves_custom_two_and_ten_gram_tolerances);
  RUN_TEST(test_blank_tag_linked_in_spoolman_is_shown_as_its_spool);
  RUN_TEST(test_unlinked_blank_or_other_tag_stays_hidden);
  RUN_TEST(test_readback_preserves_default_five_and_twenty_gram_tolerances);
  RUN_TEST(test_removal_preserves_discovery_and_reinsertion_can_assign);
  RUN_TEST(test_explicit_spool_confirmation_is_generation_fenced);
  RUN_TEST(test_complete_decoded_tag_to_verified_t3_assignment_slice);
  RUN_TEST(test_unstable_scale_still_resolves_spool);
  RUN_TEST(test_spoolman_outage_preserves_tag_and_physical_weight);
  RUN_TEST(test_ambiguous_spools_are_never_selected_silently);
  RUN_TEST(test_filabridge_outage_keeps_identification_and_never_queues_assignment);
  RUN_TEST(test_unknown_filabridge_version_stays_online_read_only);
  RUN_TEST(test_disabled_local_toolhead_profile_blocks_assignment);
  RUN_TEST(test_workflow_unassignment_uses_shared_verified_safety_path);
  RUN_TEST(test_stale_workflow_generation_rejects_assignment);
  RUN_TEST(test_stale_printer_revision_rejects_assignment);
  RUN_TEST(test_nfc_handoff_submits_once_without_weight_and_clears);
  RUN_TEST(test_uncalibrated_scale_still_resolves_spool);
  RUN_TEST(test_lookup_repeats_once_spoolman_returns);
  RUN_TEST(test_lookup_retry_is_spaced_and_requeued_when_queue_is_full);
  RUN_TEST(test_scan_report_is_claimed_once_per_placement);
  RUN_TEST(test_failed_resolution_workspace_never_stays_resolving);
  RUN_TEST(test_removed_tag_rejects_queued_and_inflight_resolution);
  RUN_TEST(test_mapping_completion_does_not_restore_removed_or_replaced_tag);
  RUN_TEST(test_verified_mapping_updates_the_printer_cache_after_tag_removal);
  return UNITY_END();
}

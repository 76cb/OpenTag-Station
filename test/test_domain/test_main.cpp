#include <unity.h>

#include "application/state_machine.hpp"
#include "application/task_contracts.hpp"
#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "domain/printer.hpp"
#include "domain/weight.hpp"
#include "events/application_event.hpp"
#include "hardware/nfc/st25r3916b/wiring_guard.hpp"
#include "integrations/backend_capabilities.hpp"
#include "services/weight_reconciler.hpp"

using opentag::domain::EmptyWeightSource;
using opentag::domain::EmptyWeightCandidates;
using opentag::domain::EmptyWeightResolver;
using opentag::domain::Toolhead;
using opentag::domain::WeightSnapshot;
using opentag::events::ApplicationEvent;
using opentag::events::EventQueue;
using opentag::events::EventType;
using opentag::integrations::BackendCapabilities;
using opentag::integrations::BackendCapability;
using opentag::services::ReconciliationDecision;
using opentag::services::ReconciliationTolerances;
using opentag::services::WeightReconciler;
using opentag::application::ApplicationState;
using opentag::application::ApplicationStateMachine;
using opentag::application::TagPresenceMachine;
using opentag::application::TagPresenceState;

void setUp() {}
void tearDown() {}

void test_toolhead_numbering_is_translated_once() {
  const auto toolhead = Toolhead::from_zero_based_backend("xl", 4);
  TEST_ASSERT_EQUAL_INT(4, toolhead.backend_id);
  TEST_ASSERT_EQUAL_INT(5, toolhead.display_number);
  TEST_ASSERT_EQUAL_STRING("T5", toolhead.display_name.c_str());
}

void test_unstable_weight_cannot_reconcile() {
  WeightSnapshot snapshot;
  snapshot.physical = {741.8F, false};
  snapshot.empty_spool_grams = 192.7F;
  snapshot.empty_weight_source = EmptyWeightSource::openprinttag;

  const auto result = WeightReconciler::compare(snapshot, 5.0F);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReconciliationDecision::unavailable),
      static_cast<int>(result.decision));
}

void test_large_weight_difference_requires_confirmation() {
  WeightSnapshot snapshot;
  snapshot.physical = {741.8F, true};
  snapshot.empty_spool_grams = 192.7F;
  snapshot.empty_weight_source = EmptyWeightSource::openprinttag;
  snapshot.spoolman_remaining_grams = 510.8F;
  snapshot.tag_remaining_grams = 511.0F;

  const auto result = WeightReconciler::compare(snapshot, 5.0F);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 549.1F, *result.measured_remaining_grams);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReconciliationDecision::confirmation_required),
      static_cast<int>(result.decision));
}

void test_empty_weight_resolver_uses_documented_priority_and_exposes_source() {
  EmptyWeightCandidates candidates;
  candidates.openprinttag_grams = 190.0F;
  candidates.spoolman_spool_grams = 191.0F;
  candidates.package_default_grams = 192.0F;
  candidates.vendor_default_grams = 193.0F;
  candidates.manual_grams = 194.0F;
  const auto resolved = EmptyWeightResolver::resolve(candidates);
  TEST_ASSERT_TRUE(resolved.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 190.0F, resolved->grams);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(EmptyWeightSource::openprinttag),
      static_cast<int>(resolved->source));

  candidates.openprinttag_grams = -1.0F;
  const auto fallback = EmptyWeightResolver::resolve(candidates);
  TEST_ASSERT_TRUE(fallback.has_value());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(EmptyWeightSource::spoolman_spool),
      static_cast<int>(fallback->source));
}

void test_weight_model_rejects_negative_net_and_reconciliation_has_three_bands() {
  WeightSnapshot invalid;
  invalid.physical = {100.0F, true};
  invalid.empty_spool_grams = 200.0F;
  TEST_ASSERT_FALSE(invalid.physical_remaining_grams().has_value());

  WeightSnapshot snapshot;
  snapshot.physical = {700.0F, true};
  snapshot.empty_spool_grams = 200.0F;
  snapshot.empty_weight_source = EmptyWeightSource::manual;
  snapshot.spoolman_remaining_grams = 496.0F;
  snapshot.tag_remaining_grams = 493.0F;
  auto result = WeightReconciler::compare(
      snapshot, ReconciliationTolerances{5.0F, 20.0F});
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReconciliationDecision::warning),
      static_cast<int>(result.decision));
  TEST_ASSERT_FLOAT_WITHIN(
      0.01F, 7.0F, *result.maximum_absolute_difference_grams);

  snapshot.tag_remaining_grams = 470.0F;
  result = WeightReconciler::compare(
      snapshot, ReconciliationTolerances{5.0F, 20.0F});
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ReconciliationDecision::confirmation_required),
      static_cast<int>(result.decision));
}

void test_capabilities_are_explicit() {
  BackendCapabilities capabilities;
  capabilities.add(BackendCapability::get_toolheads);
  TEST_ASSERT_TRUE(capabilities.has(BackendCapability::get_toolheads));
  TEST_ASSERT_FALSE(capabilities.has(BackendCapability::map_toolhead));
}

void test_event_queue_is_bounded_and_fifo() {
  EventQueue<2> queue;
  TEST_ASSERT_TRUE(queue.push({EventType::tag_detected, 1}));
  TEST_ASSERT_TRUE(queue.push({EventType::weight_stable, 2}));
  TEST_ASSERT_FALSE(queue.push({EventType::spool_resolved, 3}));

  ApplicationEvent event{EventType::ota_failed, 0};
  TEST_ASSERT_TRUE(queue.pop(event));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EventType::tag_detected), static_cast<int>(event.type));
  TEST_ASSERT_EQUAL_INT(1, event.value);
}

void test_unresolved_nfc_wiring_stays_disabled() {
  TEST_ASSERT_FALSE(opentag::hardware::nfc::st25r3916b_wiring_complete);
}

void test_wt32_display_pipeline_contract_is_explicit() {
  using Board = opentag::boards::Wt32Sc01PlusRevA;
  TEST_ASSERT_EQUAL_UINT16(320U, Board::display_native_width);
  TEST_ASSERT_EQUAL_UINT16(480U, Board::display_native_height);
  TEST_ASSERT_EQUAL_UINT16(480U, Board::display_width);
  TEST_ASSERT_EQUAL_UINT16(320U, Board::display_height);
  TEST_ASSERT_EQUAL_UINT8(1U, Board::display_rotation);
  TEST_ASSERT_TRUE(Board::display_invert);
  TEST_ASSERT_FALSE(Board::display_rgb_order);
  TEST_ASSERT_TRUE(Board::display_swap_bytes);
}

void test_application_state_machine_rejects_skipped_workflow_steps() {
  ApplicationStateMachine machine;
  TEST_ASSERT_TRUE(machine.transition(ApplicationState::idle).ok());
  const auto skipped = machine.transition(ApplicationState::spool_ready);
  TEST_ASSERT_FALSE(skipped.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ApplicationState::idle),
      static_cast<int>(machine.state()));

  TEST_ASSERT_TRUE(machine.transition(ApplicationState::tag_detected).ok());
  TEST_ASSERT_TRUE(machine.transition(ApplicationState::reading_tag).ok());
  TEST_ASSERT_TRUE(machine.transition(ApplicationState::tag_parsed).ok());
  TEST_ASSERT_TRUE(machine.transition(ApplicationState::resolving_spool).ok());
  TEST_ASSERT_TRUE(machine.transition(ApplicationState::spool_ready).ok());
}

void test_stationary_tag_triggers_exactly_once_until_removed() {
  TagPresenceMachine presence(50U);
  TEST_ASSERT_FALSE(presence.observe(true, 100U));
  TEST_ASSERT_TRUE(presence.observe(true, 150U));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(TagPresenceState::tag_present),
      static_cast<int>(presence.state()));
  TEST_ASSERT_TRUE(presence.should_begin_read());
  TEST_ASSERT_TRUE(presence.mark_processed());
  TEST_ASSERT_FALSE(presence.should_begin_read());

  TEST_ASSERT_FALSE(presence.observe(true, 1000U));
  TEST_ASSERT_FALSE(presence.observe(false, 1100U));
  TEST_ASSERT_TRUE(presence.observe(false, 1150U));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(TagPresenceState::tag_removed),
      static_cast<int>(presence.state()));
  TEST_ASSERT_TRUE(presence.acknowledge_removed());

  TEST_ASSERT_FALSE(presence.observe(true, 1200U));
  TEST_ASSERT_TRUE(presence.observe(true, 1250U));
  TEST_ASSERT_TRUE(presence.should_begin_read());
}

void test_every_runtime_owner_has_bounded_queue_and_deadline() {
  TEST_ASSERT_EQUAL_UINT(9U, opentag::application::task_contracts.size());
  for (const auto& contract : opentag::application::task_contracts) {
    TEST_ASSERT_GREATER_THAN_UINT(0U, contract.command_queue_depth);
    TEST_ASSERT_GREATER_THAN_UINT(0U, contract.maximum_block_ms);
    TEST_ASSERT_NOT_NULL(contract.name);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_toolhead_numbering_is_translated_once);
  RUN_TEST(test_unstable_weight_cannot_reconcile);
  RUN_TEST(test_large_weight_difference_requires_confirmation);
  RUN_TEST(test_empty_weight_resolver_uses_documented_priority_and_exposes_source);
  RUN_TEST(test_weight_model_rejects_negative_net_and_reconciliation_has_three_bands);
  RUN_TEST(test_capabilities_are_explicit);
  RUN_TEST(test_event_queue_is_bounded_and_fifo);
  RUN_TEST(test_unresolved_nfc_wiring_stays_disabled);
  RUN_TEST(test_wt32_display_pipeline_contract_is_explicit);
  RUN_TEST(test_application_state_machine_rejects_skipped_workflow_steps);
  RUN_TEST(test_stationary_tag_triggers_exactly_once_until_removed);
  RUN_TEST(test_every_runtime_owner_has_bounded_queue_and_deadline);
  return UNITY_END();
}

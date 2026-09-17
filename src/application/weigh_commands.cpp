#include "application/backend_worker.hpp"
#include "application/nfc_worker.hpp"
#include <Arduino.h>
namespace opentag::application {
void BackendWorker::begin_weigh(std::uint64_t id) {
  services::WeighSyncSnapshot captured;
  captured.measurement_id = id;
  configuration_.visit([&](const auto &config, auto revision) {
    captured.automatic = config.reconciliation.auto_update_after_weigh;
    captured.tolerance = config.reconciliation.normal_tolerance_grams;
    captured.settings_revision = revision;
  });
  workflow_.visit([&](const auto &state) {
    captured.generation = state.spool_generation;
    captured.uid = state.uid;
    if (!state.spool ||
        state.spoolman != services::BackendAvailability::online ||
        (state.stage != services::WorkflowStage::spool_ready &&
         state.stage != services::WorkflowStage::assignment_complete))
      return;
    captured.spool_id = state.spool->id;
    captured.name = state.spool->display_name;
    captured.expected_used = state.spool->used_grams;
    captured.canonical_remaining = state.spool->remaining_grams;
    captured.tare = state.weight_snapshot.empty_spool_grams;
  });
  (void)weigh_sync_.begin(std::move(captured));
}
void BackendWorker::complete_weigh(std::uint64_t id,
                                   std::optional<float> grams) {
  if (grams)
    weigh_sync_.capture(id, {*grams, true});
  else
    weigh_sync_.fail(id,
                     "Measurement failed or timed out. No inventory update");
}
CommandReceipt BackendWorker::submit_weight_update(std::uint64_t id) {
  auto *command = new (std::nothrow) Command;
  if (!command || !id) {
    delete command;
    return {false, 0};
  }
  command->type = CommandType::weight_update;
  command->measurement_id = id;
  command->operation_id = operations_.begin(OperationKind::weight_update,
                                            millis(), "Weight update queued");
  auto operation = command->operation_id;
  if (!operation) {
    delete command;
    return {false, 0};
  }
  if (!enqueue(command)) {
    operations_.fail(operation, millis(),
                     {core::ErrorCategory::backend_unavailable,
                      "Weight update queue unavailable", true});
    return {false, operation};
  }
  return {true, operation};
}
__attribute__((noinline)) bool
BackendWorker::weight_fence(const services::WeighSyncSnapshot &captured) {
  if (captured.settings_revision != configuration_.revision() || !nfc_)
    return false;
  bool same = false;
  workflow_.visit([&](const auto &state) {
    same = state.spool_generation == captured.generation && state.spool &&
           state.spool->id == captured.spool_id && state.uid == captured.uid &&
           state.spoolman == services::BackendAvailability::online;
  });
  if (!same)
    return false;
  // Runs on the existing backend/NFC owner, after canonical GET and before
  // PATCH. Inventory only: no nested decoder, no NFC write, no new task.
  if (!nfc_->reader_.field_on().ok())
    return false;
  auto tags = nfc_->reader_.inventory();
  auto off = nfc_->reader_.field_off();
  return tags.ok() && off.ok() && tags.value().size() == 1 &&
         tags.value()[0] == captured.uid && nfc_->reader_.bus_errors() == 0;
}
__attribute__((noinline)) void
BackendWorker::process_weight_update(std::uint64_t id,
                                     std::uint64_t operation) {
  (void)apply_backend_settings_if_changed();
  if (operation)
    operations_.mark_running(operation, millis(),
                             "Checking and updating Spoolman");
  auto result = weigh_sync_.update(id, spoolman_, [this](const auto &captured) {
    return weight_fence(captured);
  });
  const auto captured = weigh_sync_.snapshot();
  if (captured.measurement_id == id && captured.consumed &&
      captured.settings_revision == configuration_.revision()) {
    auto fresh = spoolman_.get_spool(captured.spool_id);
    if (fresh.ok())
      workflow_.apply_weight_readback(captured.generation, fresh.value(),
                                      captured.gross);
  }
  if (operation) {
    if (result.ok())
      operations_.succeed(operation, millis(), captured.message);
    else
      operations_.fail(operation, millis(), result.error());
  }
}
__attribute__((noinline)) void BackendWorker::auto_weight_update() {
  auto captured = weigh_sync_.snapshot();
  if (captured.phase != "ready" || !captured.automatic || captured.consumed)
    return;
  transport_.begin_operation(millis());
  process_weight_update(captured.measurement_id);
  transport_.end_operation();
}
void BackendWorker::write_weigh_snapshot(JsonObject out) const {
  const auto s = weigh_sync_.snapshot();
  out["measurement_id"] = s.measurement_id;
  out["phase"] = s.phase;
  out["message"] = s.message;
  out["spool_id"] = s.spool_id;
  out["name"] = s.name;
  out["automatic"] = s.automatic;
  out["can_update"] = s.phase == "ready" && !s.consumed;
  configuration_.visit([&](const auto &config, auto) {
    out["policy_auto"] = config.reconciliation.auto_update_after_weigh;
  });
  auto number = [&](const char *name, const auto &value) {
    if (value)
      out[name] = *value;
    else
      out[name] = nullptr;
  };
  number("gross", s.gross);
  number("tare", s.tare);
  number("measured", s.measured);
  number("canonical_remaining", s.canonical_remaining);
  number("difference", s.difference);
}
} // namespace opentag::application

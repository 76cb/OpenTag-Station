#include "services/weigh_sync.hpp"
#include <cmath>
namespace opentag::services {
namespace {
core::Result<void> refused(const std::string &message) {
  return core::Result<void>::failure(
      {core::ErrorCategory::conflict, message, false});
}
bool valid(float value) { return std::isfinite(value) && value >= 0; }
} // namespace
bool WeighSync::begin(WeighSyncSnapshot snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.phase == "updating" || !snapshot.measurement_id ||
      snapshot.measurement_id == state_.measurement_id)
    return false;
  state_ = std::move(snapshot);
  state_.consumed = false;
  state_.phase = "measuring";
  state_.message = "Waiting for a stable weight capture";
  return true;
}
void WeighSync::capture(std::uint64_t id, domain::WeightReading reading) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (id != state_.measurement_id || state_.phase != "measuring")
    return;
  state_.gross = reading.gross_grams;
  // Say exactly why this measurement cannot be saved, in terms the user can
  // act on, rather than one generic sentence.
  const char *reason = nullptr;
  if (!reading.stable || !valid(reading.gross_grams))
    reason = "The weight did not settle. Keep the spool still and weigh again.";
  else if (state_.spool_id <= 0)
    reason = "This spool is not linked to Spoolman yet. Link the tag to a "
             "spool (Manage tag), then weigh again.";
  else if (!state_.tare || !valid(*state_.tare))
    reason = "The empty spool weight is unknown. Set it on the spool in "
             "Spoolman, then weigh again.";
  else if (!state_.canonical_remaining || !valid(*state_.canonical_remaining) ||
           !valid(state_.expected_used))
    reason = "Spoolman has no remaining weight for this spool. Set its initial "
             "weight in Spoolman, then weigh again.";
  else if (reading.gross_grams < *state_.tare)
    reason = "The scale reads less than the empty spool weight. Check that the "
             "spool is on the scale and its empty weight is right.";
  else if (!valid(state_.tolerances.normal_grams))
    reason = "The weight tolerance setting is invalid. Check Settings.";
  if (reason != nullptr) {
    state_.phase = "unavailable";
    state_.message = reason;
    return;
  }
  state_.measured = reading.gross_grams - *state_.tare;
  state_.difference = *state_.measured - *state_.canonical_remaining;
  state_.phase = "ready";
  state_.message = state_.automatic ? "Measured. Saving to Spoolman..."
                                    : "Measured. Tap Update Spoolman to save it.";
}
void WeighSync::fail(std::uint64_t id, const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.measurement_id == id && state_.phase == "measuring") {
    state_.phase = "failed";
    state_.message = message;
  }
}
WeighSyncSnapshot WeighSync::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}
core::Result<void>
WeighSync::update(std::uint64_t id, integrations::ISpoolInventory &inventory,
                  const std::function<bool(const WeighSyncSnapshot &)> &fence) {
  WeighSyncSnapshot captured;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id != state_.measurement_id || state_.phase != "ready" ||
        state_.consumed)
      return refused("Capture a new explicit Weigh before updating");
    state_.consumed = true;
    state_.phase = "updating";
    state_.message = "Checking Spoolman usage and this spool";
    captured = state_;
  }
  auto finish = [&](const char *phase, const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.phase = phase;
    state_.message = message;
  };
  if (!inventory.capabilities().has(
          integrations::BackendCapability::update_remaining_weight)) {
    finish("failed",
           "Saving weights is turned off: this Spoolman version has not been "
           "tested with this firmware, or Spoolman is offline. See Settings.");
    return refused("Spoolman weight updates unavailable");
  }
  if (!fence(captured)) {
    finish("failed", "The spool on the station changed. Weigh again.");
    return refused("Spool or backend changed");
  }
  // The existing normal reconciliation tolerance is the no-write deadband.
  if (std::fabs(*captured.difference) <= captured.tolerances.normal_grams) {
    finish("unchanged", "Spoolman already matches this weight. Nothing to "
                        "update.");
    return core::Result<void>::success();
  }
  integrations::RemainingWeightUpdate update;
  update.expected_used_grams = captured.expected_used;
  update.remaining_grams = *captured.measured;
  update.before_mutation = [&] { return fence(captured); };
  auto result = inventory.set_remaining_weight(captured.spool_id, update);
  if (!result.ok()) {
    auto fresh = inventory.get_spool(captured.spool_id);
    if (fresh.ok() && fresh.value().id == captured.spool_id) {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.canonical_remaining = fresh.value().remaining_grams;
      if (state_.canonical_remaining)
        state_.difference = *state_.measured - *state_.canonical_remaining;
    }
    const bool conflict =
        result.error().category == core::ErrorCategory::conflict;
    finish(conflict ? "conflict" : "failed",
           conflict
               ? "Spoolman usage changed while this weight was being "
                 "processed. No inventory value was overwritten. Weigh again"
               : result.error().message);
    return core::Result<void>::failure(result.error());
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.canonical_remaining = result.value().remaining_grams;
    if (state_.canonical_remaining)
      state_.difference = *state_.measured - *state_.canonical_remaining;
  }
  finish("updated", "Saved to Spoolman.");
  return core::Result<void>::success();
}
} // namespace opentag::services

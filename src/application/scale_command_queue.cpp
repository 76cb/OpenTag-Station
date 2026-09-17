#include "application/scale_command_queue.hpp"

#include <cmath>

namespace opentag::application {

bool ScaleCommandQueue::initialize() {
  if (queue_ != nullptr) return true;
  queue_ = xQueueCreate(queue_depth, sizeof(Command));
  return queue_ != nullptr;
}

CommandReceipt ScaleCommandQueue::submit(Command command, OperationKind kind) {
  command.operation_id = operations_.begin(
      kind,
      command.enqueued_at_ms,
      kind == OperationKind::scale_weigh
          ? "Scale measurement queued"
          : kind == OperationKind::scale_tare ? "Scale tare queued"
                                               : "Scale calibration queued");
  if (command.operation_id == 0U) return {false, 0U};
  pending_.fetch_add(1U, std::memory_order_relaxed);
  if (queue_ == nullptr || xQueueSend(queue_, &command, 0U) != pdTRUE) {
    pending_.fetch_sub(1U, std::memory_order_relaxed);
    operations_.fail(
        command.operation_id,
        command.enqueued_at_ms,
        {core::ErrorCategory::scale_unavailable,
         "Scale command queue is unavailable or full",
         true});
    return {false, command.operation_id};
  }
  return {true, command.operation_id};
}

CommandReceipt ScaleCommandQueue::submit_weigh(std::uint32_t now_ms, bool explicit_request) {
  Command command;
  command.type = CommandType::weigh;
  command.explicit_request=explicit_request;
  command.enqueued_at_ms = now_ms;
  return submit(command, OperationKind::scale_weigh);
}

CommandReceipt ScaleCommandQueue::submit_tare(std::uint32_t now_ms) {
  Command command;
  command.type = CommandType::tare;
  command.enqueued_at_ms = now_ms;
  return submit(command, OperationKind::scale_tare);
}

CommandReceipt ScaleCommandQueue::submit_calibration(
    float reference_grams,
    std::uint32_t now_ms) {
  Command command;
  command.type = CommandType::calibrate;
  command.reference_grams = reference_grams;
  command.enqueued_at_ms = now_ms;
  const auto configured_capacity =
      configuration_.scale_profile_snapshot().hardware.rated_capacity_grams;
  if (!std::isfinite(reference_grams) || reference_grams <= 0.0F ||
      reference_grams > configured_capacity) {
    const auto operation_id = operations_.begin(
        OperationKind::scale_calibration,
        now_ms,
        "Scale calibration rejected");
    if (operation_id == 0U) return {false, 0U};
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Calibration reference weight exceeds the configured capacity",
         false});
    return {false, operation_id};
  }
  return submit(command, OperationKind::scale_calibration);
}

void ScaleCommandQueue::process_one(std::uint32_t now_ms) {
  if (active_.has_value()) {
    const auto& status = scale_.status();
    if (status.measurement_state == services::ScaleMeasurementState::timed_out ||
        status.measurement_state == services::ScaleMeasurementState::failed) {
      operations_.fail(
          active_->operation_id,
          now_ms,
          status.measurement_error.value_or(core::Error{
              core::ErrorCategory::scale_unstable,
              "Scale measurement did not complete",
              true}));
      if (active_->type == CommandType::weigh && active_->explicit_request &&
          weigh_finished)
        weigh_finished(active_->operation_id, std::nullopt);
      active_.reset();
      return;
    }
    if (active_->type == CommandType::weigh &&
        status.measurement_state == services::ScaleMeasurementState::completed) {
      operations_.succeed(
          active_->operation_id, now_ms, "Stable weight captured");
      if(active_->explicit_request && weigh_finished) weigh_finished(active_->operation_id,status.last_completed_grams);
      active_.reset();
      return;
    }
    if (active_->type == CommandType::calibrate &&
        status.sample.raw_stable &&
        status.measurement_state == services::ScaleMeasurementState::settling &&
        !active_->reference_detected_reported) {
      operations_.mark_running(
          active_->operation_id,
          now_ms,
          "Reference detected; settling reference plateau");
      active_->reference_detected_reported = true;
    }
    if (status.measurement_state != services::ScaleMeasurementState::ready) {
      return;
    }
    if (active_->type == CommandType::tare) {
      const auto result = scale_.tare();
      if (result.ok()) {
        operations_.succeed(
            active_->operation_id, now_ms, "Scale tare completed");
      } else {
        operations_.fail(active_->operation_id, now_ms, result.error());
      }
      active_.reset();
      return;
    }
    const auto configured = configuration_.scale_profile_snapshot();
    const auto result = scale_.calibrate(
        active_->reference_grams, configured.hardware.rated_capacity_grams);
    if (result.ok()) {
      operations_.succeed(
          active_->operation_id, now_ms, "Scale calibration persisted");
    } else {
      operations_.fail(active_->operation_id, now_ms, result.error());
    }
    active_.reset();
    return;
  }

  if (queue_ == nullptr) return;
  Command command;
  if (xQueueReceive(queue_, &command, 0U) != pdTRUE) return;
  pending_.fetch_sub(1U, std::memory_order_relaxed);
  if (static_cast<std::uint32_t>(now_ms - command.enqueued_at_ms) >
      command_expiry_ms) {
    operations_.fail(
        command.operation_id,
        now_ms,
        {core::ErrorCategory::scale_unavailable,
         "Scale command expired before its measurement session started",
         true});
    return;
  }
  operations_.mark_running(
      command.operation_id, now_ms, "Waiting for the scale to settle");
  const auto purpose = command.type == CommandType::weigh
      ? services::ScaleMeasurementPurpose::weigh
      : command.type == CommandType::tare
          ? services::ScaleMeasurementPurpose::tare
          : services::ScaleMeasurementPurpose::calibration;
  const auto started = scale_.begin_measurement(purpose, now_ms);
  if (!started.ok()) {
    operations_.fail(command.operation_id, now_ms, started.error());
    return;
  }
  active_ = command;
  if(command.type==CommandType::weigh && command.explicit_request && weigh_started) weigh_started(command.operation_id);
}

}  // namespace opentag::application

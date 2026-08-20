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
      kind == OperationKind::scale_tare ? "Scale tare queued"
                                        : "Scale calibration queued");
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
      configuration_.snapshot().scale_hardware.rated_capacity_grams;
  if (!std::isfinite(reference_grams) || reference_grams <= 0.0F ||
      reference_grams > configured_capacity) {
    const auto operation_id = operations_.begin(
        OperationKind::scale_calibration,
        now_ms,
        "Scale calibration rejected");
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
         "Scale command expired; retry with a stable scale",
         true});
    return;
  }
  operations_.mark_running(command.operation_id, now_ms);
  if (command.type == CommandType::tare) {
    const auto result = scale_.tare();
    if (result.ok()) {
      operations_.succeed(command.operation_id, now_ms, "Scale tare completed");
    } else {
      operations_.fail(command.operation_id, now_ms, result.error());
    }
    return;
  }

  const auto configured = configuration_.snapshot();
  const auto result = scale_.calibrate(
      command.reference_grams,
      configured.scale_hardware.rated_capacity_grams);
  if (result.ok()) {
    operations_.succeed(
        command.operation_id, now_ms, "Scale calibration persisted");
  } else {
    operations_.fail(command.operation_id, now_ms, result.error());
  }
}

}  // namespace opentag::application

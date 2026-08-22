#include "application/ota_worker.hpp"

#include <Arduino.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "ota/upload_operation_policy.hpp"

namespace opentag::application {
namespace {

core::Error worker_error(const char* message, bool retryable = true) {
  return {core::ErrorCategory::firmware_update, message, retryable};
}

core::Error worker_conflict(const char* message) {
  return {core::ErrorCategory::conflict, message, false};
}

bool candidate_state(opentag::ota::UpdateState state) {
  return state == opentag::ota::UpdateState::candidate_boot ||
      state == opentag::ota::UpdateState::validating_candidate;
}

bool candidate_lease_state(opentag::ota::UpdateState state) {
  return candidate_state(state) ||
      state == opentag::ota::UpdateState::rollback_pending;
}

bool update_lease_state(opentag::ota::UpdateState state) {
  return state == opentag::ota::UpdateState::upload_receiving ||
      state == opentag::ota::UpdateState::writing ||
      state == opentag::ota::UpdateState::validating ||
      state == opentag::ota::UpdateState::ready_to_activate ||
      state == opentag::ota::UpdateState::ready_to_reboot ||
      state == opentag::ota::UpdateState::reboot_pending;
}

bool pending_bootloader_confirmation(
    opentag::ota::PartitionImageState state) {
  return state == opentag::ota::PartitionImageState::pending_verify ||
      state == opentag::ota::PartitionImageState::new_image;
}

bool rollback_seed_recovery_state(
    const opentag::ota::UpdateSnapshot& state) {
  const bool recoverable_running =
      pending_bootloader_confirmation(state.running_image_state) ||
      state.running_image_state ==
          opentag::ota::PartitionImageState::valid;
  return state.state == opentag::ota::UpdateState::ready_to_reboot &&
      state.validation_passed && state.calculated_sha_available &&
      state.expected_sha256 == state.calculated_sha256 &&
      state.image_size != 0U && state.bytes_received == state.image_size &&
      state.activation_intent && !state.activated && state.target.present() &&
      opentag::ota::same_partition(state.boot, state.running) &&
      opentag::ota::same_partition(state.inactive, state.target) &&
      !opentag::ota::same_partition(state.running, state.target) &&
      recoverable_running;
}

bool same_precondition(
    opentag::ota::OperationPrecondition left,
    opentag::ota::OperationPrecondition right) {
  return left.operation_id == right.operation_id &&
      left.generation == right.generation;
}

bool snapshot_owned_by(
    const opentag::ota::UpdateSnapshot& state,
    opentag::ota::OperationPrecondition precondition) {
  return precondition.operation_id != 0U && precondition.generation != 0U &&
      state.operation_id == precondition.operation_id &&
      state.generation == precondition.generation;
}

bool reboot_state_allowed(
    const opentag::ota::UpdateSnapshot& state,
    bool already_queued) {
  return state.validation_passed && state.target.present() &&
      (state.state == opentag::ota::UpdateState::ready_to_reboot ||
       (already_queued &&
        state.state == opentag::ota::UpdateState::reboot_pending));
}

bool cancel_state_allowed(const opentag::ota::UpdateSnapshot& state) {
  return state.state == opentag::ota::UpdateState::upload_receiving ||
      state.state == opentag::ota::UpdateState::writing ||
      (state.state == opentag::ota::UpdateState::ready_to_reboot &&
       !state.activated && !state.activation_intent);
}

bool terminal_operation_state(OperationState state) {
  return state == OperationState::succeeded ||
      state == OperationState::failed ||
      state == OperationState::confirmation_required;
}

}  // namespace

opentag::ota::UpdateSnapshot OtaWorker::snapshot() const {
  const std::lock_guard<std::mutex> lock(published_snapshot_mutex_);
  return published_snapshot_;
}

void OtaWorker::publish_owner_snapshot() {
  // Keep this fixed-size copy in the publisher's frame so callers that own
  // command/result buffers do not retain a second UpdateSnapshot across flash
  // or record-store calls.
  const auto current = manager_.snapshot();
  const std::lock_guard<std::mutex> lock(published_snapshot_mutex_);
  published_snapshot_ = current;
}

void OtaWorker::reconcile_published_lifecycle() {
  // This bounded copy has a separate lifetime from command processing and all
  // platform flash descriptors.
  const auto current = snapshot();
  reconcile_lifecycle(current);
}

bool OtaWorker::reconcile_published_candidate_confirmation() {
  const auto current = snapshot();
  reconcile_lifecycle(current);
  return current.state == opentag::ota::UpdateState::confirmed ||
      (current.running_image_state ==
           opentag::ota::PartitionImageState::valid &&
       opentag::ota::same_partition(current.running, current.target));
}

bool OtaWorker::reconcile_published_rollback(bool transition_ok) {
  const auto current = snapshot();
  reconcile_lifecycle(current);
  return transition_ok ||
      current.state == opentag::ota::UpdateState::rolled_back ||
      current.state == opentag::ota::UpdateState::confirmed ||
      (current.running_image_state ==
           opentag::ota::PartitionImageState::valid &&
       (!current.target.present() ||
        opentag::ota::same_partition(current.running, current.target)));
}

bool OtaWorker::start(std::uint32_t now_ms) {
  if (task_ != nullptr) return ready();
  initialized_at_ms_ = now_ms;
  queue_ = xQueueCreate(queue_depth, sizeof(std::uint8_t));
  startup_complete_ = xSemaphoreCreateBinary();
  if (queue_ == nullptr || startup_complete_ == nullptr) {
    cleanup_pre_task_resources();
    return false;
  }
  for (auto& slot : slots_) {
    slot.completion = xSemaphoreCreateBinary();
    if (slot.completion == nullptr) {
      cleanup_pre_task_resources();
      return false;
    }
  }
  if (xTaskCreatePinnedToCore(
          task_entry,
          "opentag-ota",
          task_stack_bytes,
          this,
          1U,
          &task_,
          0) != pdPASS) {
    task_ = nullptr;
    cleanup_pre_task_resources();
    return false;
  }
  if (xSemaphoreTake(startup_complete_, pdMS_TO_TICKS(10000U)) != pdTRUE) {
    return false;
  }
  return startup_ok_.load(std::memory_order_acquire);
}

void OtaWorker::cleanup_pre_task_resources() {
  // This helper is used only before a task has been created, so no owner can
  // race these handles. Once the task exists it owns them for its lifetime.
  if (task_ != nullptr) return;
  for (auto& slot : slots_) {
    if (slot.completion != nullptr) {
      vSemaphoreDelete(slot.completion);
      slot.completion = nullptr;
    }
    slot = {};
  }
  if (startup_complete_ != nullptr) {
    vSemaphoreDelete(startup_complete_);
    startup_complete_ = nullptr;
  }
  if (queue_ != nullptr) {
    vQueueDelete(queue_);
    queue_ = nullptr;
  }
}

std::optional<std::uint8_t> OtaWorker::reserve_slot(bool abandoned) {
  const std::lock_guard<std::mutex> lock(slots_mutex_);
  for (std::size_t index = 0U; index < slots_.size(); ++index) {
    auto& slot = slots_[index];
    if (slot.in_use || slot.completion == nullptr) continue;
    while (xSemaphoreTake(slot.completion, 0U) == pdTRUE) {}
    slot.kind = CommandKind::begin;
    slot.begin_request = {};
    slot.precondition = {};
    slot.chunk_size = 0U;
    slot.health = opentag::ota::CandidateHealthDecision::stabilizing;
    slot.now_ms = 0U;
    slot.health_revision = 0U;
    slot.health_attempt = 0U;
    slot.control_operation_id = 0U;
    slot.cleanup_reason.reset();
    slot.result.reset();
    slot.in_use = true;
    slot.abandoned = abandoned;
    return static_cast<std::uint8_t>(index);
  }
  return std::nullopt;
}

void OtaWorker::release_slot(std::uint8_t index) {
  if (index >= slots_.size()) return;
  const std::lock_guard<std::mutex> lock(slots_mutex_);
  slots_[index].result.reset();
  slots_[index].in_use = false;
  slots_[index].abandoned = false;
}

bool OtaWorker::enqueue_slot(std::uint8_t index) {
  if (queue_ == nullptr || index >= slots_.size()) return false;
  pending_.fetch_add(1U, std::memory_order_relaxed);
  if (xQueueSend(queue_, &index, 0U) == pdTRUE) return true;
  pending_.fetch_sub(1U, std::memory_order_relaxed);
  return false;
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::wait_for_slot(
    std::uint8_t index) {
  if (index >= slots_.size()) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_error("OTA command slot is invalid"));
  }
  auto& slot = slots_[index];
  if (xSemaphoreTake(
          slot.completion, pdMS_TO_TICKS(command_timeout_ms)) == pdTRUE) {
    const std::lock_guard<std::mutex> lock(slots_mutex_);
    if (!slot.result.has_value()) {
      slot.in_use = false;
      return core::Result<opentag::ota::UpdateSnapshot>::failure(
          worker_error("OTA owner returned no command result"));
    }
    auto result = *slot.result;
    slot.result.reset();
    slot.in_use = false;
    slot.abandoned = false;
    return result;
  }

  const std::lock_guard<std::mutex> lock(slots_mutex_);
  if (slot.result.has_value()) {
    auto result = *slot.result;
    slot.result.reset();
    slot.in_use = false;
    slot.abandoned = false;
    (void)xSemaphoreTake(slot.completion, 0U);
    return result;
  }
  // The fixed slot remains reserved until the owner eventually completes it;
  // it is never reused while the worker may still hold references to it.
  slot.abandoned = true;
  if (slot.kind == CommandKind::finish_and_activate) {
    pending_finish_cleanup_ = slot.precondition;
  }
  return core::Result<opentag::ota::UpdateSnapshot>::failure(
      worker_error("OTA owner command timed out"));
}

void OtaWorker::complete_slot(
    std::uint8_t index,
    core::Result<opentag::ota::UpdateSnapshot> result) {
  std::unique_lock<std::mutex> lock(slots_mutex_);
  if (index >= slots_.size() || !slots_[index].in_use) return;
  auto& slot = slots_[index];
  slot.result = std::move(result);
  if (slot.abandoned) {
    const auto kind = slot.kind;
    const auto precondition = slot.precondition;
    const auto begin_operation_id = slot.begin_request.operation_id;
    auto abandoned_result = std::move(*slot.result);
    slot.result.reset();
    lock.unlock();
    if (kind == CommandKind::begin) {
      if (!abandoned_result.ok()) {
        operations_.fail(
            begin_operation_id, millis(), abandoned_result.error());
      } else {
        const auto& opened = abandoned_result.value();
        const opentag::ota::OperationPrecondition opened_precondition{
            opened.operation_id,
            opened.generation,
        };
        const auto reason = worker_error(
            "OTA begin timed out; the late upload was canceled", true);
        const auto canceled = manager_.cancel(opened_precondition, millis());
        publish_owner_snapshot();
        reconcile_upload_cleanup(
            opened_precondition, millis(), reason, canceled, true);
      }
    } else if (kind == CommandKind::finish_and_activate) {
      reconcile_late_finish(precondition, millis(), abandoned_result);
    }
    reconcile_published_lifecycle();
    lock.lock();
    slot.in_use = false;
    slot.abandoned = false;
    return;
  }
  // Publish the wakeup before the slot can become reusable. A timeout racing
  // this path either drains this semaphore while consuming result, or marks
  // the still-reserved slot abandoned before we acquire the mutex.
  (void)xSemaphoreGive(slot.completion);
}

void OtaWorker::reconcile_upload_cleanup(
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms,
    const core::Error& terminal_reason,
    const core::Result<opentag::ota::UpdateSnapshot>& cleanup,
    bool authoritative_cleanup_attempt) {
  const auto snapshot = cleanup.ok() ? cleanup.value() : this->snapshot();
  const auto disposition = opentag::ota::classify_upload_cleanup(
      precondition, snapshot, cleanup.ok());
  bool finish_cleanup_pending = false;
  {
    const std::lock_guard<std::mutex> lock(slots_mutex_);
    finish_cleanup_pending = pending_finish_cleanup_.has_value() &&
        same_precondition(*pending_finish_cleanup_, precondition);
  }
  const auto resolution = opentag::ota::resolve_upload_operation(
      disposition,
      finish_cleanup_pending,
      authoritative_cleanup_attempt);

  if (resolution != opentag::ota::UploadOperationResolution::running &&
      finish_cleanup_pending) {
    const std::lock_guard<std::mutex> lock(slots_mutex_);
    if (pending_finish_cleanup_.has_value() &&
        same_precondition(*pending_finish_cleanup_, precondition)) {
      pending_finish_cleanup_.reset();
    }
  }

  const auto operation = operations_.get(precondition.operation_id);
  if (!operation.has_value()) return;
  if (resolution == opentag::ota::UploadOperationResolution::failed) {
    auto error = terminal_reason;
    if (disposition == opentag::ota::UploadCleanupDisposition::failed &&
        !snapshot.last_error.empty()) {
      error.message = std::string(snapshot.last_error.view());
    }
    operations_.fail(precondition.operation_id, now_ms, std::move(error));
    return;
  }
  if (resolution == opentag::ota::UploadOperationResolution::succeeded) {
    operations_.succeed(
        precondition.operation_id,
        now_ms,
        "Firmware validated in the inactive slot; reboot confirmation required");
    return;
  }
  if (operation->state == OperationState::queued ||
      operation->state == OperationState::running) {
    operations_.mark_running(
        precondition.operation_id,
        now_ms,
        "Firmware cleanup is unresolved; inspect update state before retrying");
  }
}

void OtaWorker::reconcile_late_finish(
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms,
    const core::Result<opentag::ota::UpdateSnapshot>& result) {
  const auto snapshot = result.ok() ? result.value() : this->snapshot();
  const auto disposition = opentag::ota::classify_upload_cleanup(
      precondition, snapshot, false);
  const auto resolution = opentag::ota::resolve_upload_operation(
      disposition, true, false);
  if (resolution == opentag::ota::UploadOperationResolution::failed) {
    const auto error = result.ok()
        ? worker_error("Firmware validation failed", false)
        : result.error();
    {
      const std::lock_guard<std::mutex> lock(slots_mutex_);
      if (pending_finish_cleanup_.has_value() &&
          same_precondition(*pending_finish_cleanup_, precondition)) {
        pending_finish_cleanup_.reset();
      }
    }
    operations_.fail(precondition.operation_id, now_ms, error);
    return;
  }

  const auto operation = operations_.get(precondition.operation_id);
  if (operation.has_value() && terminal_operation_state(operation->state)) {
    const std::lock_guard<std::mutex> lock(slots_mutex_);
    if (pending_finish_cleanup_.has_value() &&
        same_precondition(*pending_finish_cleanup_, precondition)) {
      pending_finish_cleanup_.reset();
    }
    return;
  }
  if (operation.has_value()) {
    operations_.mark_running(
        precondition.operation_id,
        now_ms,
        "Firmware finish exceeded its deadline; canceling the late candidate");
  }
  const auto reason = worker_error(
      "Firmware finish exceeded its deadline; the late candidate was canceled",
      true);
  const auto cleanup = manager_.cancel(precondition, now_ms);
  publish_owner_snapshot();
  reconcile_upload_cleanup(
      precondition, now_ms, reason, cleanup, true);
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::execute_sync(
    std::uint8_t index) {
  if (!enqueue_slot(index)) {
    release_slot(index);
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_error("OTA command queue is unavailable or full"));
  }
  return wait_for_slot(index);
}

core::Result<void> OtaWorker::acquire_lifecycle(DeviceLifecycleOwner owner) {
  const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (lifecycle_lease_) {
    return core::Result<void>::failure(
        {core::ErrorCategory::conflict,
         "Another OTA lifecycle is already active",
         false});
  }
  const auto lease = lifecycle_.try_acquire(owner);
  if (!lease) {
    const auto active = lifecycle_.snapshot();
    return core::Result<void>::failure(
        {core::ErrorCategory::conflict,
         std::string("Device lifecycle is owned by ") +
             to_string(active.owner),
         false});
  }
  lifecycle_lease_ = lease;
  return core::Result<void>::success();
}

void OtaWorker::release_lifecycle() {
  DeviceLifecycleLease lease;
  {
    const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    lease = lifecycle_lease_;
    lifecycle_lease_ = {};
  }
  if (lease) (void)lifecycle_.release(lease);
}

bool OtaWorker::lifecycle_owned(DeviceLifecycleOwner owner) const {
  const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  return lifecycle_lease_.owner == owner &&
      lifecycle_.owns(lifecycle_lease_);
}

void OtaWorker::reconcile_lifecycle(
    const opentag::ota::UpdateSnapshot& state) {
  const bool physically_confirmed =
      state.running_image_state == opentag::ota::PartitionImageState::valid &&
      (!state.target.present() ||
       opentag::ota::same_partition(state.running, state.target));
  if (physically_confirmed ||
      state.state == opentag::ota::UpdateState::idle ||
      state.state == opentag::ota::UpdateState::confirmed ||
      state.state == opentag::ota::UpdateState::rolled_back) {
    release_lifecycle();
    return;
  }

  // A failed pre-activation operation is terminal: the manager has closed or
  // aborted its writer before exposing this state. Activation ambiguity and a
  // pending bootloader candidate remain fail-closed under the existing lease.
  if (state.state == opentag::ota::UpdateState::failed &&
      !state.activation_intent && !state.activated &&
      !pending_bootloader_confirmation(state.running_image_state)) {
    release_lifecycle();
  }
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::begin_upload(
    const opentag::ota::BeginUploadRequest& request,
    std::uint32_t now_ms) {
  const auto reject = [&](core::Error error) {
    operations_.fail(request.operation_id, now_ms, error);
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        std::move(error));
  };
  if (!ready()) return reject(worker_error("OTA owner is unavailable"));
  if (storage_.factory_reset_recovery_pending()) {
    return reject(worker_conflict(
        "Factory reset recovery blocks firmware upload"));
  }
  const auto lease = acquire_lifecycle(DeviceLifecycleOwner::ota_update);
  if (!lease.ok()) return reject(lease.error());
  const auto index = reserve_slot(false);
  if (!index.has_value()) {
    release_lifecycle();
    return reject(worker_error("OTA command pool is exhausted"));
  }
  auto& slot = slots_[*index];
  slot.kind = CommandKind::begin;
  slot.begin_request = request;
  slot.now_ms = now_ms;
  // expected_generation == 0 is valid on a fresh repository. The owner-side
  // manager atomically reserves the first positive durable generation.
  if (!enqueue_slot(*index)) {
    release_slot(*index);
    release_lifecycle();
    return reject(worker_error("OTA command queue is unavailable or full"));
  }
  return wait_for_slot(*index);
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::write_chunk(
    opentag::ota::OperationPrecondition precondition,
    core::ByteView chunk,
    std::uint32_t now_ms) {
  if (!ready() || !lifecycle_owned(DeviceLifecycleOwner::ota_update) ||
      chunk.data == nullptr || chunk.empty() ||
      chunk.size > opentag::ota::maximum_upload_chunk_bytes) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_error("OTA stream state or chunk is invalid", false));
  }
  const auto index = reserve_slot(false);
  if (!index.has_value()) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_error("OTA command pool is exhausted"));
  }
  auto& slot = slots_[*index];
  slot.kind = CommandKind::write;
  slot.precondition = precondition;
  slot.chunk_size = chunk.size;
  std::memcpy(slot.chunk.data(), chunk.data, chunk.size);
  slot.now_ms = now_ms;
  return execute_sync(*index);
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::finish_and_activate(
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms) {
  if (!ready() || !lifecycle_owned(DeviceLifecycleOwner::ota_update)) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_error("OTA stream is not active", false));
  }
  const auto index = reserve_slot(false);
  if (!index.has_value()) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_error("OTA command pool is exhausted"));
  }
  auto& slot = slots_[*index];
  slot.kind = CommandKind::finish_and_activate;
  slot.precondition = precondition;
  slot.now_ms = now_ms;
  return execute_sync(*index);
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::abort_upload(
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms,
    core::Error terminal_reason) {
  const auto reject = [&](core::Error error) {
    auto result = core::Result<opentag::ota::UpdateSnapshot>::failure(
        std::move(error));
    reconcile_upload_cleanup(
        precondition, now_ms, terminal_reason, result, false);
    return result;
  };
  const auto operation = operations_.get(precondition.operation_id);
  if (operation.has_value() && terminal_operation_state(operation->state)) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(
        worker_conflict(
            "Firmware upload operation already reached a terminal result"));
  }
  if (operation.has_value() &&
      (operation->state == OperationState::queued ||
       operation->state == OperationState::running)) {
    operations_.mark_running(
        precondition.operation_id,
        now_ms,
        "Stopping incomplete firmware upload");
  }
  if (!ready() || !lifecycle_owned(DeviceLifecycleOwner::ota_update)) {
    return reject(worker_error("OTA stream is not active", false));
  }
  const auto index = reserve_slot(false);
  if (!index.has_value()) {
    return reject(worker_error("OTA command pool is exhausted"));
  }
  auto& slot = slots_[*index];
  slot.kind = CommandKind::abort;
  slot.precondition = precondition;
  slot.now_ms = now_ms;
  slot.cleanup_reason = terminal_reason;
  auto result = execute_sync(*index);
  reconcile_upload_cleanup(
      precondition, millis(), terminal_reason, result, false);
  return result;
}

CommandReceipt OtaWorker::submit_reboot(
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms) {
  return submit_control(
      CommandKind::reboot,
      OperationKind::firmware_reboot,
      "OTA candidate reboot queued",
      precondition,
      now_ms);
}

CommandReceipt OtaWorker::submit_cancel(
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms) {
  return submit_control(
      CommandKind::cancel,
      OperationKind::firmware_cancel,
      "OTA cancellation queued",
      precondition,
      now_ms);
}

CommandReceipt OtaWorker::submit_control(
    CommandKind kind,
    OperationKind operation_kind,
    const char* queued_message,
    opentag::ota::OperationPrecondition precondition,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(control_mutex_);
  const auto reject = [&](core::Error error) {
    const auto id = operations_.begin(operation_kind, now_ms, queued_message);
    if (id == 0U) return CommandReceipt{false, 0U};
    operations_.fail(id, now_ms, std::move(error));
    return CommandReceipt{false, id};
  };

  if (!ready()) {
    return reject(worker_error("OTA owner is unavailable"));
  }
  if (!lifecycle_owned(DeviceLifecycleOwner::ota_update)) {
    return reject(worker_conflict(
        "OTA update does not own the device lifecycle"));
  }
  const auto current = snapshot();
  const bool matching_control = control_active_ && control_kind_ == kind &&
      same_precondition(control_precondition_, precondition);
  if (!snapshot_owned_by(current, precondition)) {
    return reject(worker_conflict("OTA control precondition is stale"));
  }
  const bool state_allowed = kind == CommandKind::reboot
      ? reboot_state_allowed(current, matching_control)
      : cancel_state_allowed(current);
  if (!state_allowed) {
    return reject(worker_conflict(
        "OTA control is not allowed in the current update state"));
  }
  if (control_active_) {
    if (matching_control) return {true, control_operation_id_};
    return reject(worker_conflict(
        "Another OTA control command is already queued"));
  }

  const auto operation_id = operations_.begin(
      operation_kind, now_ms, queued_message);
  if (operation_id == 0U) return {false, 0U};
  const auto index = reserve_slot(true);
  if (!index.has_value()) {
    operations_.fail(
        operation_id, now_ms, worker_error("OTA command pool is exhausted"));
    return {false, operation_id};
  }
  auto& slot = slots_[*index];
  slot.kind = kind;
  slot.precondition = precondition;
  slot.now_ms = now_ms;
  slot.control_operation_id = operation_id;
  control_active_ = true;
  control_kind_ = kind;
  control_precondition_ = precondition;
  control_operation_id_ = operation_id;
  if (!enqueue_slot(*index)) {
    control_active_ = false;
    control_precondition_ = {};
    control_operation_id_ = 0U;
    release_slot(*index);
    operations_.fail(
        operation_id, now_ms, worker_error("OTA command queue is unavailable"));
    return {false, operation_id};
  }
  return {true, operation_id};
}

void OtaWorker::clear_control(std::uint64_t operation_id) {
  const std::lock_guard<std::mutex> lock(control_mutex_);
  if (!control_active_ || control_operation_id_ != operation_id) return;
  control_active_ = false;
  control_precondition_ = {};
  control_operation_id_ = 0U;
}

bool OtaWorker::queue_boot_health_locked() {
  const auto index = reserve_slot(true);
  if (!index.has_value()) return false;
  auto& slot = slots_[*index];
  slot.kind = CommandKind::boot_health;
  slot.health = boot_health_decision_;
  slot.now_ms = boot_health_now_ms_;
  slot.health_revision = boot_health_revision_;
  slot.health_attempt = boot_health_attempt_;
  if (!enqueue_slot(*index)) {
    release_slot(*index);
    return false;
  }
  boot_health_queued_ = true;
  return true;
}

bool OtaWorker::submit_boot_health(
    opentag::ota::CandidateHealthDecision decision,
    std::uint32_t now_ms) {
  if (!ready()) return false;
  const std::lock_guard<std::mutex> lock(boot_health_mutex_);
  const bool decision_changed = decision != boot_health_decision_;
  boot_health_decision_ = decision;
  boot_health_now_ms_ = now_ms;
  if (boot_health_queued_) {
    if (decision_changed) {
      ++boot_health_revision_;
      if (boot_health_revision_ == 0U) ++boot_health_revision_;
      boot_health_attempt_ = 0U;
    }
    return true;
  }
  ++boot_health_revision_;
  if (boot_health_revision_ == 0U) ++boot_health_revision_;
  boot_health_attempt_ = 0U;
  return queue_boot_health_locked();
}

void OtaWorker::task_entry(void* context) {
  auto* worker = static_cast<OtaWorker*>(context);
  worker->run(worker->initialized_at_ms_);
}

void OtaWorker::run(std::uint32_t initialized_at_ms) {
  const auto initialized = manager_.initialize_from_boot(initialized_at_ms);
  publish_owner_snapshot();
  auto initialized_state = snapshot();
  if (initialized_state.operation_id != 0U) {
    operations_.reserve_ids_above(initialized_state.operation_id);
  }
  bool startup_ok = false;
  if (initialized.ok()) {
    const auto state = initialized.value().state;
    if (candidate_lease_state(state)) {
      startup_ok = acquire_lifecycle(
          DeviceLifecycleOwner::candidate_validation).ok();
    } else if (update_lease_state(state)) {
      startup_ok = acquire_lifecycle(DeviceLifecycleOwner::ota_update).ok();
    } else {
      startup_ok = true;
    }
  } else {
    const bool topology_available = initialized_state.running.present() &&
        initialized_state.inactive.present();
    if (rollback_seed_recovery_state(initialized_state)) {
      rollback_seed_recovery_required_ = true;
      const bool update_excluded = acquire_lifecycle(
          DeviceLifecycleOwner::ota_update).ok();
      startup_ok = topology_available && update_excluded;
      if (startup_ok) {
        const auto recovered =
            manager_.recover_rollback_seed(initialized_at_ms);
        rollback_seed_recovery_required_ = !recovered.ok();
        publish_owner_snapshot();
        initialized_state = snapshot();
      }
    } else if (pending_bootloader_confirmation(
                   initialized_state.running_image_state)) {
      // Metadata failure for an unconfirmed image must retain candidate
      // exclusion and retry the real bootloader rollback path.
      candidate_rollback_required_ = true;
      const bool candidate_excluded = acquire_lifecycle(
          DeviceLifecycleOwner::candidate_validation).ok();
      startup_ok = topology_available && candidate_excluded;
      if (startup_ok && candidate_state(initialized_state.state)) {
        (void)manager_.rollback_candidate(initialized_at_ms);
        publish_owner_snapshot();
        initialized_state = snapshot();
        reconcile_lifecycle(initialized_state);
      }
    } else {
      // A corrupt/interrupted audit record must not permanently disable the
      // recoverable manager. Valid known-good topology can accept a fresh
      // upload, while absent platform topology remains unavailable.
      startup_ok = topology_available;
    }
  }
  startup_ok_.store(startup_ok, std::memory_order_release);
  ready_.store(startup_ok, std::memory_order_release);
  (void)xSemaphoreGive(startup_complete_);

  for (;;) {
    std::uint8_t index = 0U;
    if (xQueueReceive(queue_, &index, portMAX_DELAY) != pdTRUE) continue;
    pending_.fetch_sub(1U, std::memory_order_relaxed);
    if (index >= slots_.size()) continue;
    auto& command = slots_[index];
    if (command.kind == CommandKind::boot_health) {
      opentag::ota::CandidateHealthDecision decision;
      std::uint32_t now_ms = 0U;
      std::uint64_t revision = 0U;
      std::uint8_t attempt = 0U;
      {
        const std::lock_guard<std::mutex> lock(boot_health_mutex_);
        decision = boot_health_decision_;
        now_ms = boot_health_now_ms_;
        revision = boot_health_revision_;
        attempt = boot_health_attempt_;
      }
      const bool retry = process_boot_health(decision, now_ms, attempt);
      if (retry) {
        vTaskDelay(pdMS_TO_TICKS(boot_health_retry_delay_ms));
      }
      complete_slot(
          index,
          core::Result<opentag::ota::UpdateSnapshot>::success(snapshot()));
      {
        const std::lock_guard<std::mutex> lock(boot_health_mutex_);
        const bool changed = boot_health_revision_ != revision;
        boot_health_queued_ = false;
        if (changed) {
          boot_health_attempt_ = 0U;
          (void)queue_boot_health_locked();
        } else if (retry) {
          boot_health_attempt_ = static_cast<std::uint8_t>(attempt + 1U);
          boot_health_now_ms_ = millis();
          (void)queue_boot_health_locked();
        } else {
          boot_health_attempt_ = 0U;
        }
      }
      continue;
    }
    complete_slot(index, process(command));
  }
}

core::Result<opentag::ota::UpdateSnapshot> OtaWorker::process(
    CommandSlot& command) {
  auto result = core::Result<opentag::ota::UpdateSnapshot>::failure(
      worker_error("Unsupported OTA owner command", false));
  bool reconcile_cleanup = false;
  switch (command.kind) {
    case CommandKind::begin:
      result = manager_.begin_upload(command.begin_request, command.now_ms);
      if (!result.ok()) {
        operations_.fail(
            command.begin_request.operation_id, millis(), result.error());
      }
      break;
    case CommandKind::write:
      result = manager_.write_chunk(
          command.precondition,
          {command.chunk.data(), command.chunk_size},
          command.now_ms);
      break;
    case CommandKind::finish_and_activate:
      // This command validates and closes the inactive image only. Selection
      // is deliberately deferred to the confirmed reboot command below.
      result = manager_.finish_upload(command.precondition, command.now_ms);
      break;
    case CommandKind::abort:
      if (const auto operation =
              operations_.get(command.precondition.operation_id);
          operation.has_value() &&
          terminal_operation_state(operation->state)) {
        // A late queued abort must not mutate a candidate after the correlated
        // upload receipt has reached its immutable authoritative outcome.
        result = core::Result<opentag::ota::UpdateSnapshot>::failure(
            worker_conflict(
                "Firmware upload operation already reached a terminal result"));
      } else {
        result = manager_.cancel(command.precondition, command.now_ms);
        reconcile_cleanup = command.cleanup_reason.has_value();
      }
      break;
    case CommandKind::cancel: {
      operations_.mark_running(
          command.control_operation_id,
          command.now_ms,
          "Cancelling staged OTA image");
      result = manager_.cancel(command.precondition, command.now_ms);
      publish_owner_snapshot();
      if (result.ok()) {
        operations_.succeed(
            command.control_operation_id,
            command.now_ms,
            "Staged OTA image cancelled");
      } else {
        operations_.fail(
            command.control_operation_id, command.now_ms, result.error());
      }
      clear_control(command.control_operation_id);
      break;
    }
    case CommandKind::reboot: {
      operations_.mark_running(
          command.control_operation_id,
          command.now_ms,
          "Selecting validated inactive OTA image");
      result = manager_.activate(command.precondition, command.now_ms);
      publish_owner_snapshot();
      if (!result.ok()) {
        operations_.fail(
            command.control_operation_id, command.now_ms, result.error());
        clear_control(command.control_operation_id);
        break;
      }
      result = manager_.mark_reboot_pending(
          command.precondition, command.now_ms);
      publish_owner_snapshot();
      if (!result.ok()) {
        operations_.fail(
            command.control_operation_id, command.now_ms, result.error());
        clear_control(command.control_operation_id);
        break;
      }
      operations_.mark_running(
          command.control_operation_id,
          command.now_ms,
          "Rebooting into validated OTA candidate");
      vTaskDelay(pdMS_TO_TICKS(750U));
      esp_restart();
      // A successful restart does not return. If a platform unexpectedly
      // does return, retain both the active control and lifecycle lease so a
      // repeated request coalesces and no conflicting reset can be accepted.
      break;
    }
    case CommandKind::boot_health:
      break;
  }
  publish_owner_snapshot();
  if (reconcile_cleanup && command.cleanup_reason.has_value()) {
    reconcile_upload_cleanup(
        command.precondition,
        millis(),
        *command.cleanup_reason,
        result,
        true);
  }
  reconcile_published_lifecycle();
  return result;
}

bool OtaWorker::process_boot_health(
    opentag::ota::CandidateHealthDecision decision,
    std::uint32_t now_ms,
    std::uint8_t attempt) {
  const auto current = snapshot();
  if (candidate_state(current.state) &&
      decision ==
          opentag::ota::CandidateHealthDecision::factory_reset_recovery &&
      lifecycle_owned(DeviceLifecycleOwner::candidate_validation)) {
    // A durable factory-reset marker is not an OTA health failure. Retain the
    // candidate exclusion lease and reboot so early storage recovery and the
    // rollback-enabled bootloader can each reconcile on the next boot.
    (void)manager_.handle_candidate_health(decision, now_ms);
    publish_owner_snapshot();
    reconcile_published_lifecycle();
    vTaskDelay(pdMS_TO_TICKS(750U));
    esp_restart();
    return false;
  }
  if (rollback_seed_recovery_required_) {
    const auto recovered = manager_.recover_rollback_seed(now_ms);
    rollback_seed_recovery_required_ = !recovered.ok();
    publish_owner_snapshot();
    reconcile_published_lifecycle();
    return rollback_seed_recovery_required_ &&
        attempt < boot_health_retry_limit;
  }
  if (candidate_rollback_required_) {
    return attempt_candidate_rollback(now_ms, attempt);
  }
  if (!candidate_state(current.state)) {
    if (decision == opentag::ota::CandidateHealthDecision::healthy &&
        storage_.boot_confirmation_pending()) {
      const auto confirmed = storage_.confirm_healthy_boot();
      reconcile_lifecycle(current);
      return !confirmed.ok() && attempt < boot_health_retry_limit;
    }
    reconcile_lifecycle(current);
    return false;
  }

  if (decision == opentag::ota::CandidateHealthDecision::healthy) {
    // The manager's candidate timestamp and OTA constant are the sole
    // eligibility check before clearing local crash/boot-pending state.
    const auto elapsed = static_cast<std::uint32_t>(
        now_ms - current.candidate_started_at_ms);
    if (elapsed < opentag::ota::candidate_confirmation_window_ms) {
      (void)manager_.handle_candidate_health(
          opentag::ota::CandidateHealthDecision::stabilizing, now_ms);
      publish_owner_snapshot();
      reconcile_published_lifecycle();
      return false;
    }

    if (storage_.boot_confirmation_pending()) {
      const auto local_confirmed = storage_.confirm_healthy_boot();
      if (!local_confirmed.ok()) {
        if (attempt < boot_health_retry_limit) return true;
        candidate_rollback_required_ = true;
        return attempt_candidate_rollback(now_ms, attempt);
      }
    }

    const auto confirmed = manager_.confirm_candidate(now_ms);
    publish_owner_snapshot();
    const bool physically_or_logically_confirmed =
        reconcile_published_candidate_confirmation();
    if (confirmed.ok() || physically_or_logically_confirmed) {
      candidate_rollback_required_ = false;
      return false;
    }
    if (attempt < boot_health_retry_limit) return true;
    candidate_rollback_required_ = true;
    return attempt_candidate_rollback(now_ms, attempt);
  }
  if (decision == opentag::ota::CandidateHealthDecision::unhealthy) {
    candidate_rollback_required_ = true;
    return attempt_candidate_rollback(now_ms, attempt);
  }
  const auto handled = manager_.handle_candidate_health(decision, now_ms);
  publish_owner_snapshot();
  reconcile_published_lifecycle();
  return !handled.ok() && attempt < boot_health_retry_limit;
}

bool OtaWorker::attempt_candidate_rollback(
    std::uint32_t now_ms,
    std::uint8_t attempt) {
  const auto rolled_back = manager_.rollback_candidate(now_ms);
  publish_owner_snapshot();
  const bool terminal = reconcile_published_rollback(rolled_back.ok());
  if (terminal) candidate_rollback_required_ = false;
  return !terminal && attempt < boot_health_retry_limit;
}

}  // namespace opentag::application

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "application/device_lifecycle_gate.hpp"
#include "application/operation_registry.hpp"
#include "ota/update_manager.hpp"
#include "platform/storage/storage_service.hpp"

namespace opentag::application {

// Sole post-boot owner of flash OTA APIs and the portable UpdateManager. HTTP
// callers stream fixed chunks through a bounded pool and wait for an owner-task
// acknowledgement; they never write or select flash partitions directly.
class OtaWorker final {
 public:
  static constexpr std::size_t queue_depth = 4U;
  static constexpr std::uint32_t task_stack_bytes = 24576U;
  static constexpr std::uint32_t command_timeout_ms = 30000U;
  static constexpr std::uint8_t boot_health_retry_limit = 2U;
  static constexpr std::uint32_t boot_health_retry_delay_ms = 100U;

  OtaWorker(
      opentag::ota::UpdateManager& manager,
      platform::storage::StorageService& storage,
      OperationRegistry& operations,
      DeviceLifecycleGate& lifecycle)
      : manager_(manager),
        storage_(storage),
        operations_(operations),
        lifecycle_(lifecycle) {}

  [[nodiscard]] bool start(std::uint32_t now_ms);
  [[nodiscard]] TaskHandle_t task_handle() const { return task_; }
  [[nodiscard]] bool ready() const {
    return ready_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::size_t pending() const {
    return pending_.load(std::memory_order_relaxed);
  }
  // Returns a bounded copy published by the OTA owner task. This never
  // acquires UpdateManager's mutex or waits for flash/storage work.
  [[nodiscard]] opentag::ota::UpdateSnapshot snapshot() const;

  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot> begin_upload(
      const opentag::ota::BeginUploadRequest& request,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot> write_chunk(
      opentag::ota::OperationPrecondition precondition,
      core::ByteView chunk,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot>
  finish_and_activate(
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot> abort_upload(
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms,
      core::Error terminal_reason);

  [[nodiscard]] CommandReceipt submit_reboot(
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms);
  [[nodiscard]] CommandReceipt submit_cancel(
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms);
  [[nodiscard]] bool submit_boot_health(
      opentag::ota::CandidateHealthDecision decision,
      std::uint32_t now_ms);

 private:
  enum class CommandKind : std::uint8_t {
    begin,
    write,
    finish_and_activate,
    abort,
    reboot,
    cancel,
    boot_health,
  };

  struct CommandSlot {
    CommandKind kind{CommandKind::begin};
    opentag::ota::BeginUploadRequest begin_request;
    opentag::ota::OperationPrecondition precondition;
    std::array<std::uint8_t, opentag::ota::maximum_upload_chunk_bytes> chunk{};
    std::size_t chunk_size{0U};
    opentag::ota::CandidateHealthDecision health{
        opentag::ota::CandidateHealthDecision::stabilizing};
    std::uint32_t now_ms{0U};
    std::uint64_t health_revision{0U};
    std::uint8_t health_attempt{0U};
    std::uint64_t control_operation_id{0U};
    std::optional<core::Error> cleanup_reason;
    std::optional<core::Result<opentag::ota::UpdateSnapshot>> result;
    SemaphoreHandle_t completion{nullptr};
    bool in_use{false};
    bool abandoned{false};
  };

  [[nodiscard]] std::optional<std::uint8_t> reserve_slot(bool abandoned);
  void release_slot(std::uint8_t index);
  [[nodiscard]] bool enqueue_slot(std::uint8_t index);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot> wait_for_slot(
      std::uint8_t index);
  void complete_slot(
      std::uint8_t index,
      core::Result<opentag::ota::UpdateSnapshot> result);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot> execute_sync(
      std::uint8_t index);
  // Owner-task only: take UpdateManager's mutex after a manager transition has
  // returned, then publish the fixed-size snapshot under an independent lock.
  void publish_owner_snapshot();
  void reconcile_published_lifecycle();
  [[nodiscard]] bool reconcile_published_candidate_confirmation();
  [[nodiscard]] bool reconcile_published_rollback(bool transition_ok);
  void reconcile_upload_cleanup(
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms,
      const core::Error& terminal_reason,
      const core::Result<opentag::ota::UpdateSnapshot>& cleanup,
      bool authoritative_cleanup_attempt);
  void reconcile_late_finish(
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms,
      const core::Result<opentag::ota::UpdateSnapshot>& result);
  void cleanup_pre_task_resources();
  [[nodiscard]] core::Result<void> acquire_lifecycle(
      DeviceLifecycleOwner owner);
  void release_lifecycle();
  [[nodiscard]] bool lifecycle_owned(DeviceLifecycleOwner owner) const;
  void reconcile_lifecycle(const opentag::ota::UpdateSnapshot& state);

  [[nodiscard]] CommandReceipt submit_control(
      CommandKind kind,
      OperationKind operation_kind,
      const char* queued_message,
      opentag::ota::OperationPrecondition precondition,
      std::uint32_t now_ms);
  void clear_control(std::uint64_t operation_id);
  [[nodiscard]] bool queue_boot_health_locked();

  static void task_entry(void* context);
  void run(std::uint32_t initialized_at_ms);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot> process(
      CommandSlot& command);
  [[nodiscard]] bool process_boot_health(
      opentag::ota::CandidateHealthDecision decision,
      std::uint32_t now_ms,
      std::uint8_t attempt);
  [[nodiscard]] bool attempt_candidate_rollback(
      std::uint32_t now_ms,
      std::uint8_t attempt);

  opentag::ota::UpdateManager& manager_;
  platform::storage::StorageService& storage_;
  OperationRegistry& operations_;
  DeviceLifecycleGate& lifecycle_;
  QueueHandle_t queue_{nullptr};
  SemaphoreHandle_t startup_complete_{nullptr};
  TaskHandle_t task_{nullptr};
  std::array<CommandSlot, queue_depth> slots_{};
  mutable std::mutex slots_mutex_;
  std::optional<opentag::ota::OperationPrecondition>
      pending_finish_cleanup_;
  mutable std::mutex published_snapshot_mutex_;
  opentag::ota::UpdateSnapshot published_snapshot_;
  mutable std::mutex lifecycle_mutex_;
  DeviceLifecycleLease lifecycle_lease_;
  mutable std::mutex control_mutex_;
  bool control_active_{false};
  CommandKind control_kind_{CommandKind::reboot};
  opentag::ota::OperationPrecondition control_precondition_{};
  std::uint64_t control_operation_id_{0U};
  mutable std::mutex boot_health_mutex_;
  bool boot_health_queued_{false};
  std::uint64_t boot_health_revision_{0U};
  opentag::ota::CandidateHealthDecision boot_health_decision_{
      opentag::ota::CandidateHealthDecision::stabilizing};
  std::uint32_t boot_health_now_ms_{0U};
  std::uint8_t boot_health_attempt_{0U};
  bool rollback_seed_recovery_required_{false};
  bool candidate_rollback_required_{false};
  std::atomic_size_t pending_{0U};
  std::atomic_bool ready_{false};
  std::atomic_bool startup_ok_{false};
  std::uint32_t initialized_at_ms_{0U};
};

}  // namespace opentag::application

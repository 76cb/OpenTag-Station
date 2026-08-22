#pragma once

#include "ota/update_manager.hpp"

namespace opentag::ota {

enum class UploadCleanupDisposition : std::uint8_t {
  aborted,
  failed,
  validated,
  unresolved,
};

enum class UploadOperationResolution : std::uint8_t {
  running,
  succeeded,
  failed,
};

// Classifies only states that prove what happened to the exact upload
// generation. A caller must keep the operation nonterminal for unresolved
// states: an owner-task timeout is not evidence that flash work stopped.
[[nodiscard]] inline UploadCleanupDisposition classify_upload_cleanup(
    OperationPrecondition precondition,
    const UpdateSnapshot& snapshot,
    bool cleanup_succeeded) {
  if (cleanup_succeeded) return UploadCleanupDisposition::aborted;

  const bool same_operation = precondition.operation_id != 0U &&
      precondition.generation != 0U &&
      snapshot.operation_id == precondition.operation_id &&
      snapshot.generation == precondition.generation;
  const bool exact_generation_cleared = precondition.generation != 0U &&
      snapshot.operation_id == 0U &&
      snapshot.generation == precondition.generation;
  if (exact_generation_cleared && snapshot.state == UpdateState::idle) {
    return UploadCleanupDisposition::aborted;
  }
  if (exact_generation_cleared && snapshot.state == UpdateState::failed) {
    return UploadCleanupDisposition::failed;
  }
  if (!same_operation) return UploadCleanupDisposition::unresolved;

  if (snapshot.state == UpdateState::failed) {
    return UploadCleanupDisposition::failed;
  }
  if (snapshot.validation_passed &&
      (snapshot.state == UpdateState::ready_to_activate ||
       snapshot.state == UpdateState::ready_to_reboot ||
       snapshot.state == UpdateState::reboot_pending ||
       snapshot.state == UpdateState::candidate_boot ||
       snapshot.state == UpdateState::validating_candidate ||
       snapshot.state == UpdateState::confirmed ||
       snapshot.state == UpdateState::rollback_pending ||
       snapshot.state == UpdateState::rolled_back)) {
    return UploadCleanupDisposition::validated;
  }
  return UploadCleanupDisposition::unresolved;
}

// A validated snapshot is terminal success only after a correlated cleanup
// attempt is authoritative. While a timed-out finish is still awaiting owner
// cleanup, publishing success would expose a transient result that a queued
// abort can invalidate.
[[nodiscard]] inline UploadOperationResolution resolve_upload_operation(
    UploadCleanupDisposition disposition,
    bool finish_cleanup_pending,
    bool authoritative_cleanup_attempt) {
  if (disposition == UploadCleanupDisposition::aborted ||
      disposition == UploadCleanupDisposition::failed) {
    return UploadOperationResolution::failed;
  }
  if (disposition == UploadCleanupDisposition::validated &&
      (!finish_cleanup_pending || authoritative_cleanup_attempt)) {
    return UploadOperationResolution::succeeded;
  }
  return UploadOperationResolution::running;
}

}  // namespace opentag::ota

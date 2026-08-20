#pragma once

#include <cstdint>

#include "core/result.hpp"

namespace opentag::application {

enum class ApplicationState {
  booting,
  idle,
  tag_detected,
  reading_tag,
  tag_parsed,
  resolving_spool,
  spool_ready,
  assigning,
  assignment_complete,
  reconciling,
  writing_tag,
  error,
};

[[nodiscard]] constexpr const char* to_string(ApplicationState state) {
  switch (state) {
    case ApplicationState::booting: return "BOOTING";
    case ApplicationState::idle: return "IDLE";
    case ApplicationState::tag_detected: return "TAG_DETECTED";
    case ApplicationState::reading_tag: return "READING_TAG";
    case ApplicationState::tag_parsed: return "TAG_PARSED";
    case ApplicationState::resolving_spool: return "RESOLVING_SPOOL";
    case ApplicationState::spool_ready: return "SPOOL_READY";
    case ApplicationState::assigning: return "ASSIGNING";
    case ApplicationState::assignment_complete: return "ASSIGNMENT_COMPLETE";
    case ApplicationState::reconciling: return "RECONCILING";
    case ApplicationState::writing_tag: return "WRITING_TAG";
    case ApplicationState::error: return "ERROR";
  }
  return "UNKNOWN";
}

class ApplicationStateMachine {
 public:
  [[nodiscard]] ApplicationState state() const { return state_; }
  [[nodiscard]] bool can_transition(ApplicationState next) const;
  core::Result<void> transition(ApplicationState next);

 private:
  ApplicationState state_{ApplicationState::booting};
};

enum class TagPresenceState {
  no_tag,
  tag_present,
  tag_processed,
  tag_removed,
};

[[nodiscard]] constexpr const char* to_string(TagPresenceState state) {
  switch (state) {
    case TagPresenceState::no_tag: return "NO_TAG";
    case TagPresenceState::tag_present: return "TAG_PRESENT";
    case TagPresenceState::tag_processed: return "TAG_PROCESSED";
    case TagPresenceState::tag_removed: return "TAG_REMOVED";
  }
  return "UNKNOWN";
}

class TagPresenceMachine {
 public:
  explicit TagPresenceMachine(std::uint32_t debounce_ms = 75U)
      : debounce_ms_(debounce_ms) {}

  // Returns true only when the debounced logical state changes.
  bool observe(bool physically_present, std::uint32_t now_ms);
  bool mark_processed();
  bool acknowledge_removed();

  [[nodiscard]] TagPresenceState state() const { return state_; }
  [[nodiscard]] bool should_begin_read() const {
    return state_ == TagPresenceState::tag_present;
  }

 private:
  TagPresenceState state_{TagPresenceState::no_tag};
  bool candidate_present_{false};
  bool candidate_initialized_{false};
  bool stable_present_{false};
  std::uint32_t candidate_since_ms_{0};
  std::uint32_t debounce_ms_{75};
};

}  // namespace opentag::application

#pragma once

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

}  // namespace opentag::application

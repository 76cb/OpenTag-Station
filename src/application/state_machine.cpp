#include "application/state_machine.hpp"

#include <string>

namespace opentag::application {

bool ApplicationStateMachine::can_transition(ApplicationState next) const {
  if (next == state_) {
    return true;
  }
  if (next == ApplicationState::error) {
    return state_ != ApplicationState::booting;
  }

  switch (state_) {
    case ApplicationState::booting:
      return next == ApplicationState::idle;
    case ApplicationState::idle:
      return next == ApplicationState::tag_detected;
    case ApplicationState::tag_detected:
      return next == ApplicationState::reading_tag || next == ApplicationState::idle;
    case ApplicationState::reading_tag:
      return next == ApplicationState::tag_parsed || next == ApplicationState::idle;
    case ApplicationState::tag_parsed:
      return next == ApplicationState::resolving_spool || next == ApplicationState::idle;
    case ApplicationState::resolving_spool:
      return next == ApplicationState::spool_ready || next == ApplicationState::idle;
    case ApplicationState::spool_ready:
      return next == ApplicationState::assigning ||
             next == ApplicationState::reconciling ||
             next == ApplicationState::idle;
    case ApplicationState::assigning:
      return next == ApplicationState::assignment_complete ||
             next == ApplicationState::spool_ready;
    case ApplicationState::assignment_complete:
      return next == ApplicationState::spool_ready || next == ApplicationState::idle;
    case ApplicationState::reconciling:
      return next == ApplicationState::writing_tag ||
             next == ApplicationState::spool_ready;
    case ApplicationState::writing_tag:
      return next == ApplicationState::spool_ready || next == ApplicationState::idle;
    case ApplicationState::error:
      return next == ApplicationState::idle;
  }
  return false;
}

core::Result<void> ApplicationStateMachine::transition(ApplicationState next) {
  if (!can_transition(next)) {
    return core::Result<void>::failure({
        core::ErrorCategory::configuration,
        std::string("invalid application transition ") + to_string(state_) +
            " -> " + to_string(next),
        false,
    });
  }
  state_ = next;
  return core::Result<void>::success();
}

}  // namespace opentag::application

#include "services/first_run_setup.hpp"

#include "core/error.hpp"

namespace opentag::services {
namespace {

core::Error setup_error(const char* message) {
  return {core::ErrorCategory::configuration, message, false};
}

constexpr auto last_step = static_cast<std::uint8_t>(SetupStep::ready);

}  // namespace

const char* to_string(SetupStep step) {
  switch (step) {
    case SetupStep::welcome: return "Welcome";
    case SetupStep::wifi: return "Wi-Fi";
    case SetupStep::spoolman: return "Spoolman";
    case SetupStep::filabridge: return "FilaBridge";
    case SetupStep::printer_selection: return "Printer selection";
    case SetupStep::scale_calibration: return "Scale calibration";
    case SetupStep::nfc_status: return "NFC status";
    case SetupStep::ready: return "Ready";
  }
  return "Unknown";
}

bool FirstRunSetup::complete() const {
  return configuration_.snapshot().setup.ready_confirmed;
}

bool FirstRunSetup::step_complete(SetupStep step) const {
  return (configuration_.snapshot().setup.completed_steps & step_bit(step)) != 0U;
}

core::Result<void> FirstRunSetup::mark_complete(SetupStep step) {
  auto updated = configuration_.snapshot();
  updated.setup.completed_steps |= step_bit(step);
  if (step == SetupStep::ready) updated.setup.ready_confirmed = true;
  return configuration_.replace(updated);
}

core::Result<void> FirstRunSetup::go_to(SetupStep step) {
  if (static_cast<std::uint8_t>(step) > last_step) {
    return core::Result<void>::failure(setup_error("setup step is invalid"));
  }
  current_ = step;
  return core::Result<void>::success();
}

core::Result<void> FirstRunSetup::next() {
  const auto value = static_cast<std::uint8_t>(current_);
  if (value >= last_step) {
    return core::Result<void>::failure(setup_error("setup is already at the final step"));
  }
  current_ = static_cast<SetupStep>(value + 1U);
  return core::Result<void>::success();
}

core::Result<void> FirstRunSetup::previous() {
  const auto value = static_cast<std::uint8_t>(current_);
  if (value == 0U) {
    return core::Result<void>::failure(setup_error("setup is already at the first step"));
  }
  current_ = static_cast<SetupStep>(value - 1U);
  return core::Result<void>::success();
}

}  // namespace opentag::services

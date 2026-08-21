#include "application/boot_health_policy.hpp"

namespace opentag::application {
namespace {

void require(
    bool ready,
    BootHealthRequirement requirement,
    std::uint32_t& missing) {
  if (!ready) missing |= requirement_bit(requirement);
}

}  // namespace

std::uint32_t BootHealthPolicy::missing_requirements(
    const BootHealthSignals& signals) {
  std::uint32_t missing = 0U;
  require(signals.storage_ready, BootHealthRequirement::storage, missing);
  require(
      signals.configuration_initialized ||
          signals.configuration_safely_degraded,
      BootHealthRequirement::configuration,
      missing);
  require(signals.application_ready, BootHealthRequirement::application, missing);
  require(signals.display_ready, BootHealthRequirement::display, missing);
  require(signals.ui_task_running, BootHealthRequirement::ui_task, missing);
  require(
      signals.configuration_task_running,
      BootHealthRequirement::configuration_task,
      missing);
  require(signals.backend_task_running, BootHealthRequirement::backend_task, missing);
  require(signals.scale_commands_ready, BootHealthRequirement::scale_commands, missing);
  require(signals.scale_task_running, BootHealthRequirement::scale_task, missing);
  require(signals.network_task_running, BootHealthRequirement::network_task, missing);
  require(
      signals.device_control_task_running,
      BootHealthRequirement::device_control_task,
      missing);
  require(signals.web_server_running, BootHealthRequirement::web_server, missing);
  require(signals.ota_task_running, BootHealthRequirement::ota_task, missing);
  return missing;
}

BootHealthEvaluation BootHealthPolicy::evaluate(
    std::uint32_t now_ms,
    const BootHealthSignals& signals) const {
  BootHealthEvaluation result;
  result.elapsed_ms = static_cast<std::uint32_t>(now_ms - boot_started_ms_);
  result.missing_requirements = missing_requirements(signals);

  // Durable reset recovery is intentionally not classified as failed OTA
  // health. Its owner must finish or reboot through the reset recovery path.
  if (signals.factory_reset_recovery_pending) {
    result.state = BootHealthState::factory_reset_recovery;
    return result;
  }
  if (signals.fatal_initialization_error) {
    result.state = BootHealthState::unhealthy;
    return result;
  }
  if (result.elapsed_ms < confirmation_window_ms) {
    result.state = BootHealthState::stabilizing;
    return result;
  }
  result.state = result.missing_requirements == 0U
                     ? BootHealthState::healthy
                     : BootHealthState::unhealthy;
  return result;
}

}  // namespace opentag::application

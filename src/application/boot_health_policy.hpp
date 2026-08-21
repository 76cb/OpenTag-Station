#pragma once

#include <cstdint>

namespace opentag::application {

enum class BootHealthState : std::uint8_t {
  stabilizing,
  healthy,
  unhealthy,
  factory_reset_recovery,
};

enum class BootHealthRequirement : std::uint32_t {
  none = 0U,
  storage = 1U << 0U,
  configuration = 1U << 1U,
  application = 1U << 2U,
  display = 1U << 3U,
  ui_task = 1U << 4U,
  configuration_task = 1U << 5U,
  backend_task = 1U << 6U,
  scale_commands = 1U << 7U,
  scale_task = 1U << 8U,
  network_task = 1U << 9U,
  device_control_task = 1U << 10U,
  web_server = 1U << 11U,
  ota_task = 1U << 12U,
};

[[nodiscard]] constexpr std::uint32_t requirement_bit(
    BootHealthRequirement value) {
  return static_cast<std::uint32_t>(value);
}

struct BootHealthSignals {
  bool storage_ready{false};
  bool configuration_initialized{false};
  bool configuration_safely_degraded{false};
  bool application_ready{false};
  bool display_ready{false};
  bool ui_task_running{false};
  bool configuration_task_running{false};
  bool backend_task_running{false};
  bool scale_commands_ready{false};
  bool scale_task_running{false};
  bool network_task_running{false};
  bool device_control_task_running{false};
  bool web_server_running{false};
  bool ota_task_running{false};
  bool factory_reset_recovery_pending{false};
  bool fatal_initialization_error{false};
};

struct BootHealthEvaluation {
  BootHealthState state{BootHealthState::stabilizing};
  std::uint32_t elapsed_ms{0U};
  std::uint32_t missing_requirements{0U};

  [[nodiscard]] bool missing(BootHealthRequirement requirement) const {
    return (missing_requirements & requirement_bit(requirement)) != 0U;
  }
};

// One policy governs both ordinary boot tracking and OTA candidate validation.
// The caller decides whether an unhealthy result merely leaves the local boot
// marker pending or actively requests bootloader rollback.
class BootHealthPolicy final {
 public:
  static constexpr std::uint32_t confirmation_window_ms = 30000U;

  explicit BootHealthPolicy(std::uint32_t boot_started_ms)
      : boot_started_ms_(boot_started_ms) {}

  [[nodiscard]] BootHealthEvaluation evaluate(
      std::uint32_t now_ms,
      const BootHealthSignals& signals) const;

 private:
  [[nodiscard]] static std::uint32_t missing_requirements(
      const BootHealthSignals& signals);

  std::uint32_t boot_started_ms_{0U};
};

}  // namespace opentag::application

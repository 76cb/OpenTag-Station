#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentag::application {

enum class TaskOwner {
  ui,
  configuration,
  scale,
  nfc,
  network,
  backend,
  web,
  device_control,
  ota,
};

struct TaskContract {
  TaskOwner owner;
  const char* name;
  std::size_t command_queue_depth;
  std::uint32_t maximum_block_ms;
};

// These are ownership and back-pressure contracts, not a requirement to start
// idle tasks. A worker is created only when its subsystem is compiled and
// initialized. The UI worker is the first active contract in Phase 1.
inline constexpr std::array<TaskContract, 9> task_contracts = {{
    {TaskOwner::ui, "ui", 16U, 20U},
    {TaskOwner::configuration, "configuration", 8U, 1000U},
    {TaskOwner::scale, "scale", 4U, 1000U},
    {TaskOwner::nfc, "nfc", 8U, 250U},
    {TaskOwner::network, "network", 8U, 1000U},
    {TaskOwner::backend, "backend", 12U, 40000U},
    {TaskOwner::web, "web", 2U, 1000U},
    {TaskOwner::device_control, "device-control", 1U, 1000U},
    {TaskOwner::ota, "ota", 4U, 1000U},
}};

}  // namespace opentag::application

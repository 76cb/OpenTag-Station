#pragma once
#include <functional>

namespace opentag::services {
// Answers "is the station about to restart?" (a reboot, a factory reset, a
// firmware update or the health check of new firmware is in progress). The
// backend asks before it changes a tag or Spoolman, because a restart in the
// middle of either leaves half-done work behind (#85, A3/WB-D4).
using RestartPending = std::function<bool()>;
inline constexpr const char* restart_pending_message =
    "The station is restarting, resetting or checking new firmware. Try again "
    "after it has restarted.";
inline bool restart_pending(const RestartPending& pending) {
  return pending && pending();
}
}  // namespace opentag::services

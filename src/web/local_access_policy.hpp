#pragma once

#include <string_view>

namespace opentag::web {

// One source of truth for the station's local-LAN access policy. An empty
// token deliberately selects trusted-LAN mode: authentication is disabled,
// browser mutations remain enabled, and health is not degraded. Configuring a
// token enables bearer authentication without changing local service health.
struct LocalAccessPolicy {
  bool authentication_enabled{false};
  bool browser_mutations_enabled{true};
  bool health_degraded{false};
};

[[nodiscard]] constexpr LocalAccessPolicy local_access_policy(
    std::string_view configured_token) {
  return {
      !configured_token.empty(),
      true,
      false,
  };
}

}  // namespace opentag::web

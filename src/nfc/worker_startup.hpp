#pragma once

#include <atomic>
#include "nfc/read_only_service.hpp"

namespace opentag::nfc {
// Network owner opens this latch; only the backend owner polls hardware.
// No allocation or RFAL call occurs on the network/UI/httpd tasks.
class WorkerStartup {
 public:
  void enable_when_configured(bool configuration_ready, bool configured, bool connected,
                             bool provisioning, bool grace) {
    if (!configuration_ready || !configured || !connected || provisioning || grace)
      return;
    enabled_.store(true);
  }
  bool enabled() const { return enabled_.load(); }
  void describe(ReadSnapshot& snapshot) const {
    if (!enabled()) snapshot.state = ReadState::deferred;
  }
 private:
  std::atomic_bool enabled_{false};
};
}  // namespace opentag::nfc

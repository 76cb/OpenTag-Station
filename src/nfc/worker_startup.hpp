#pragma once

#include <atomic>
#include "nfc/read_only_service.hpp"

namespace opentag::nfc {
// Only the network owner attempts startup. Atomic state also serves UI/API
// readers. Failure is latched until reboot: no allocation retry storm.
class WorkerStartup {
 public:
  template <class Create>
  void poll(bool configuration_ready, bool configured, bool connected,
            bool provisioning, bool grace, Create create) {
    if (!configuration_ready || !configured || !connected || provisioning || grace)
      return;
    auto expected = State::deferred;
    if (!state_.compare_exchange_strong(expected, State::starting)) return;
    state_.store(create() ? State::running : State::failed);
  }
  void describe(ReadSnapshot& snapshot) const {
    switch (state_.load()) {
      case State::deferred:
        snapshot.state = ReadState::deferred;
        break;
      case State::failed:
        snapshot.state = ReadState::error;
        snapshot.error = core::Error{core::ErrorCategory::nfc_communication,
            "NFC task allocation failed; networking remains available; reboot to retry", false};
        break;
      default: break;
    }
  }
 private:
  enum class State { deferred, starting, running, failed };
  std::atomic<State> state_{State::deferred};
};
}  // namespace opentag::nfc

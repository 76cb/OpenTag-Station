#pragma once

#include <cstdint>

namespace opentag::nfc {
// Decides what one NFC poll cycle does. NFC-V is asked first on every cycle,
// exactly as since 1.0. NFC-A is asked in the same field window, after NFC-V,
// and only while NFC-V finds nothing and no NFC-V tag is being tracked. An
// NFC-V tag is therefore found and lost on the same polls as before; the
// only cost is one short NFC-A request (REQA) on cycles with no NFC-V tag.
class PollSchedule {
 public:
  static constexpr std::uint32_t cadence_ms = 500U;
  static constexpr std::uint32_t error_cadence_ms = 5000U;
  // True when a poll cycle is due. The first call is always due.
  bool due(std::uint32_t now_ms, bool in_error) {
    const auto cadence = in_error ? error_cadence_ms : cadence_ms;
    if (polled_ && static_cast<std::uint32_t>(now_ms - last_poll_ms_) < cadence) return false;
    polled_ = true;
    last_poll_ms_ = now_ms;
    return true;
  }
  // `nfcv_found`: the NFC-V inventory of this cycle answered with a tag.
  // `nfcv_tracked`: a tag is still counted present (stabilizing, read, or in
  // its one tolerated missed poll).
  static bool nfca_turn(bool nfcv_found, bool nfcv_tracked) {
    return !nfcv_found && !nfcv_tracked;
  }

 private:
  std::uint32_t last_poll_ms_{0};
  bool polled_{false};
};
}  // namespace opentag::nfc

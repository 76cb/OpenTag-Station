#pragma once

#include <mutex>

#include <Preferences.h>

#include "ota/update_manager.hpp"

namespace opentag::platform::ota {

// OTA lifecycle metadata is intentionally isolated from the application
// configuration namespace. A configuration factory reset never erases or
// fabricates boot-slot state while an OTA/candidate lease is active.
class Esp32UpdateRecordStore final : public opentag::ota::IUpdateRecordStore {
 public:
  [[nodiscard]] core::Result<void> initialize();
  [[nodiscard]] core::Result<std::optional<opentag::ota::UpdateRecord>> load()
      override;
  [[nodiscard]] core::Result<std::uint64_t> reserve_generation(
      std::uint64_t minimum_exclusive) override;
  [[nodiscard]] core::Result<void> save(
      const opentag::ota::UpdateRecord& record) override;

 private:
  Preferences preferences_;
  std::mutex mutex_;
  bool initialized_{false};
};

}  // namespace opentag::platform::ota

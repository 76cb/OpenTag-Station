#pragma once

#include <cstdint>
#include <mutex>

namespace opentag::application {

enum class DeviceLifecycleOwner : std::uint8_t {
  none,
  reboot,
  factory_reset,
  ota_update,
  candidate_validation,
};

struct DeviceLifecycleLease {
  DeviceLifecycleOwner owner{DeviceLifecycleOwner::none};
  std::uint64_t generation{0U};

  [[nodiscard]] explicit operator bool() const {
    return owner != DeviceLifecycleOwner::none && generation != 0U;
  }
};

struct DeviceLifecycleSnapshot {
  DeviceLifecycleOwner owner{DeviceLifecycleOwner::none};
  std::uint64_t generation{0U};

  [[nodiscard]] bool busy() const {
    return owner != DeviceLifecycleOwner::none;
  }
};

[[nodiscard]] const char* to_string(DeviceLifecycleOwner owner);

// Serializes device-wide lifecycle actions that cannot safely overlap. A lease
// is an opaque generation token: only the holder of the current token can
// release the gate, so a stale command cannot unlock a newer operation.
class DeviceLifecycleGate final {
 public:
  [[nodiscard]] DeviceLifecycleLease try_acquire(DeviceLifecycleOwner owner);
  [[nodiscard]] bool release(DeviceLifecycleLease lease);
  [[nodiscard]] bool owns(DeviceLifecycleLease lease) const;
  [[nodiscard]] DeviceLifecycleSnapshot snapshot() const;

 private:
  mutable std::mutex mutex_;
  DeviceLifecycleOwner owner_{DeviceLifecycleOwner::none};
  std::uint64_t generation_{0U};
};

}  // namespace opentag::application

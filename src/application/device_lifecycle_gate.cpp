#include "application/device_lifecycle_gate.hpp"

#include <limits>

namespace opentag::application {

const char* to_string(DeviceLifecycleOwner owner) {
  switch (owner) {
    case DeviceLifecycleOwner::none: return "none";
    case DeviceLifecycleOwner::reboot: return "reboot";
    case DeviceLifecycleOwner::factory_reset: return "factory_reset";
    case DeviceLifecycleOwner::ota_update: return "ota_update";
    case DeviceLifecycleOwner::candidate_validation:
      return "candidate_validation";
  }
  return "unknown";
}

DeviceLifecycleLease DeviceLifecycleGate::try_acquire(
    DeviceLifecycleOwner owner) {
  if (owner == DeviceLifecycleOwner::none) return {};
  const std::lock_guard<std::mutex> lock(mutex_);
  if (owner_ != DeviceLifecycleOwner::none) return {};

  if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
    generation_ = 1U;
  } else {
    ++generation_;
    if (generation_ == 0U) generation_ = 1U;
  }
  owner_ = owner;
  return {owner_, generation_};
}

bool DeviceLifecycleGate::release(DeviceLifecycleLease lease) {
  if (!lease) return false;
  const std::lock_guard<std::mutex> lock(mutex_);
  if (owner_ != lease.owner || generation_ != lease.generation) return false;
  owner_ = DeviceLifecycleOwner::none;
  return true;
}

bool DeviceLifecycleGate::owns(DeviceLifecycleLease lease) const {
  if (!lease) return false;
  const std::lock_guard<std::mutex> lock(mutex_);
  return owner_ == lease.owner && generation_ == lease.generation;
}

DeviceLifecycleSnapshot DeviceLifecycleGate::snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return {owner_, generation_};
}

}  // namespace opentag::application

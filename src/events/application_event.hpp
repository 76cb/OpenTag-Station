#pragma once

#include <array>
#include <cstddef>

namespace opentag::events {

enum class EventType {
  tag_detected,
  tag_removed,
  tag_read_complete,
  tag_read_failed,
  weight_changed,
  weight_stable,
  spool_resolved,
  spool_not_found,
  spool_ambiguous,
  toolheads_updated,
  assignment_requested,
  assignment_confirmed,
  assignment_failed,
  backend_connected,
  backend_disconnected,
  ota_available,
  ota_started,
  ota_complete,
  ota_failed,
};

struct ApplicationEvent {
  EventType type;
  int value{0};
};

template <std::size_t Capacity>
class EventQueue {
 public:
  bool push(ApplicationEvent event) {
    if (size_ == Capacity) {
      return false;
    }
    items_[tail_] = event;
    tail_ = (tail_ + 1U) % Capacity;
    ++size_;
    return true;
  }

  bool pop(ApplicationEvent& event) {
    if (size_ == 0U) {
      return false;
    }
    event = items_[head_];
    head_ = (head_ + 1U) % Capacity;
    --size_;
    return true;
  }

  [[nodiscard]] std::size_t size() const { return size_; }

 private:
  std::array<ApplicationEvent, Capacity> items_{};
  std::size_t head_{0};
  std::size_t tail_{0};
  std::size_t size_{0};
};

}  // namespace opentag::events

#pragma once

#include <cstdint>

namespace opentag::integrations {

enum class BackendCapability : std::uint32_t {
  list_spools = 1U << 0U,
  get_spool = 1U << 1U,
  update_remaining_weight = 1U << 2U,
  create_spool = 1U << 3U,
  archive_spool = 1U << 4U,
  list_locations = 1U << 5U,
  set_location = 1U << 6U,
  get_printers = 1U << 7U,
  get_toolheads = 1U << 8U,
  map_toolhead = 1U << 9U,
  unmap_toolhead = 1U << 10U,
  get_print_state = 1U << 11U,
  websocket_status = 1U << 12U,
  nfc_mapping = 1U << 13U,
  print_history = 1U << 14U,
  health = 1U << 15U,
  runtime_version = 1U << 16U,
  search_spools = 1U << 17U,
  list_extra_fields = 1U << 18U,
  update_extra_fields = 1U << 19U,
};

class BackendCapabilities {
 public:
  void add(BackendCapability capability) {
    bits_ |= static_cast<std::uint32_t>(capability);
  }

  [[nodiscard]] bool has(BackendCapability capability) const {
    return (bits_ & static_cast<std::uint32_t>(capability)) != 0U;
  }

  [[nodiscard]] std::uint32_t bits() const { return bits_; }

 private:
  std::uint32_t bits_{0};
};

}  // namespace opentag::integrations

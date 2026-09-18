#pragma once
#include <string_view>

namespace opentag::services {
// Presentation only. No state here grants permission to mutate NFC or Spoolman.
enum class TagLifecycle { no_tag, reading, blank_compatible, linked, unlinked,
                          write_pending, cleanup_pending, ready, unsupported };
struct TagLifecycleInput {
  bool present{false}, blank{false}, valid{false}, linked{false}, unsupported{false};
  // Pending transactions survive absence/reboot. Completed results must match
  // the currently present physical UID before they override a fresh read.
  std::string_view phase;
  bool writer_matches{false};
};
inline TagLifecycle tag_lifecycle(const TagLifecycleInput& s) {
  if (s.phase == "unlink_pending" || s.phase == "unlinking" || s.phase == "clear_recovery")
    return TagLifecycle::cleanup_pending;
  if (s.phase == "write_recovery" || s.phase == "association_pending" || s.phase == "writing" ||
      s.phase == "verifying" || s.phase == "associating" || s.phase == "clearing")
    return TagLifecycle::write_pending;
  if (!s.present) return TagLifecycle::no_tag;
  if (s.writer_matches && (s.phase == "cleared" || s.phase == "complete"))
    return TagLifecycle::ready;
  if (s.blank) return TagLifecycle::blank_compatible;
  if (s.valid) return s.linked ? TagLifecycle::linked : TagLifecycle::unlinked;
  return s.unsupported ? TagLifecycle::unsupported : TagLifecycle::reading;
}
inline const char* to_string(TagLifecycle state) {
  switch (state) {
    case TagLifecycle::no_tag: return "NO_TAG";
    case TagLifecycle::reading: return "READING";
    case TagLifecycle::blank_compatible: return "BLANK_COMPATIBLE";
    case TagLifecycle::linked: return "OPENPRINTTAG_LINKED";
    case TagLifecycle::unlinked: return "OPENPRINTTAG_UNLINKED";
    case TagLifecycle::write_pending: return "WRITE_PENDING";
    case TagLifecycle::cleanup_pending: return "CLEANUP_PENDING";
    case TagLifecycle::ready: return "READY";
    case TagLifecycle::unsupported: return "UNSUPPORTED";
  }
  return "NO_TAG";
}
}  // namespace opentag::services

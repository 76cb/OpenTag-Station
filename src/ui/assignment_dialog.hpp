#pragma once
#include <cstring>

// Printer assignment confirmation dialog. The decision "did the user confirm?"
// is kept here, independent of LVGL, so it can be unit tested. Only an
// explicitly affirmative label submits; every other label (Cancel, Back, or an
// unknown/null label) must leave the toolhead mapping untouched.
namespace opentag::ui::assignment_dialog {

inline constexpr const char* cancel_label = "Cancel";
inline constexpr const char* assign_label = "Assign";
inline constexpr const char* replace_label = "Replace";
inline constexpr const char* override_label = "Assign anyway";
inline constexpr const char* override_replace_label = "Replace anyway";

inline bool is_confirmation(const char* label) {
  if (label == nullptr) return false;
  for (const char* affirmative : {assign_label, replace_label, override_label,
                                  override_replace_label})
    if (std::strcmp(label, affirmative) == 0) return true;
  return false;
}

}  // namespace opentag::ui::assignment_dialog

#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "services/weigh_sync.hpp"

namespace opentag::ui {
// The spool on the station now, as the workflow sees it.
struct WeighStation {
  bool tag_present{false};
  int spool_id{0};
  std::uint64_t generation{0};
  std::string name;
};
// What the touchscreen Weigh page shows. A measurement belongs to the spool
// it was taken for: after a swap the old receipt, name and UPDATE SPOOLMAN
// must not be shown as if they were the new spool's.
struct WeighView {
  bool current{false};       // the measurement is for the spool now on the station
  bool show_receipt{false};
  bool can_save{false};
  bool saving{false};        // a Spoolman update is running: WEIGH is refused
  std::string title, message;
  const char* weigh_label{"WEIGH"};
};
inline std::string weigh_grams(const std::optional<float>& value) {
  return value ? std::to_string(static_cast<long>(std::lround(*value))) + " g" : "Not set";
}
inline WeighView weigh_view(const services::WeighSyncSnapshot& m, const WeighStation& station) {
  WeighView v;
  // The generation changes whenever a different tag is identified (and when
  // the station is emptied); the spool ID catches a spool that was resolved
  // only after the measurement started.
  v.current = m.measurement_id != 0 && station.tag_present && m.generation == station.generation &&
              m.spool_id == station.spool_id;
  v.saving = m.phase == "updating";
  if (v.current) {
    v.show_receipt = true;
    v.title = !m.name.empty() ? m.name : station.name;
    v.can_save = m.phase == "ready" && !m.consumed;
    v.message = m.message;
    if (v.can_save && m.measured)
      v.message = "Tap UPDATE SPOOLMAN to set its remaining weight to " + weigh_grams(m.measured) + ".";
    v.weigh_label = services::weigh_button_label(m);
  } else {
    v.title = station.name;
    v.message = station.tag_present ? "Tap WEIGH to measure this spool."
                                    : "Place a tagged spool on the station, then tap WEIGH.";
  }
  if (v.saving)
    v.message = v.current ? "Saving to Spoolman..."
                          : "Saving the previous weight to Spoolman. Try again in a moment.";
  if (v.title.empty())
    v.title = "Scale";
  return v;
}
}  // namespace opentag::ui

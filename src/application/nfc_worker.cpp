#include "application/nfc_worker.hpp"

#include <Arduino.h>

namespace opentag::application {
bool NfcWorker::start() {
  return task_ ||
         xTaskCreatePinnedToCore(task_entry, "opentag-nfc", stack_bytes, this,
                                 1, &task_, 0) == pdPASS;
}
void NfcWorker::task_entry(void* context) {
  auto* self = static_cast<NfcWorker*>(context);
  for (;;) {
    self->run_once();
    vTaskDelay(pdMS_TO_TICKS(100U));
  }
}
void NfcWorker::run_once() {
  service_.poll();
  const auto current = service_.snapshot();
  std::optional<domain::WeightReading> weight;
  if (!current.tag) {
    tag_generation_ = 0;
    measurement_requested_ = false;
  } else {
    if (tag_generation_ != current.tag->generation) {
      tag_generation_ = current.tag->generation;
      measurement_requested_ = false;
    }
    if (!handoff_.submitted() || !measurement_requested_) {
      const auto scale = diagnostics_.scale_snapshot();
      const auto now = millis();
      if (!measurement_requested_ && scale.scale_calibrated &&
          scale.scale_adc_ready && !diagnostics_.scale_measurement_active() &&
          scale_.pending() == 0U) {
        const auto receipt = scale_.submit_weigh(now);
        if (receipt.accepted) {
          measurement_requested_ = true;
          measurement_requested_at_ = now;
        }
      }
      const bool fresh =
          measurement_requested_ && scale.scale_last_completed_available &&
          static_cast<std::int32_t>(scale.scale_last_completed_at_ms -
                                    measurement_requested_at_) >= 0 &&
          static_cast<std::uint32_t>(now - scale.scale_last_completed_at_ms) <
              5000U &&
          scale.scale_measurement_purpose ==
              services::ScaleMeasurementPurpose::weigh &&
          !scale.scale_overload && scale.scale_adc_ready && scale.scale_calibrated;
      if (fresh)
        weight = domain::WeightReading{
            scale.scale_last_completed_milligrams / 1000.0F, true};
      // A failed/timed-out measurement remains waiting; another explicit Weigh
      // may satisfy it. No autonomous retry loop is queued on the scale owner.
    }
  }
  handoff_.observe(current, weight);
  const auto now = millis();
  if (static_cast<std::uint32_t>(now - last_log_) >= 5000U) {
    last_log_ = now;
    Serial.printf(
        "NFC state=%s generation=%llu stack_free=%lu bytes bus_errors=%lu\n",
        nfc::to_string(current.state),
        static_cast<unsigned long long>(current.generation),
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)),
        static_cast<unsigned long>(current.bus_errors));
  }
}
}  // namespace opentag::application

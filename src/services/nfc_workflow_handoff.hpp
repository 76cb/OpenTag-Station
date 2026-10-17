#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

#include "nfc/read_only_service.hpp"
#include "services/station_workflow.hpp"

namespace opentag::services {
// Runs on the NFC owner. The callback queues backend work and never resolves
// over the network here. Its generation fences queued and in-flight responses.
class NfcWorkflowHandoff {
 public:
  using Submit = std::function<bool(const nfc::IdentifiedTag&,
                                    domain::WeightReading, std::uint64_t)>;
  NfcWorkflowHandoff(StationWorkflow& workflow, Submit submit)
      : workflow_(workflow), submit_(std::move(submit)) {}
  // Minimum spacing between lookups of the same tag after a failure. A retry
  // only starts once the backend probe reports Spoolman reachable again, and
  // only for failures where Spoolman could not be reached. The spacing
  // triples after each retry and a placement gets at most max_retries.
  static constexpr std::uint32_t retry_interval_ms = 10000U;
  static constexpr std::uint8_t max_retries = 3U;

  void observe(const nfc::ReadSnapshot& s,
               std::optional<domain::WeightReading> weight,
               std::uint32_t now_ms = 0U) {
    // A tag without OpenPrintTag data is looked up by UID alone; the
    // workflow keeps it hidden unless Spoolman links it to a spool.
    const auto uid_only = nfc::uid_only_generation(s);
    if (!s.tag && uid_only == 0U) {
      if (tag_generation_ != 0) workflow_.clear();
      tag_generation_ = 0;
      submitted_ = false;
      weight_attached_ = false;
      retries_ = 0U;
      return;
    }
    nfc::IdentifiedTag by_uid;
    if (!s.tag) {
      by_uid.uid = *s.uid;
      by_uid.generation = uid_only;
    }
    const auto& tag = s.tag ? *s.tag : by_uid;
    if (tag_generation_ != tag.generation) {
      tag_generation_ = tag.generation;
      workflow_generation_ = workflow_.begin_identified_spool(
          tag.decoded.material, tag.uid, !s.tag);
      submitted_ = false;
      weight_attached_ = false;
      retries_ = 0U;
    }
    const bool usable = weight && weight->stable &&
                        std::isfinite(weight->gross_grams);
    // The lookup is queued as soon as the tag is identified. A weight is
    // optional: one already measured travels with it, a later one is
    // attached for reconciliation without another Spoolman request.
    if (!submitted_) {
      const auto reading =
          usable ? *weight : domain::WeightReading{NAN, false};
      submitted_ = submit_(tag, reading, workflow_generation_);
      if (submitted_) {
        last_submit_ms_ = now_ms;
        weight_attached_ = usable;
      }
      return;
    }
    if (usable && !weight_attached_) {
      workflow_.attach_physical_weight(workflow_generation_, *weight);
      weight_attached_ = true;
    }
    // A lookup that failed while Spoolman was unreachable is repeated once
    // Spoolman is back. begin_lookup_retry() moves the stage off
    // spool_resolution_unavailable, so at most one lookup is in flight.
    std::uint32_t spacing = retry_interval_ms;
    for (std::uint8_t i = 0U; i < retries_; ++i) spacing *= 3U;
    if (retries_ >= max_retries ||
        static_cast<std::uint32_t>(now_ms - last_submit_ms_) < spacing ||
        !workflow_.begin_lookup_retry(workflow_generation_))
      return;
    last_submit_ms_ = now_ms;
    ++retries_;
    if (!submit_(tag, domain::WeightReading{NAN, false}, workflow_generation_))
      workflow_.abort_lookup_retry(workflow_generation_);
  }
  bool submitted() const { return submitted_; }
  // True once a stable weight reached the workflow for the current tag.
  bool weight_attached() const { return weight_attached_; }

 private:
  StationWorkflow& workflow_;
  Submit submit_;
  std::uint64_t tag_generation_{0}, workflow_generation_{0};
  std::uint32_t last_submit_ms_{0};
  bool submitted_{false};
  bool weight_attached_{false};
  std::uint8_t retries_{0U};
};
}  // namespace opentag::services

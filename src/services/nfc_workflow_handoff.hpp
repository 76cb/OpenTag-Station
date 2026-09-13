#pragma once
#include <functional>

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
  void observe(const nfc::ReadSnapshot& s,
               std::optional<domain::WeightReading> weight) {
    if (!s.tag) {
      if (tag_generation_ != 0) workflow_.clear();
      tag_generation_ = 0;
      submitted_ = false;
      return;
    }
    if (tag_generation_ != s.tag->generation) {
      tag_generation_ = s.tag->generation;
      workflow_generation_ =
          workflow_.begin_identified_spool(s.tag->decoded.material, s.tag->uid);
      submitted_ = false;
    }
    if (!submitted_ && weight && weight->stable &&
        std::isfinite(weight->gross_grams))
      submitted_ = submit_(*s.tag, *weight, workflow_generation_);
  }
  bool submitted() const { return submitted_; }

 private:
  StationWorkflow& workflow_;
  Submit submit_;
  std::uint64_t tag_generation_{0}, workflow_generation_{0};
  bool submitted_{false};
};
}  // namespace opentag::services

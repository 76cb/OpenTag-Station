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
    // A tag without OpenPrintTag data is looked up by UID alone; the
    // workflow keeps it hidden unless Spoolman links it to a spool.
    const auto uid_only = nfc::uid_only_generation(s);
    if (!s.tag && uid_only == 0U) {
      if (tag_generation_ != 0) workflow_.clear();
      tag_generation_ = 0;
      submitted_ = false;
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
    }
    if (!submitted_ && weight && weight->stable &&
        std::isfinite(weight->gross_grams))
      submitted_ = submit_(tag, *weight, workflow_generation_);
  }
  bool submitted() const { return submitted_; }

 private:
  StationWorkflow& workflow_;
  Submit submit_;
  std::uint64_t tag_generation_{0}, workflow_generation_{0};
  bool submitted_{false};
};
}  // namespace opentag::services

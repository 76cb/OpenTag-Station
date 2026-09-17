#pragma once
#include "domain/weight.hpp"
#include "integrations/inventory.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "services/weight_reconciler.hpp"
#include <functional>
#include <mutex>

namespace opentag::services {
struct WeighSyncSnapshot {
  std::uint64_t measurement_id{0}, generation{0}, settings_revision{0};
  domain::SpoolId spool_id{0};
  nfc::nfcv::Uid uid;
  std::string name, phase{"idle"},
      message{"Press Weigh to capture a measurement"};
  bool automatic{false}, consumed{false};
  float expected_used{0};
  ReconciliationTolerances tolerances;
  std::optional<float> gross, tare, measured, canonical_remaining, difference;
};
// Explicit measurement sessions only. The backend owns HTTP; scale callbacks
// only publish bounded state. A consumed session can never mutate again.
class WeighSync final {
public:
  bool begin(WeighSyncSnapshot snapshot);
  void capture(std::uint64_t id, domain::WeightReading reading);
  void fail(std::uint64_t id, const std::string &message);
  WeighSyncSnapshot snapshot() const;
  core::Result<void>
  update(std::uint64_t id, integrations::ISpoolInventory &inventory,
         const std::function<bool(const WeighSyncSnapshot &)> &fence);

private:
  mutable std::mutex mutex_;
  WeighSyncSnapshot state_;
};
} // namespace opentag::services

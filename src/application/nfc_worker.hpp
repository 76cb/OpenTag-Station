#pragma once
#include "application/backend_worker.hpp"
#include "application/scale_command_queue.hpp"
#include "diagnostics/system_diagnostics.hpp"
#include "hardware/nfc/st25r3916b/i2c_reader.hpp"
#include "services/nfc_workflow_handoff.hpp"
#include "nfc/worker_startup.hpp"

namespace opentag::application {
class NfcWorker {
 public:
  NfcWorker(diagnostics::SystemDiagnostics& diagnostics,
            ScaleCommandQueue& scale, services::StationWorkflow& workflow,
            BackendWorker& backend)
      : service_(reader_),
        diagnostics_(diagnostics),
        scale_(scale),
        handoff_(workflow, [&backend](const nfc::IdentifiedTag& tag,
                                      domain::WeightReading weight,
                                      std::uint64_t generation) {
          return backend.submit_identified_spool(tag.decoded.material, tag.uid,
                                                 weight, {}, generation);
        }) {}
  void enable_when_configured(bool configuration_ready, bool configured,
                             bool connected, bool provisioning, bool grace);
  nfc::ReadSnapshot snapshot() const {
    auto result = service_.snapshot();
    startup_.describe(result);
    return result;
  }
 private:
  // Logical owner only: BackendWorker is the sole caller; no RTOS task/stack.
  friend class BackendWorker;
  void poll();
  nfc::WorkerStartup startup_;
  void run_once();
  hardware::nfc::st25r3916b::I2cReader reader_;
  nfc::ReadOnlyService service_;
  diagnostics::SystemDiagnostics& diagnostics_;
  ScaleCommandQueue& scale_;
  services::NfcWorkflowHandoff handoff_;
  std::uint64_t tag_generation_{0};
  bool measurement_requested_{false};
  std::uint32_t measurement_requested_at_{0};
  std::uint32_t last_log_{0};
};
}  // namespace opentag::application

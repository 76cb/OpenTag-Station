#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application/backend_worker.hpp"
#include "application/scale_command_queue.hpp"
#include "diagnostics/system_diagnostics.hpp"
#include "hardware/nfc/st25r3916b/i2c_reader.hpp"
#include "services/nfc_workflow_handoff.hpp"

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
  bool start();
  nfc::ReadSnapshot snapshot() const { return service_.snapshot(); }
  static constexpr std::uint32_t stack_bytes = 16384U;
  static constexpr std::uint32_t safety_bytes = 4096U;

 private:
  static void task_entry(void* context);
  void run_once();
  hardware::nfc::st25r3916b::I2cReader reader_;
  nfc::ReadOnlyService service_;
  diagnostics::SystemDiagnostics& diagnostics_;
  ScaleCommandQueue& scale_;
  services::NfcWorkflowHandoff handoff_;
  TaskHandle_t task_{nullptr};
  std::uint64_t tag_generation_{0};
  bool measurement_requested_{false};
  std::uint32_t measurement_requested_at_{0};
  std::uint32_t last_log_{0};
};
}  // namespace opentag::application

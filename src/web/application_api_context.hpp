#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "application/backend_worker.hpp"
#include "application/configuration_worker.hpp"
#include "application/device_control_worker.hpp"
#include "application/ota_worker.hpp"
#include "application/operation_registry.hpp"
#include "application/scale_command_queue.hpp"
#include "config/configuration_service.hpp"
#include "diagnostics/system_diagnostics.hpp"
#include "logging/bounded_log.hpp"
#include "services/station_workflow.hpp"
#include "web/api_router.hpp"
#include "web/idempotency_ledger.hpp"

namespace opentag::web {

struct StreamingUploadRequest {
  std::string idempotency_key;
  std::uint64_t expected_generation{0U};
  std::uint32_t expected_length{0U};
  opentag::ota::Sha256Digest expected_sha256{};
};

struct StreamingUploadSession {
  opentag::ota::OperationPrecondition precondition;
  std::uint64_t operation_id{0U};
  bool duplicate{false};
  std::optional<opentag::ota::UpdateSnapshot> replay_snapshot;
};

class ApplicationApiContext final : public api::IApiContext {
 public:
  ApplicationApiContext(
      diagnostics::SystemDiagnostics& diagnostics,
      config::ConfigurationService& configuration,
      application::ConfigurationWorker& configuration_worker,
      application::BackendWorker& backend_worker,
      application::ScaleCommandQueue& scale_commands,
      services::StationWorkflow& workflow,
      application::OperationRegistry& operations,
      logging::BoundedLog& logs,
      application::DeviceControlWorker& device_control,
      application::OtaWorker& ota_worker)
      : diagnostics_(diagnostics),
        configuration_(configuration),
        configuration_worker_(configuration_worker),
        backend_worker_(backend_worker),
        scale_commands_(scale_commands),
        workflow_(workflow),
        operations_(operations),
        logs_(logs),
        device_control_(device_control),
        ota_worker_(ota_worker) {}

  [[nodiscard]] bool authorize_mutation(
      std::string_view bearer_token) override;
  [[nodiscard]] core::Result<std::string> snapshot_json(
      api::Resource resource) override;
  [[nodiscard]] core::Result<std::optional<std::string>>
  operation_status_json(std::uint64_t operation_id) override;
  [[nodiscard]] core::Result<api::OperationReceipt> submit(
      const api::Mutation& mutation) override;

  // The HTTP owner validates the binary transport envelope before calling
  // these methods. This context owns the idempotency transaction, operation
  // record, and every call into the sole OTA owner task.
  [[nodiscard]] core::Result<StreamingUploadSession> begin_streaming_upload(
      const StreamingUploadRequest& request);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot>
  write_streaming_upload(
      const StreamingUploadSession& session,
      core::ByteView chunk);
  [[nodiscard]] core::Result<opentag::ota::UpdateSnapshot>
  finish_streaming_upload(const StreamingUploadSession& session);
  void abort_streaming_upload(
      const StreamingUploadSession& session,
      const core::Error& reason);

  [[nodiscard]] std::uint64_t update_revision() const {
    return ota_worker_.snapshot().revision;
  }

 private:
  [[nodiscard]] core::Result<api::OperationReceipt> submit_fresh(
      const api::Mutation& mutation,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<api::OperationReceipt> receipt_result(
      application::CommandReceipt receipt,
      const char* unavailable_message);

  diagnostics::SystemDiagnostics& diagnostics_;
  config::ConfigurationService& configuration_;
  application::ConfigurationWorker& configuration_worker_;
  application::BackendWorker& backend_worker_;
  application::ScaleCommandQueue& scale_commands_;
  services::StationWorkflow& workflow_;
  application::OperationRegistry& operations_;
  logging::BoundedLog& logs_;
  application::DeviceControlWorker& device_control_;
  application::OtaWorker& ota_worker_;

  std::mutex idempotency_mutex_;
  IdempotencyLedger idempotency_;
};

}  // namespace opentag::web

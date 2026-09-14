#include "application/backend_worker.hpp"
#include "application/nfc_worker.hpp"
#include "platform/storage/writer_journal.hpp"
#include <Arduino.h>
#include <esp_system.h>
namespace opentag::application {
CommandReceipt BackendWorker::submit_writer(std::string_view payload) {
  if (payload.empty() || payload.size() > 4096)
    return {false, 0};
  auto *command = new (std::nothrow) Command;
  if (!command)
    return {false, 0};
  if (!command->writer_payload.append(payload.data(), payload.size())) {
    delete command;
    return {false, 0};
  }
  command->type = CommandType::writer;
  command->enqueued_at_ms = millis();
  command->operation_id = operations_.begin(OperationKind::tag_writer, millis(),
                                            "Writer operation queued");
  const auto id = command->operation_id;
  if (!id) {
    delete command;
    return {false, 0};
  }
  if (!enqueue(command)) {
    operations_.fail(id, millis(),
                     {core::ErrorCategory::backend_unavailable,
                      "Writer queue unavailable", true});
    return {false, id};
  }
  return {true, id};
}
network::ResponseBody BackendWorker::writer_snapshot() const {
  std::lock_guard<std::mutex> lock(writer_mutex_);
  network::ResponseBody body(24576);
  if (writer_view_.empty())
    body.append("{\"phase\":\"idle\"}", 16);
  else
    body.append(writer_view_.data(), writer_view_.size());
  return body;
}
void BackendWorker::process_writer(Command &command) {
  (void)apply_backend_settings_if_changed();
  if (!nfc_ || !nfc_->snapshot().initialized) {
    operations_.fail(command.operation_id, millis(),
                     {core::ErrorCategory::nfc_communication,
                      "NFC is not initialized", true});
    return;
  }
  if (!writer_)
    writer_ = network::make_external<services::TagWriterService>([&] {
      return services::TagWriterService(
          spoolman_, nfc_->reader_,
          [this] { return nfc_->service_.snapshot().generation; },
          [](std::uint8_t *bytes, std::size_t size) {
            esp_fill_random(bytes, size);
          },
          [this](const network::ResponseBody &view) {
            // Physical reads/writes have their own deadline. Start the bounded
            // network budget only when canonical loading/association begins.
            if (std::string_view(view).find("\"phase\":\"associating\"") !=
                    std::string_view::npos ||
                std::string_view(view).find("\"phase\":\"loading_spool\"") !=
                    std::string_view::npos) {
              transport_.end_operation();
              transport_.begin_operation(millis());
            }
            std::lock_guard<std::mutex> lock(writer_mutex_);
            writer_view_.release();
            writer_view_.append(view.data(), view.size());
          },
          &platform::storage::writer_journal());
    });
  if (!writer_) {
    operations_.fail(command.operation_id, millis(),
                     {core::ErrorCategory::backend_unavailable,
                      "Writer PSRAM unavailable", true});
    return;
  }
  auto parsed =
      network::parse_backend_json(command.writer_payload, "Writer command");
  if (!parsed.ok()) {
    operations_.fail(command.operation_id, millis(), parsed.error());
    return;
  }
  const std::string action = parsed.value()["action"] | "";
  if (action == "write" &&
      static_cast<std::uint32_t>(millis() - command.enqueued_at_ms) >
          destructive_command_expiry_ms) {
    operations_.fail(command.operation_id, millis(),
                     {core::ErrorCategory::conflict,
                      "Write confirmation expired in queue", false});
    return;
  }
  operations_.mark_running(command.operation_id, millis(),
                           "Writer operation running; see Tags progress");
  const auto result = writer_->process(parsed.value().as<JsonObjectConst>());
  if (action == "write" || (action == "retry_association" && result.ok()))
    nfc_->service_.invalidate_after_write();
  if (result.ok())
    operations_.succeed(command.operation_id, millis(),
                        "Writer operation complete; see Tags result");
  else
    operations_.fail(command.operation_id, millis(), result.error());
}
} // namespace opentag::application

#include "diagnostics/system_diagnostics.hpp"

#include <Arduino.h>
#include <esp_system.h>

#include <utility>

#include "hardware/display/wt32_display.hpp"
#include "platform/storage/storage_service.hpp"

namespace opentag::diagnostics {

const char* SystemDiagnostics::current_reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_UNKNOWN:
    default: return "unknown";
  }
}

SystemSnapshot SystemDiagnostics::snapshot(std::uint32_t now_ms) const {
  const auto& storage = storage_.status();
  SystemSnapshot result;
  result.reset_reason = current_reset_reason();
  result.uptime_ms = now_ms;
  result.free_heap_bytes = ESP.getFreeHeap();
  result.minimum_free_heap_bytes = ESP.getMinFreeHeap();
  result.psram_total_bytes = ESP.getPsramSize();
  result.psram_free_bytes = ESP.getFreePsram();
  result.boot_count = storage.boot_count;
  result.crash_streak = storage.crash_streak;
  result.display_ready = display_.initialized();
  result.touch_configured = display_.touch_configured();
  result.nvs_ready = storage.nvs_ready;
  result.filesystem_ready = storage.filesystem_ready;
  result.coredump_partition_present = storage.coredump_partition_present;
  result.ui_task_running = ui_task_running_.load(std::memory_order_relaxed);
  static_cast<ScaleDiagnosticSnapshot&>(result) = scale_diagnostics_.snapshot();
  result.network_task_running =
      network_task_running_.load(std::memory_order_relaxed);
  {
    const std::lock_guard<std::mutex> lock(network_mutex_);
    result.wifi_state = network_status_.state;
    result.wifi_configured = network_status_.configured;
    result.wifi_connected = network_status_.connected;
    result.wifi_scan_running = network_status_.scan_running;
    result.mdns_ready = network_status_.mdns_ready;
    result.ntp_ready = network_status_.ntp_ready;
    result.wifi_ssid = network_status_.ssid;
    result.wifi_rssi_dbm = network_status_.rssi_dbm;
    result.ip_address = network_status_.ip_address;
    result.gateway = network_status_.gateway;
    result.dns_server = network_status_.dns_server;
    result.wifi_reconnect_attempts = network_status_.reconnect_attempts;
  }
  return result;
}

void SystemDiagnostics::set_scale_status(
    const services::ScaleStatus& status,
    const std::optional<services::ScaleCalibration>& calibration,
    const services::ScaleHardwareSettings& hardware) {
  scale_diagnostics_.update(status, calibration, hardware);
}

void SystemDiagnostics::set_network_status(const network::WifiStatus& status) {
  const std::lock_guard<std::mutex> lock(network_mutex_);
  network_status_ = status;
}

}  // namespace opentag::diagnostics

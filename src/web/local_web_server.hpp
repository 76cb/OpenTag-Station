#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "web/application_api_context.hpp"
#include "web/api_router.hpp"

namespace opentag::web {

// ESP-IDF HTTP/WebSocket transport for the local appliance interface. The
// object, Router, and Router context must outlive the running server. start(),
// stop(), and publish() are intended to be called by the network owner.
class LocalWebServer final {
 public:
  static constexpr std::size_t maximum_open_sockets = 5U;
  static constexpr std::size_t maximum_websocket_clients = 2U;
  // Physical ordinary HTTPD use was 1,088 bytes. The compiler reports the
  // nested upload handler/begin frames at 3,232 + 5,024 bytes, so the bounded
  // worst-route budget is 9,344 bytes and the 12 KiB reservation leaves 2,944.
  static constexpr std::uint32_t http_task_stack_bytes = 12288U;
  static constexpr std::uint32_t http_worst_case_stack_use_bytes = 9344U;
  static constexpr std::uint32_t minimum_http_stack_safety_bytes = 2048U;
  static constexpr std::size_t maximum_websocket_message_bytes = 4096U;
  static constexpr std::uint32_t scale_publish_interval_ms = 500U;
  static constexpr std::uint32_t update_publish_interval_ms = 500U;
  static constexpr std::uint32_t heartbeat_interval_ms = 15000U;
  static constexpr std::uint32_t publish_check_interval_ms = 500U;

  static_assert(
      http_task_stack_bytes - http_worst_case_stack_use_bytes >=
      minimum_http_stack_safety_bytes);

  LocalWebServer(
      api::Router& router,
      ApplicationApiContext& api_context) noexcept;
  ~LocalWebServer();

  LocalWebServer(const LocalWebServer&) = delete;
  LocalWebServer& operator=(const LocalWebServer&) = delete;
  LocalWebServer(LocalWebServer&&) = delete;
  LocalWebServer& operator=(LocalWebServer&&) = delete;

  [[nodiscard]] esp_err_t start();
  [[nodiscard]] esp_err_t stop();
  [[nodiscard]] bool running() const noexcept { return server_ != nullptr; }

  // Publishes only when live clients exist. Scale state is bounded to 2 Hz;
  // heartbeat state is bounded to approximately every 15 seconds. Network
  // ownership never waits for a client socket send.
  void publish(std::uint32_t now_ms);

 private:
  struct WebsocketClientSlot {
    std::atomic_int socket{-1};
    std::atomic<diagnostics::WebsocketDisconnectReason> pending_reason{
        diagnostics::WebsocketDisconnectReason::none};
  };

  struct WebsocketSendBatch {
    LocalWebServer* owner{nullptr};
    httpd_handle_t server{nullptr};
    std::atomic_bool busy{false};
    std::atomic_size_t remaining{0U};
    std::array<std::uint8_t, maximum_websocket_message_bytes> payload{};
    std::array<httpd_ws_frame_t, maximum_websocket_clients> frames{};
    std::array<int, maximum_websocket_clients> sockets{};
  };

  static esp_err_t session_open_handler(httpd_handle_t server, int socket);
  static void session_close_handler(httpd_handle_t server, int socket);
  static void preserve_global_context(void* context);
  static esp_err_t static_asset_handler(httpd_req_t* request);
  static esp_err_t update_upload_handler(httpd_req_t* request);
  static esp_err_t api_handler(httpd_req_t* request);
  static esp_err_t websocket_handler(httpd_req_t* request);
  static void websocket_send_complete(
      esp_err_t error,
      int socket,
      void* context);

  esp_err_t handle_static_asset(httpd_req_t* request);
  esp_err_t handle_update_upload(httpd_req_t* request);
  esp_err_t handle_api(httpd_req_t* request);
  esp_err_t handle_websocket(httpd_req_t* request);
  void handle_session_close(int socket);
  [[nodiscard]] bool track_websocket_client(int socket);
  [[nodiscard]] bool release_websocket_client(
      int socket,
      diagnostics::WebsocketDisconnectReason& reason);
  void mark_websocket_disconnect(
      int socket, diagnostics::WebsocketDisconnectReason reason);

  [[nodiscard]] std::string make_scale_event();
  [[nodiscard]] std::string make_update_event(std::uint64_t& revision);
  [[nodiscard]] std::size_t send_to_websocket_clients(
      const std::string& message);
  [[nodiscard]] std::size_t websocket_client_count(
      int excluded_fd = -1) const;

  api::Router& router_;
  ApplicationApiContext& api_context_;
  httpd_handle_t server_{nullptr};
  std::atomic_bool stopping_{false};
  std::array<WebsocketClientSlot, maximum_websocket_clients>
      websocket_clients_{};
  WebsocketSendBatch websocket_send_{};
  std::array<std::uint8_t, opentag::ota::maximum_upload_chunk_bytes>
      upload_buffer_{};
  std::uint32_t last_scale_publish_ms_{0U};
  std::uint32_t last_update_publish_ms_{0U};
  std::uint32_t last_heartbeat_ms_{0U};
  std::uint32_t next_publish_check_ms_{0U};
  std::uint64_t last_update_revision_{0U};
  std::array<std::uint64_t, 3> last_nfc_revision_{};
  bool scale_published_{false};
  bool scale_session_was_active_{false};
  bool heartbeat_published_{false};
  bool update_published_{false};
};

}  // namespace opentag::web

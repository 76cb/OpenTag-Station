#include "web/local_web_server.hpp"

#include <ArduinoJson.h>
#include <esp_timer.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "network/network_policy.hpp"
#include "web/web_assets.hpp"

#if !defined(CONFIG_HTTPD_WS_SUPPORT) || CONFIG_HTTPD_WS_SUPPORT != 1
#error "LocalWebServer requires ESP-IDF HTTP server WebSocket support"
#endif

namespace opentag::web {
namespace {

constexpr std::size_t maximum_collected_header_value_bytes = 512U;
constexpr std::uint32_t maximum_request_receive_ms = 5000U;
constexpr std::uint32_t maximum_upload_receive_ms = 180000U;
constexpr char invalid_scale_event[] =
    R"({"type":"invalidate","data":{"resource":"scale"}})";
constexpr char invalid_update_event[] =
    R"({"type":"invalidate","data":{"resource":"update"}})";

using UriHandler = esp_err_t (*)(httpd_req_t* request);

httpd_uri_t make_uri(
    const char* uri,
    httpd_method_t method,
    UriHandler handler,
    void* context,
    bool websocket = false) {
  httpd_uri_t route{};
  route.uri = uri;
  route.method = method;
  route.handler = handler;
  route.user_ctx = context;
  route.is_websocket = websocket;
  route.handle_ws_control_frames = false;
  route.supported_subprotocol = nullptr;
  return route;
}

const char* status_line(std::int32_t status) {
  switch (status) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 408: return "408 Request Timeout";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 415: return "415 Unsupported Media Type";
    case 422: return "422 Unprocessable Entity";
    case 429: return "429 Too Many Requests";
    case 500: return "500 Internal Server Error";
    case 501: return "501 Not Implemented";
    case 502: return "502 Bad Gateway";
    case 503: return "503 Service Unavailable";
    case 507: return "507 Insufficient Storage";
    default: return "500 Internal Server Error";
  }
}

esp_err_t set_security_headers(httpd_req_t* request) {
  static constexpr std::array<std::pair<const char*, const char*>, 5U> headers = {{
      {"Content-Security-Policy",
       "default-src 'self'; script-src 'self'; style-src 'self'; "
       "img-src 'self' data:; connect-src 'self' ws: wss:; object-src "
       "'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"},
      {"X-Content-Type-Options", "nosniff"},
      {"Referrer-Policy", "no-referrer"},
      {"X-Frame-Options", "DENY"},
      {"Cache-Control", "no-store"},
  }};
  for (const auto& header : headers) {
    const auto result =
        httpd_resp_set_hdr(request, header.first, header.second);
    if (result != ESP_OK) return result;
  }
  return ESP_OK;
}

esp_err_t send_json_error(
    httpd_req_t* request,
    std::int32_t status,
    const char* code,
    const char* message,
    bool retryable = false) {
  std::string body =
      R"({"api_version":"v1","ok":false,"error":{"code":")";
  body += code;
  body += R"(","message":")";
  body += message;
  body += R"(","retryable":)";
  body += retryable ? "true" : "false";
  body += "}}";
  auto result = httpd_resp_set_status(request, status_line(status));
  if (result != ESP_OK) return result;
  result = httpd_resp_set_type(request, "application/json; charset=utf-8");
  if (result != ESP_OK) return result;
  result = set_security_headers(request);
  if (result != ESP_OK) return result;
  return httpd_resp_send(
      request, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t finish_response_without_purging(
    httpd_req_t* request,
    std::size_t consumed_body_bytes,
    esp_err_t response_result) {
  if (request != nullptr && consumed_body_bytes < request->content_len) {
    return ESP_FAIL;
  }
  return response_result;
}

esp_err_t send_router_response(
    httpd_req_t* request,
    const api::Response& response) {
  auto result = httpd_resp_set_status(request, status_line(response.status));
  if (result != ESP_OK) return result;
  for (const auto& header : response.headers) {
    if (header.name == "Content-Type") {
      result = httpd_resp_set_type(request, header.value.c_str());
    } else if (header.name != "Cache-Control" &&
               header.name != "X-Content-Type-Options") {
      result = httpd_resp_set_hdr(
          request, header.name.c_str(), header.value.c_str());
    }
    if (result != ESP_OK) return result;
  }
  result = set_security_headers(request);
  if (result != ESP_OK) return result;
  return httpd_resp_send(
      request,
      response.body.data(),
      static_cast<ssize_t>(response.body.size()));
}

esp_err_t collect_header(
    httpd_req_t* request,
    const char* name,
    api::Request& api_request) {
  const auto length = httpd_req_get_hdr_value_len(request, name);
  if (length == 0U) return ESP_OK;
  if (length > maximum_collected_header_value_bytes) {
    return ESP_ERR_INVALID_SIZE;
  }
  std::array<char, maximum_collected_header_value_bytes + 1U> value{};
  const auto result = httpd_req_get_hdr_value_str(
      request, name, value.data(), length + 1U);
  if (result != ESP_OK) return result;
  api_request.headers.push_back({name, std::string(value.data(), length)});
  return ESP_OK;
}

bool read_required_header(
    httpd_req_t* request,
    const char* name,
    std::string& value,
    std::size_t& collected_bytes) {
  const auto length = httpd_req_get_hdr_value_len(request, name);
  if (length == 0U || length > maximum_collected_header_value_bytes) {
    return false;
  }
  if (collected_bytes > api::maximum_request_header_bytes - length ||
      collected_bytes + length >
          api::maximum_request_header_bytes - std::strlen(name) - 4U) {
    return false;
  }
  std::array<char, maximum_collected_header_value_bytes + 1U> buffer{};
  if (httpd_req_get_hdr_value_str(
          request, name, buffer.data(), length + 1U) != ESP_OK) {
    return false;
  }
  value.assign(buffer.data(), length);
  collected_bytes += std::strlen(name) + length + 4U;
  return true;
}

bool valid_idempotency_key(std::string_view value) {
  return !value.empty() &&
      value.size() <= IdempotencyLedger::maximum_key_bytes &&
      std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' ||
            character == '_' || character == '.' || character == ':';
      });
}

bool decode_sha256(
    std::string_view encoded,
    opentag::ota::Sha256Digest& digest) {
  if (!api::valid_sha256_hex(encoded)) return false;
  const auto nibble = [](char value) -> std::uint8_t {
    return value <= '9'
        ? static_cast<std::uint8_t>(value - '0')
        : static_cast<std::uint8_t>(value - 'a' + 10);
  };
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    digest[index] = static_cast<std::uint8_t>(
        (nibble(encoded[index * 2U]) << 4U) |
        nibble(encoded[index * 2U + 1U]));
  }
  return true;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

api::Response upload_receipt(
    const StreamingUploadSession& session,
    const opentag::ota::UpdateSnapshot* update = nullptr) {
  JsonDocument document;
  document["api_version"] = api::version;
  document["ok"] = true;
  auto data = document["data"].to<JsonObject>();
  data["operation_id"] = session.operation_id;
  data["upload_operation_id"] = session.operation_id;
  data["kind"] = "firmware_upload";
  data["state"] = session.duplicate ? "duplicate" : "succeeded";
  if (update != nullptr) {
    data["update_revision"] = update->revision;
    data["generation"] = update->generation;
    data["update_state"] = opentag::ota::to_string(update->state);
    data["validated"] = update->validation_passed;
    data["activated"] = update->activated;
  }
  std::string body;
  serializeJson(document, body);
  return {
      session.duplicate ? 202 : 200,
      {{"Content-Type", "application/json; charset=utf-8"}},
      std::move(body),
  };
}

api::Method api_method(int method) {
  switch (method) {
    case HTTP_GET: return api::Method::get;
    case HTTP_POST: return api::Method::post;
    case HTTP_PATCH: return api::Method::patch;
    case HTTP_PUT: return api::Method::put;
    case HTTP_DELETE: return api::Method::delete_method;
    case HTTP_HEAD: return api::Method::head;
    case HTTP_OPTIONS: return api::Method::options;
    default: return api::Method::unknown;
  }
}

bool due(
    std::uint32_t now_ms,
    std::uint32_t previous_ms,
    std::uint32_t interval_ms,
    bool previously_published) {
  return !previously_published ||
      static_cast<std::uint32_t>(now_ms - previous_ms) >= interval_ms;
}

bool provisioning_peer(
    httpd_req_t* request,
    ApplicationApiContext& context) {
  if (request == nullptr || !context.authorize_provisioning()) return false;
  const auto socket = httpd_req_to_sockfd(request);
  sockaddr_storage peer{};
  socklen_t peer_length = sizeof(peer);
  sockaddr_storage local{};
  socklen_t local_length = sizeof(local);
  if (socket < 0 ||
      getpeername(
          socket, reinterpret_cast<sockaddr*>(&peer), &peer_length) != 0 ||
      getsockname(
          socket, reinterpret_cast<sockaddr*>(&local), &local_length) != 0 ||
      peer.ss_family != AF_INET || local.ss_family != AF_INET) {
    return false;
  }
  const auto& peer_ipv4 = reinterpret_cast<const sockaddr_in&>(peer);
  const auto& local_ipv4 = reinterpret_cast<const sockaddr_in&>(local);
  return network::is_setup_ap_transport(
      ntohl(peer_ipv4.sin_addr.s_addr),
      ntohl(local_ipv4.sin_addr.s_addr));
}

}  // namespace

LocalWebServer::LocalWebServer(
    api::Router& router,
    ApplicationApiContext& api_context) noexcept
    : router_(router), api_context_(api_context) {}

LocalWebServer::~LocalWebServer() {
  if (server_ != nullptr) (void)stop();
}

esp_err_t LocalWebServer::start() {
  if (server_ != nullptr) return ESP_OK;
  stopping_.store(false, std::memory_order_release);

  httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
  configuration.stack_size = http_task_stack_bytes;
  configuration.max_open_sockets =
      static_cast<std::uint16_t>(maximum_open_sockets);
  configuration.max_uri_handlers = 13U;
  configuration.max_resp_headers = 10U;
  // Match the pinned ESP-IDF server's defensible LAN defaults. Seven client
  // slots cover one live WebSocket, two scheduled REST requests, one critical
  // operation/upload connection, one transient browser connection, and two
  // maintenance slots. LRU purge must stay off: useful WebSockets and mutation
  // receipts are never sacrificial capacity under ordinary single-tab load.
  configuration.backlog_conn = 5U;
  configuration.lru_purge_enable = false;
  configuration.recv_wait_timeout = 5U;
  configuration.send_wait_timeout = 5U;
  configuration.global_user_ctx = this;
  // The server does not own this application-lifetime object.
  configuration.global_user_ctx_free_fn =
      &LocalWebServer::preserve_global_context;
  configuration.open_fn = &LocalWebServer::session_open_handler;
  configuration.close_fn = &LocalWebServer::session_close_handler;
  configuration.uri_match_fn = httpd_uri_match_wildcard;

  auto result = httpd_start(&server_, &configuration);
  if (result != ESP_OK) {
    server_ = nullptr;
    api_context_.transport_diagnostics().set_http_server_running(false);
    return result;
  }

  const std::array<httpd_uri_t, 13U> handlers = {{
      make_uri("/", HTTP_GET, &LocalWebServer::static_asset_handler, this),
      make_uri(
          "/assets/app.css",
          HTTP_GET,
          &LocalWebServer::static_asset_handler,
          this),
      make_uri(
          "/assets/app.js",
          HTTP_GET,
          &LocalWebServer::static_asset_handler,
          this),
      make_uri(
          "/api/v1/events",
          HTTP_GET,
          &LocalWebServer::websocket_handler,
          this,
          true),
      make_uri(
          "/api/v1/update/upload",
          HTTP_POST,
          &LocalWebServer::update_upload_handler,
          this),
      make_uri("/api/*", HTTP_GET, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_POST, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_PATCH, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_PUT, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_DELETE, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_HEAD, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_OPTIONS, &LocalWebServer::api_handler, this),
      make_uri("/*", HTTP_GET, &LocalWebServer::static_asset_handler, this),
  }};
  for (const auto& handler : handlers) {
    result = httpd_register_uri_handler(server_, &handler);
    if (result != ESP_OK) {
      (void)httpd_stop(server_);
      server_ = nullptr;
      api_context_.transport_diagnostics().set_http_server_running(false);
      return result;
    }
  }

  api_context_.transport_diagnostics().set_http_server_running(true);
  scale_published_ = false;
  heartbeat_published_ = false;
  update_published_ = false;
  last_scale_publish_ms_ = 0U;
  last_update_publish_ms_ = 0U;
  last_heartbeat_ms_ = 0U;
  last_update_revision_ = 0U;
  return ESP_OK;
}

esp_err_t LocalWebServer::stop() {
  if (server_ == nullptr) return ESP_OK;
  stopping_.store(true, std::memory_order_release);
  const auto handle = server_;
  const auto result = httpd_stop(handle);
  if (result != ESP_OK) {
    stopping_.store(false, std::memory_order_release);
    return result;
  }

  // httpd_stop waits for the HTTP task and its queued transfer callbacks, so
  // the stable handle remains valid until every concurrent user has exited.
  server_ = nullptr;
  api_context_.transport_diagnostics().set_http_server_running(false);
  scale_published_ = false;
  heartbeat_published_ = false;
  update_published_ = false;
  websocket_send_.remaining.store(0U, std::memory_order_relaxed);
  websocket_send_.busy.store(false, std::memory_order_release);
  for (auto& client : websocket_clients_) {
    client.pending_reason.store(
        diagnostics::WebsocketDisconnectReason::none,
        std::memory_order_relaxed);
    client.socket.store(-1, std::memory_order_relaxed);
  }
  return ESP_OK;
}

esp_err_t LocalWebServer::session_open_handler(
    httpd_handle_t server,
    int socket) {
  auto* owner =
      static_cast<LocalWebServer*>(httpd_get_global_user_ctx(server));
  if (owner == nullptr || socket < 0) return ESP_ERR_INVALID_ARG;
  owner->api_context_.transport_diagnostics().http_session_opened();
  return ESP_OK;
}

void LocalWebServer::session_close_handler(
    httpd_handle_t server,
    int socket) {
  auto* owner =
      static_cast<LocalWebServer*>(httpd_get_global_user_ctx(server));
  if (owner != nullptr) owner->handle_session_close(socket);
  if (socket >= 0) (void)lwip_close(socket);
}

void LocalWebServer::preserve_global_context(void*) {}

void LocalWebServer::handle_session_close(int socket) {
  auto& transport = api_context_.transport_diagnostics();
  diagnostics::WebsocketDisconnectReason reason =
      diagnostics::WebsocketDisconnectReason::none;
  if (release_websocket_client(socket, reason)) {
    if (reason == diagnostics::WebsocketDisconnectReason::none) {
      reason = stopping_.load(std::memory_order_acquire)
          ? diagnostics::WebsocketDisconnectReason::server_stopped
          : diagnostics::WebsocketDisconnectReason::peer_or_network_closed;
    }
    transport.websocket_disconnected(reason);
  }
  transport.http_session_closed();
}

bool LocalWebServer::track_websocket_client(int socket) {
  for (auto& client : websocket_clients_) {
    if (client.socket.load(std::memory_order_acquire) == socket) return true;
  }
  for (auto& client : websocket_clients_) {
    int empty = -1;
    if (client.socket.compare_exchange_strong(
            empty, socket, std::memory_order_acq_rel)) {
      client.pending_reason.store(
          diagnostics::WebsocketDisconnectReason::none,
          std::memory_order_release);
      api_context_.transport_diagnostics().websocket_opened();
      return true;
    }
  }
  return false;
}

bool LocalWebServer::release_websocket_client(
    int socket,
    diagnostics::WebsocketDisconnectReason& reason) {
  for (auto& client : websocket_clients_) {
    int expected = socket;
    if (client.socket.compare_exchange_strong(
            expected, -1, std::memory_order_acq_rel)) {
      reason = client.pending_reason.exchange(
          diagnostics::WebsocketDisconnectReason::none,
          std::memory_order_acq_rel);
      return true;
    }
  }
  return false;
}

void LocalWebServer::mark_websocket_disconnect(
    int socket,
    diagnostics::WebsocketDisconnectReason reason) {
  for (auto& client : websocket_clients_) {
    if (client.socket.load(std::memory_order_acquire) == socket) {
      client.pending_reason.store(reason, std::memory_order_release);
      return;
    }
  }
}

esp_err_t LocalWebServer::static_asset_handler(httpd_req_t* request) {
  if (request == nullptr || request->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<LocalWebServer*>(request->user_ctx)
      ->handle_static_asset(request);
}

esp_err_t LocalWebServer::api_handler(httpd_req_t* request) {
  if (request == nullptr || request->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<LocalWebServer*>(request->user_ctx)->handle_api(request);
}

esp_err_t LocalWebServer::update_upload_handler(httpd_req_t* request) {
  if (request == nullptr || request->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<LocalWebServer*>(request->user_ctx)
      ->handle_update_upload(request);
}

esp_err_t LocalWebServer::websocket_handler(httpd_req_t* request) {
  if (request == nullptr || request->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<LocalWebServer*>(request->user_ctx)
      ->handle_websocket(request);
}

esp_err_t LocalWebServer::handle_static_asset(httpd_req_t* request) {
  if (request->content_len != 0U) {
    const auto result = send_json_error(
        request,
        400,
        "unexpected_body",
        "Static asset requests do not accept a body");
    return finish_response_without_purging(request, 0U, result);
  }

  std::string_view path(request->uri);
  const auto query = path.find('?');
  if (query != std::string_view::npos) path = path.substr(0U, query);

  const char* body = nullptr;
  std::size_t size = 0U;
  const char* content_type = nullptr;
  if (path == "/") {
    body = assets::index_html;
    size = assets::index_html_size;
    content_type = "text/html; charset=utf-8";
  } else if (path == "/assets/app.css") {
    body = assets::application_css;
    size = assets::application_css_size;
    content_type = "text/css; charset=utf-8";
  } else if (path == "/assets/app.js") {
    body = assets::application_javascript;
    size = assets::application_javascript_size;
    content_type = "application/javascript; charset=utf-8";
  } else {
    if (provisioning_peer(request, api_context_)) {
      auto result = httpd_resp_set_status(request, "302 Found");
      if (result != ESP_OK) return result;
      result = httpd_resp_set_hdr(
          request, "Location", "http://192.168.4.1/");
      if (result != ESP_OK) return result;
      result = set_security_headers(request);
      if (result != ESP_OK) return result;
      return httpd_resp_send(request, nullptr, 0U);
    }
    return send_json_error(
        request, 404, "route_not_found", "No local asset matches this path");
  }

  auto result = httpd_resp_set_status(request, status_line(200));
  if (result != ESP_OK) return result;
  result = httpd_resp_set_type(request, content_type);
  if (result != ESP_OK) return result;
  result = set_security_headers(request);
  if (result != ESP_OK) return result;
  return httpd_resp_send(request, body, static_cast<ssize_t>(size));
}

esp_err_t LocalWebServer::handle_update_upload(httpd_req_t* request) {
  const auto reject_unread = [request](
      std::int32_t status,
      const char* code,
      const char* message,
      std::size_t consumed = 0U,
      bool retryable = false) {
    const auto sent = send_json_error(
        request, status, code, message, retryable);
    return finish_response_without_purging(request, consumed, sent);
  };

  if (request->method != HTTP_POST ||
      std::string_view(request->uri) != "/api/v1/update/upload") {
    return reject_unread(
        404,
        "route_not_found",
        "No streaming firmware route matches this request");
  }
  if (request->content_len == 0U) {
    return reject_unread(
        400,
        "invalid_firmware_length",
        "Firmware upload requires a nonzero Content-Length");
  }
  if (request->content_len > api::maximum_firmware_image_bytes) {
    return reject_unread(
        413,
        "firmware_too_large",
        "Firmware upload exceeds the 5 MiB application-slot limit");
  }

  std::size_t collected_header_bytes = 0U;
  std::string authorization;
  bool upload_authorized = api_context_.authorize_mutation({});
  if (!upload_authorized &&
      read_required_header(
          request,
          "Authorization",
          authorization,
          collected_header_bytes)) {
    constexpr std::string_view bearer_prefix = "Bearer ";
    const std::string_view authorization_view(authorization);
    upload_authorized = authorization_view.size() > bearer_prefix.size() &&
        authorization_view.substr(0U, bearer_prefix.size()) == bearer_prefix &&
        authorization_view.substr(bearer_prefix.size()).find_first_of(" \t") ==
            std::string_view::npos &&
        api_context_.authorize_mutation(
            authorization_view.substr(bearer_prefix.size()));
  }
  if (!upload_authorized) {
    (void)httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
    return reject_unread(
        401,
        "authentication_required",
        "A valid bearer token is required for firmware upload");
  }

  std::string source;
  std::string idempotency_key;
  std::string content_type;
  std::string expected_sha256;
  std::string expected_generation;
  if (!read_required_header(
          request,
          "X-OpenTag-Request",
          source,
          collected_header_bytes) ||
      source != "web" ||
      !read_required_header(
          request,
          "Idempotency-Key",
          idempotency_key,
          collected_header_bytes) ||
      !valid_idempotency_key(idempotency_key) ||
      !read_required_header(
          request,
          "Content-Type",
          content_type,
          collected_header_bytes) ||
      !read_required_header(
          request,
          "X-OpenTag-Image-SHA256",
          expected_sha256,
          collected_header_bytes) ||
      !read_required_header(
          request,
          "X-OpenTag-Expected-Generation",
          expected_generation,
          collected_header_bytes)) {
    return reject_unread(
        400,
        "invalid_upload_headers",
        "Firmware upload headers are missing, malformed, or too large");
  }
  if (lower_ascii(content_type) != "application/octet-stream") {
    return reject_unread(
        415,
        "unsupported_media_type",
        "Firmware upload requires application/octet-stream");
  }

  StreamingUploadRequest upload_request;
  upload_request.idempotency_key = std::move(idempotency_key);
  upload_request.expected_length =
      static_cast<std::uint32_t>(request->content_len);
  if (!api::parse_canonical_generation(
          expected_generation,
          upload_request.expected_generation) ||
      !decode_sha256(expected_sha256, upload_request.expected_sha256)) {
    return reject_unread(
        400,
        "invalid_upload_precondition",
        "Expected generation must be canonical nonnegative decimal and SHA-256 must be 64 lowercase hexadecimal characters");
  }

  const auto started = api_context_.begin_streaming_upload(upload_request);
  if (!started.ok()) {
    const auto response = api::response_for_context_error(started.error());
    return finish_response_without_purging(
        request,
        0U,
        send_router_response(request, response));
  }
  const auto session = started.value();
  if (session.duplicate) {
    const auto response = upload_receipt(
        session,
        session.replay_snapshot.has_value()
            ? &*session.replay_snapshot
            : nullptr);
    return finish_response_without_purging(
        request,
        0U,
        send_router_response(request, response));
  }

  const auto receive_started_us = esp_timer_get_time();
  auto last_progress_us = receive_started_us;
  std::size_t received = 0U;
  while (received < request->content_len) {
    const auto now_us = esp_timer_get_time();
    if (now_us - receive_started_us >
        static_cast<std::int64_t>(maximum_upload_receive_ms) * 1000LL) {
      const core::Error error{
          core::ErrorCategory::firmware_update,
          "Firmware upload exceeded the 180 second absolute deadline",
          true,
      };
      api_context_.abort_streaming_upload(session, error);
      return reject_unread(
          408,
          "upload_timeout",
          error.message.c_str(),
          received,
          true);
    }
    if (now_us - last_progress_us >
        static_cast<std::int64_t>(maximum_request_receive_ms) * 1000LL) {
      const core::Error error{
          core::ErrorCategory::firmware_update,
          "Firmware upload made no receive progress for five seconds",
          true,
      };
      api_context_.abort_streaming_upload(session, error);
      return reject_unread(
          408,
          "upload_timeout",
          error.message.c_str(),
          received,
          true);
    }

    const auto remaining = request->content_len - received;
    const auto requested = std::min(remaining, upload_buffer_.size());
    const auto count = httpd_req_recv(
        request,
        reinterpret_cast<char*>(upload_buffer_.data()),
        requested);
    if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (count <= 0) {
      const core::Error error{
          core::ErrorCategory::firmware_update,
          "Firmware upload disconnected before its declared length",
          true,
      };
      api_context_.abort_streaming_upload(session, error);
      return reject_unread(
          400,
          "incomplete_firmware_upload",
          error.message.c_str(),
          received);
    }

    const auto chunk_size = static_cast<std::size_t>(count);
    received += chunk_size;
    last_progress_us = esp_timer_get_time();
    const auto written = api_context_.write_streaming_upload(
        session,
        {upload_buffer_.data(), chunk_size});
    if (!written.ok()) {
      api_context_.abort_streaming_upload(session, written.error());
      const auto response = api::response_for_context_error(written.error());
      return finish_response_without_purging(
          request,
          received,
          send_router_response(request, response));
    }
    // Flash-owner latency is part of the absolute bound but not a network
    // no-progress interval.
    last_progress_us = esp_timer_get_time();
  }

  if (esp_timer_get_time() - receive_started_us >
      static_cast<std::int64_t>(maximum_upload_receive_ms) * 1000LL) {
    const core::Error error{
        core::ErrorCategory::firmware_update,
        "Firmware upload exceeded the 180 second absolute deadline",
        true,
    };
    api_context_.abort_streaming_upload(session, error);
    return reject_unread(
        408,
        "upload_timeout",
        error.message.c_str(),
        received,
        true);
  }

  const auto finished = api_context_.finish_streaming_upload(session);
  if (!finished.ok()) {
    api_context_.abort_streaming_upload(session, finished.error());
    const auto response = api::response_for_context_error(finished.error());
    return send_router_response(request, response);
  }
  // A successful finish is an authoritative, durable validation boundary.
  // Never attempt to cancel that candidate merely because validation crossed
  // the transport deadline while the owner was completing it.
  const auto response = upload_receipt(session, &finished.value());
  return send_router_response(request, response);
}

esp_err_t LocalWebServer::handle_api(httpd_req_t* request) {
  if (std::string_view(request->uri).rfind("/api/v1/events", 0U) == 0U) {
    const auto result = send_json_error(
        request,
        404,
        "route_not_found",
        "The live event endpoint is available only as a WebSocket");
    return finish_response_without_purging(request, 0U, result);
  }
  if (request->content_len > api::maximum_request_body_bytes) {
    const auto result = send_json_error(
        request,
        413,
        "request_too_large",
        "The request body exceeds the 16 KiB transport limit");
    return finish_response_without_purging(request, 0U, result);
  }

  api::Request api_request;
  api_request.method = api_method(request->method);
  api_request.path = request->uri;
  static constexpr std::array<const char*, 5U> collected_headers = {{
      "Content-Type",
      "X-OpenTag-Request",
      "Idempotency-Key",
      "Authorization",
      "Accept",
  }};
  for (const auto* header : collected_headers) {
    const auto result = collect_header(request, header, api_request);
    if (result != ESP_OK) {
      const auto sent = send_json_error(
          request,
          400,
          "invalid_request_headers",
          "A forwarded request header is invalid or too large");
      return finish_response_without_purging(request, 0U, sent);
    }
  }

  const auto receive_started_us = esp_timer_get_time();
  const auto receive_expired = [&]() {
    return esp_timer_get_time() - receive_started_us >
        static_cast<std::int64_t>(maximum_request_receive_ms) * 1000LL;
  };
  api_request.body.resize(request->content_len);
  api_request.provisioning_transport =
      provisioning_peer(request, api_context_);
  std::size_t received = 0U;
  while (received < request->content_len) {
    if (receive_expired()) {
      const auto sent = send_json_error(
          request,
          408,
          "request_timeout",
          "The complete request body was not received within five seconds");
      return finish_response_without_purging(request, received, sent);
    }
    const auto count = httpd_req_recv(
        request,
        api_request.body.data() + received,
        request->content_len - received);
    if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (count <= 0) {
      const auto sent = send_json_error(
          request,
          400,
          "incomplete_request_body",
          "The complete declared request body was not received");
      return finish_response_without_purging(request, received, sent);
    }
    received += static_cast<std::size_t>(count);
    if (receive_expired()) {
      const auto sent = send_json_error(
          request,
          408,
          "request_timeout",
          "The complete request body was not received within five seconds");
      return finish_response_without_purging(request, received, sent);
    }
  }

  const auto response = router_.handle(api_request);
  const auto sent = send_router_response(request, response);
  if (sent == ESP_OK &&
      response.delivered_network_connect_operation.has_value()) {
    (void)api_context_.acknowledge_network_connect_receipt(
        *response.delivered_network_connect_operation);
  }
  return sent;
}

esp_err_t LocalWebServer::handle_websocket(httpd_req_t* request) {
  const auto socket = httpd_req_to_sockfd(request);
  if (request->method == HTTP_GET) {
    if (request->content_len != 0U) return ESP_FAIL;
    if (socket < 0 || websocket_client_count(socket) >= maximum_websocket_clients) {
      return ESP_FAIL;
    }
    return track_websocket_client(socket) ? ESP_OK : ESP_FAIL;
  }

  // This is a server-push channel, not a bidirectional API. The pinned HTTPD
  // handles bounded PING/PONG/CLOSE frames internally; reject application data
  // before reading its payload so a client cannot slow-drip a frame while
  // monopolizing the single HTTP server task.
  mark_websocket_disconnect(
      socket, diagnostics::WebsocketDisconnectReason::protocol_rejected);
  return ESP_FAIL;
}

std::size_t LocalWebServer::websocket_client_count(int excluded_fd) const {
  if (server_ == nullptr) return 0U;
  std::array<int, maximum_open_sockets> sockets{};
  std::size_t count = sockets.size();
  if (httpd_get_client_list(server_, &count, sockets.data()) != ESP_OK) {
    return 0U;
  }
  std::size_t clients = 0U;
  for (std::size_t index = 0U; index < count; ++index) {
    const auto socket = sockets[index];
    if (socket != excluded_fd &&
        httpd_ws_get_fd_info(server_, socket) ==
            HTTPD_WS_CLIENT_WEBSOCKET) {
      ++clients;
    }
  }
  return clients;
}

std::string LocalWebServer::make_scale_event() {
  const auto event = api_context_.scale_event_json();
  return event.ok() &&
          !event.value().empty() &&
          event.value().size() <= maximum_websocket_message_bytes
      ? event.value()
      : std::string(invalid_scale_event);
}

std::string LocalWebServer::make_update_event(std::uint64_t& revision) {
  revision = 0U;
  const auto event = api_context_.update_event_json(revision);
  return event.ok() &&
          !event.value().empty() &&
          event.value().size() <= maximum_websocket_message_bytes
      ? event.value()
      : std::string(invalid_update_event);
}

void LocalWebServer::websocket_send_complete(
    esp_err_t error,
    int socket,
    void* context) {
  auto* batch = static_cast<WebsocketSendBatch*>(context);
  if (batch == nullptr) return;
  auto* owner = batch->owner;
  if (batch->server != nullptr && owner != nullptr &&
      !owner->stopping_.load(std::memory_order_acquire)) {
    if (error == ESP_OK) {
      (void)httpd_sess_update_lru_counter(batch->server, socket);
    } else {
      owner->api_context_.transport_diagnostics().websocket_send_failed();
      owner->mark_websocket_disconnect(
          socket, diagnostics::WebsocketDisconnectReason::send_failure);
      (void)httpd_sess_trigger_close(batch->server, socket);
    }
  }
  const auto remaining =
      batch->remaining.fetch_sub(1U, std::memory_order_acq_rel);
  if (remaining == 1U) {
    batch->busy.store(false, std::memory_order_release);
  }
}

std::size_t LocalWebServer::send_to_websocket_clients(
    const std::string& message) {
  if (server_ == nullptr || message.empty() ||
      message.size() > maximum_websocket_message_bytes) {
    return 0U;
  }
  std::array<int, maximum_open_sockets> sockets{};
  std::size_t count = sockets.size();
  if (httpd_get_client_list(server_, &count, sockets.data()) != ESP_OK) {
    return 0U;
  }

  std::size_t client_count = 0U;
  for (std::size_t index = 0U; index < count; ++index) {
    const auto socket = sockets[index];
    if (httpd_ws_get_fd_info(server_, socket) !=
        HTTPD_WS_CLIENT_WEBSOCKET) {
      continue;
    }
    if (client_count >= maximum_websocket_clients) {
      api_context_.transport_diagnostics().websocket_event_dropped();
      mark_websocket_disconnect(
          socket,
          diagnostics::WebsocketDisconnectReason::protocol_rejected);
      (void)httpd_sess_trigger_close(server_, socket);
      continue;
    }
    websocket_send_.sockets[client_count++] = socket;
  }
  if (client_count == 0U) return 0U;

  bool expected = false;
  if (!websocket_send_.busy.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return 0U;
  }
  websocket_send_.owner = this;
  websocket_send_.server = server_;
  std::copy(
      message.begin(), message.end(), websocket_send_.payload.begin());
  websocket_send_.remaining.store(client_count, std::memory_order_release);

  std::size_t queued = 0U;
  for (std::size_t index = 0U; index < client_count; ++index) {
    auto& frame = websocket_send_.frames[index];
    frame = {};
    frame.final = true;
    frame.fragmented = false;
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = websocket_send_.payload.data();
    frame.len = message.size();
    const auto result = httpd_ws_send_data_async(
        server_,
        websocket_send_.sockets[index],
        &frame,
        &LocalWebServer::websocket_send_complete,
        &websocket_send_);
    if (result == ESP_OK) {
      ++queued;
    } else {
      websocket_send_complete(
          result, websocket_send_.sockets[index], &websocket_send_);
    }
  }
  return queued;
}

void LocalWebServer::publish(std::uint32_t now_ms) {
  if (server_ == nullptr || websocket_client_count() == 0U) return;
  // The network task is the sole publisher. Avoid constructing JSON while the
  // HTTPD task still owns the reusable batch payload for a prior socket send.
  if (websocket_send_.busy.load(std::memory_order_acquire)) return;

  const auto current_update_revision = api_context_.update_revision();
  if ((!update_published_ ||
       current_update_revision != last_update_revision_) &&
      due(
          now_ms,
          last_update_publish_ms_,
          update_publish_interval_ms,
          update_published_)) {
    std::uint64_t published_revision = 0U;
    const auto queued = send_to_websocket_clients(
        make_update_event(published_revision));
    if (queued > 0U) {
      last_update_revision_ = published_revision;
      last_update_publish_ms_ = now_ms;
      update_published_ = true;
      return;
    }
  }

  if (due(
          now_ms,
          last_scale_publish_ms_,
          scale_publish_interval_ms,
          scale_published_)) {
    const auto queued = send_to_websocket_clients(make_scale_event());
    if (queued > 0U) {
      last_scale_publish_ms_ = now_ms;
      scale_published_ = true;
      return;
    }
  }
  if (due(
          now_ms,
          last_heartbeat_ms_,
          heartbeat_interval_ms,
          heartbeat_published_)) {
    std::string heartbeat =
        R"({"type":"heartbeat","data":{"uptime_ms":)";
    heartbeat += std::to_string(now_ms);
    heartbeat += "}}";
    const auto queued = send_to_websocket_clients(heartbeat);
    if (queued > 0U) {
      last_heartbeat_ms_ = now_ms;
      heartbeat_published_ = true;
    }
  }
}

}  // namespace opentag::web

#include "web/local_web_server.hpp"

#include <ArduinoJson.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "web/web_assets.hpp"

#if !defined(CONFIG_HTTPD_WS_SUPPORT) || CONFIG_HTTPD_WS_SUPPORT != 1
#error "LocalWebServer requires ESP-IDF HTTP server WebSocket support"
#endif

namespace opentag::web {
namespace {

constexpr std::size_t maximum_collected_header_value_bytes = 512U;
constexpr std::uint32_t maximum_request_receive_ms = 5000U;
constexpr std::size_t maximum_event_json_nesting = 8U;
constexpr char invalid_scale_event[] =
    R"({"type":"invalidate","data":{"resource":"scale"}})";

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
  route.handle_ws_control_frames = websocket;
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
    const char* message) {
  std::string body =
      R"({"api_version":"v1","ok":false,"error":{"code":")";
  body += code;
  body += R"(","message":")";
  body += message;
  body += R"(","retryable":false}})";
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

}  // namespace

LocalWebServer::LocalWebServer(api::Router& router) noexcept : router_(router) {}

LocalWebServer::~LocalWebServer() {
  if (server_ != nullptr) (void)stop();
}

esp_err_t LocalWebServer::start() {
  if (server_ != nullptr) return ESP_OK;
  stopping_.store(false, std::memory_order_release);

  httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
  configuration.stack_size = 6144U;
  configuration.max_open_sockets =
      static_cast<std::uint16_t>(maximum_open_sockets);
  configuration.max_uri_handlers = 12U;
  configuration.max_resp_headers = 10U;
  configuration.backlog_conn = 2U;
  configuration.lru_purge_enable = true;
  configuration.recv_wait_timeout = 1U;
  configuration.send_wait_timeout = 1U;
  configuration.uri_match_fn = httpd_uri_match_wildcard;

  auto result = httpd_start(&server_, &configuration);
  if (result != ESP_OK) {
    server_ = nullptr;
    return result;
  }

  const std::array<httpd_uri_t, 11U> handlers = {{
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
      make_uri("/api/*", HTTP_GET, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_POST, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_PATCH, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_PUT, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_DELETE, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_HEAD, &LocalWebServer::api_handler, this),
      make_uri("/api/*", HTTP_OPTIONS, &LocalWebServer::api_handler, this),
  }};
  for (const auto& handler : handlers) {
    result = httpd_register_uri_handler(server_, &handler);
    if (result != ESP_OK) {
      (void)httpd_stop(server_);
      server_ = nullptr;
      return result;
    }
  }

  scale_published_ = false;
  heartbeat_published_ = false;
  last_scale_publish_ms_ = 0U;
  last_heartbeat_ms_ = 0U;
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
  scale_published_ = false;
  heartbeat_published_ = false;
  websocket_send_.remaining.store(0U, std::memory_order_relaxed);
  websocket_send_.busy.store(false, std::memory_order_release);
  return ESP_OK;
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

  return send_router_response(request, router_.handle(api_request));
}

esp_err_t LocalWebServer::handle_websocket(httpd_req_t* request) {
  const auto socket = httpd_req_to_sockfd(request);
  if (request->method == HTTP_GET) {
    if (request->content_len != 0U) return ESP_FAIL;
    if (socket < 0 || websocket_client_count(socket) >= maximum_websocket_clients) {
      return ESP_FAIL;
    }
    return ESP_OK;
  }

  // This is a server-push channel, not a bidirectional API. Reject data and
  // control frames before reading their payload so an unauthenticated client
  // cannot slow-drip a frame while monopolizing the single HTTP server task.
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
  const auto response = router_.handle(
      {api::Method::get,
       "/api/v1/scale",
       {{"Accept", "application/json"}},
       {}});
  if (response.status != 200 || response.body.empty()) {
    return invalid_scale_event;
  }

  JsonDocument envelope;
  const auto parsed = deserializeJson(
      envelope,
      response.body,
      DeserializationOption::NestingLimit(maximum_event_json_nesting));
  if (parsed || !envelope.is<JsonObjectConst>()) return invalid_scale_event;
  const auto root = envelope.as<JsonObjectConst>();
  const auto* api_version = root["api_version"].as<const char*>();
  const auto data = root["data"];
  if (api_version == nullptr || std::strcmp(api_version, "v1") != 0 ||
      !root["ok"].is<bool>() || !root["ok"].as<bool>() ||
      data.isUnbound()) {
    return invalid_scale_event;
  }

  constexpr std::string_view leading = R"({"type":"scale","data":)";
  const auto data_size = measureJson(data);
  if (leading.size() + data_size + 1U > maximum_websocket_message_bytes) {
    return invalid_scale_event;
  }
  std::string event;
  event.reserve(leading.size() + data_size + 1U);
  event.append(leading.data(), leading.size());
  serializeJson(data, event);
  event.push_back('}');
  return event.size() <= maximum_websocket_message_bytes
      ? event
      : std::string(invalid_scale_event);
}

void LocalWebServer::websocket_send_complete(
    esp_err_t error,
    int socket,
    void* context) {
  auto* batch = static_cast<WebsocketSendBatch*>(context);
  if (batch == nullptr) return;
  const auto* owner = batch->owner;
  if (batch->server != nullptr && owner != nullptr &&
      !owner->stopping_.load(std::memory_order_acquire)) {
    if (error == ESP_OK) {
      (void)httpd_sess_update_lru_counter(batch->server, socket);
    } else {
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

  if (due(
          now_ms,
          last_scale_publish_ms_,
          scale_publish_interval_ms,
          scale_published_)) {
    const auto queued = send_to_websocket_clients(make_scale_event());
    if (queued > 0U) {
      last_scale_publish_ms_ = now_ms;
      scale_published_ = true;
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

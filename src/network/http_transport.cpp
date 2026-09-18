#include "network/http_transport.hpp"
#include "network/deadline_client.hpp"
#include "network/bounded_response_body.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "network/tcp_connect.hpp"
#include "network/bounded_dns.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace opentag::network {
namespace {

core::Error request_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

#ifdef ARDUINO
core::Error network_error(const std::string& message, bool retryable = true) {
  return {core::ErrorCategory::network, message, retryable};
}

class BackendWiFiClient final : public WiFiClient {
 public:
  explicit BackendWiFiClient(IPAddress resolved) : resolved_(resolved) {}
  int connect(const char*, std::uint16_t port, std::int32_t timeout) override {
    return connect(resolved_, port, timeout);
  }
  int connect(IPAddress ip, std::uint16_t port, std::int32_t timeout) override {
    stop();
    const auto connected = connect_tcp_ipv4(static_cast<std::uint32_t>(ip), port, timeout);
    if (connected.socket < 0) { errno = connected.error; return 0; }
    WiFiClient::operator=(WiFiClient(connected.socket));
    _timeout = timeout;
    return 1;
  }
 private:
  IPAddress resolved_;
};

class BoundedResponseStream final : public Stream, public BoundedResponseBody {
 public:
  using BoundedResponseBody::BoundedResponseBody;
  std::size_t write(std::uint8_t byte) override {return BoundedResponseBody::write(byte);}
  std::size_t write(const std::uint8_t* data,std::size_t size) override {return BoundedResponseBody::write(data,size);}
  int available() override {return 0;}
  int read() override {return -1;}
  int peek() override {return -1;}
  void flush() override {}
};

bool valid_method(const std::string& method) {
  return method == "GET" || method == "POST" || method == "PUT" ||
      method == "PATCH" || method == "DELETE" || method == "HEAD";
}
#endif

}  // namespace

core::Result<ParsedUrl> parse_http_url(const std::string& url) {
  if (url.empty() || url.size() > 512U ||
      url.find('@') != std::string::npos || url.find('#') != std::string::npos) {
    return core::Result<ParsedUrl>::failure(request_error("HTTP URL is invalid"));
  }
  ParsedUrl result;
  std::size_t authority_begin = 0U;
  if (url.rfind("https://", 0U) == 0U) {
    result.secure = true;
    result.port = 443U;
    authority_begin = 8U;
  } else if (url.rfind("http://", 0U) == 0U) {
    result.port = 80U;
    authority_begin = 7U;
  } else {
    return core::Result<ParsedUrl>::failure(
        request_error("HTTP URL must use http or https"));
  }
  const auto slash = url.find('/', authority_begin);
  const auto query = url.find('?', authority_begin);
  const auto path_begin = slash == std::string::npos
                              ? query
                              : query == std::string::npos
                                    ? slash
                                    : std::min(slash, query);
  const auto authority = url.substr(
      authority_begin,
      path_begin == std::string::npos
          ? std::string::npos
          : path_begin - authority_begin);
  result.path = path_begin == std::string::npos
                    ? "/"
                    : url[path_begin] == '?'
                          ? "/" + url.substr(path_begin)
                          : url.substr(path_begin);
  if (authority.empty()) {
    return core::Result<ParsedUrl>::failure(request_error("HTTP URL host is missing"));
  }
  const auto colon = authority.rfind(':');
  result.host = colon == std::string::npos ? authority : authority.substr(0U, colon);
  if (colon != std::string::npos) {
    const auto port_text = authority.substr(colon + 1U);
    if (port_text.empty() || !std::all_of(port_text.begin(), port_text.end(), [](char c) {
          return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
      return core::Result<ParsedUrl>::failure(request_error("HTTP URL port is invalid"));
    }
    std::uint32_t port = 0U;
    for (const auto character : port_text) {
      port = port * 10U + static_cast<std::uint32_t>(character - '0');
      if (port > 65535U) break;
    }
    if (port == 0U || port > 65535U) {
      return core::Result<ParsedUrl>::failure(request_error("HTTP URL port is invalid"));
    }
    result.port = static_cast<std::uint16_t>(port);
  }
  const bool valid_host = !result.host.empty() && result.host.size() <= 253U &&
      result.host.front() != '.' && result.host.back() != '.' &&
      result.host.front() != '-' && result.host.back() != '-' &&
      std::all_of(result.host.begin(), result.host.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '.' || character == '-';
      });
  const bool valid_path = result.path.size() <= 1024U &&
      std::none_of(result.path.begin(), result.path.end(), [](char character) {
        return static_cast<unsigned char>(character) < 0x20U || character == ' ';
      });
  if (!valid_host) {
    return core::Result<ParsedUrl>::failure(request_error("HTTP URL host is invalid"));
  }
  if (!valid_path) {
    return core::Result<ParsedUrl>::failure(request_error("HTTP URL path is invalid"));
  }
  return core::Result<ParsedUrl>::success(std::move(result));
}

#ifdef ARDUINO
core::Result<HttpResponse> HttpTransport::perform(const HttpRequest& request) {
  const auto started = millis();
  BackendPhaseGuard released{"http_released", started};
  backend_memory_phase("before_http", 0, 0, started);
  // Request-local budget: never extends the backend owner's normal budget.
  OperationBudget catalog_budget;
  if(request.catalog_update)catalog_budget.begin_catalog_update(started);
  const auto& budget=request.catalog_update?catalog_budget:budget_;
  const auto deadline_error = [&]() {
    return core::Result<HttpResponse>::failure(network_error(request.catalog_update?
        "Community catalog update timed out; existing catalog retained":
        "Backend operation deadline exceeded"));
  };
  if (budget.expired(millis())) return deadline_error();
  const auto parsed = parse_http_url(request.url);
  if (!parsed.ok()) return core::Result<HttpResponse>::failure(parsed.error());
  if (!backend_admitted(backend_heap(), parsed.value().secure)) {
    return core::Result<HttpResponse>::failure({core::ErrorCategory::backend_unavailable,
        "Backend deferred: insufficient internal memory; local controls remain available", true});
  }
  if (!valid_method(request.method) || request.connect_timeout_ms < 100U ||
      request.connect_timeout_ms > 60000U || request.read_timeout_ms < 100U ||
      request.read_timeout_ms > 60000U || request.maximum_response_bytes == 0U ||
      request.maximum_response_bytes > 65536U || request.body.size() > 65536U ||
      request.headers.size() > 32U ||
      (request.catalog_update && !request.response_consumer) ||
      (request.response_consumer && (request.maximum_stream_bytes == 0 ||
       request.maximum_stream_bytes > 64U*1024U*1024U || request.method != "GET"))) {
    return core::Result<HttpResponse>::failure(
        request_error("HTTP request limits are invalid"));
  }
  if (parsed.value().secure && request.ca_certificate_pem.empty()) {
    return core::Result<HttpResponse>::failure(
        request_error("HTTPS requires a configured CA certificate"));
  }
  if (WiFi.status() != WL_CONNECTED) {
    return core::Result<HttpResponse>::failure(
        network_error("HTTP request requires Wi-Fi"));
  }

  IPAddress resolved;
  if (!resolve_backend_host(parsed.value().host.c_str(), resolved, budget)) {
    if (budget.expired(millis())) return deadline_error();
    return core::Result<HttpResponse>::failure(
        network_error("DNS resolution failed or is still pending for " + parsed.value().host));
  }
  if (budget.expired(millis())) return deadline_error();

  std::unique_ptr<WiFiClient> client;
  if (parsed.value().secure) {
    auto secure = std::unique_ptr<WiFiClientSecure>(new (std::nothrow) WiFiClientSecure);
    if (!secure) return core::Result<HttpResponse>::failure(network_error("TLS client allocation failed"));
    secure->setCACert(request.ca_certificate_pem.c_str());
    secure->setHandshakeTimeout(
        std::max<std::uint32_t>(1U, (request.connect_timeout_ms + 999U) / 1000U));
    client = std::move(secure);
  } else {
    client.reset(new (std::nothrow) BackendWiFiClient(resolved));
  }
  if (!client) return core::Result<HttpResponse>::failure(network_error("HTTP client allocation failed"));

  const auto clock = []() { return static_cast<std::uint32_t>(millis()); };
  DeadlineClient<WiFiClient, IPAddress, decltype(clock)> bounded(*client, budget, clock);
  HTTPClient http;
  if (!http.begin(bounded, request.url.c_str())) {
    return core::Result<HttpResponse>::failure(
        network_error("HTTP client initialization failed"));
  }
  http.setConnectTimeout(static_cast<std::int32_t>(request.connect_timeout_ms));
  http.setTimeout(static_cast<std::uint16_t>(request.read_timeout_ms));
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  const char* collected_headers[] = {"Content-Type","Content-Encoding"};
  http.collectHeaders(collected_headers, 2U);
  if(request.accept_gzip&&request.response_consumer)http.addHeader("Accept-Encoding","gzip");
  for (const auto& header : request.headers) {
    if (header.first.empty() || header.first.size() > 128U ||
        header.second.size() > 1024U || header.first.find('\n') != std::string::npos ||
        header.second.find('\n') != std::string::npos ||
        header.first.find('\r') != std::string::npos ||
        header.second.find('\r') != std::string::npos) {
      http.end();
      return core::Result<HttpResponse>::failure(
          request_error("HTTP header is invalid"));
    }
    http.addHeader(header.first.c_str(), header.second.c_str());
  }

  const auto status_code = http.sendRequest(
      request.method.c_str(),
      reinterpret_cast<std::uint8_t*>(const_cast<char*>(request.body.data())),
      request.body.size());
  if (status_code <= 0) {
    if (budget.expired(millis()) || (request.catalog_update &&
        (status_code == HTTPC_ERROR_READ_TIMEOUT || bounded.connection_errno() == ETIMEDOUT))) {
      http.end(); return deadline_error();
    }
    if (const auto* reason = connection_failure_message(bounded.connection_errno())) {
      http.end();
      return core::Result<HttpResponse>::failure(network_error(reason));
    }
    const auto message = std::string("HTTP transport failed: ") +
        HTTPClient::errorToString(status_code).c_str();
    http.end();
    return core::Result<HttpResponse>::failure(network_error(message));
  }
  const auto content_length = http.getSize();
  if (content_length > static_cast<std::int32_t>(request.response_consumer ? request.maximum_stream_bytes : request.maximum_response_bytes)) {
    http.end();
    return core::Result<HttpResponse>::failure(
        network_error("HTTP response exceeds configured limit", false));
  }
  const std::string encoding=http.header("Content-Encoding").c_str();
  if(!encoding.empty()&&encoding!="identity"&&!(encoding=="gzip"&&request.accept_gzip&&request.response_consumer)) {
    http.end();return core::Result<HttpResponse>::failure(network_error("Unsupported HTTP content encoding",false));
  }
  BoundedResponseStream response_stream(request,encoding=="gzip",[&]{bounded.stop();});
  const auto copied = http.writeToStream(&response_stream);
  if (response_stream.failed()) {
    http.end();
    return core::Result<HttpResponse>::failure({core::ErrorCategory::backend_unavailable,
        "Backend response PSRAM allocation failed; retry later", true});
  }
  if (budget.expired(millis()) ||
      (request.catalog_update && copied == HTTPC_ERROR_READ_TIMEOUT)) {
    http.end();
    return deadline_error();
  }
  if ((copied < 0 && !response_stream.early_complete()) || response_stream.overflowed() || !response_stream.complete()) {
    http.end();
    return core::Result<HttpResponse>::failure(
        network_error("HTTP response read failed or exceeded its limit"));
  }
  HttpResponse response;
  response.status_code = status_code;
  response.content_type = http.header("Content-Type").c_str();
  response.body = response_stream.take();
  http.end();
  backend_memory_phase("after_http", response.body.size(), 0, started);
  return core::Result<HttpResponse>::success(std::move(response));
}
#else
core::Result<HttpResponse> HttpTransport::perform(const HttpRequest&) {
  return core::Result<HttpResponse>::failure(
      {core::ErrorCategory::network,
       "HTTP transport is unavailable in a native build",
       false});
}
#endif

}  // namespace opentag::network

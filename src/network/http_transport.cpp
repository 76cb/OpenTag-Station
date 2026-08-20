#include "network/http_transport.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
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

class BoundedResponseStream final : public Stream {
 public:
  explicit BoundedResponseStream(std::size_t maximum) : maximum_(maximum) {
    data_.reserve(std::min<std::size_t>(maximum, 1024U));
  }

  std::size_t write(std::uint8_t value) override {
    return write(&value, 1U);
  }

  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    if (data_.size() + size > maximum_) {
      overflowed_ = true;
      return 0U;
    }
    data_.append(reinterpret_cast<const char*>(data), size);
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  [[nodiscard]] bool overflowed() const { return overflowed_; }
  [[nodiscard]] std::string take() { return std::move(data_); }

 private:
  std::size_t maximum_;
  bool overflowed_{false};
  std::string data_;
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
  const auto parsed = parse_http_url(request.url);
  if (!parsed.ok()) return core::Result<HttpResponse>::failure(parsed.error());
  if (!valid_method(request.method) || request.connect_timeout_ms < 100U ||
      request.connect_timeout_ms > 60000U || request.read_timeout_ms < 100U ||
      request.read_timeout_ms > 60000U || request.maximum_response_bytes == 0U ||
      request.maximum_response_bytes > 65536U || request.body.size() > 65536U ||
      request.headers.size() > 32U) {
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
  if (!WiFi.hostByName(parsed.value().host.c_str(), resolved)) {
    return core::Result<HttpResponse>::failure(
        network_error("DNS resolution failed for " + parsed.value().host));
  }

  std::unique_ptr<WiFiClient> client;
  if (parsed.value().secure) {
    auto secure = std::make_unique<WiFiClientSecure>();
    secure->setCACert(request.ca_certificate_pem.c_str());
    secure->setHandshakeTimeout(
        std::max<std::uint32_t>(1U, (request.connect_timeout_ms + 999U) / 1000U));
    client = std::move(secure);
  } else {
    client = std::make_unique<WiFiClient>();
  }

  HTTPClient http;
  if (!http.begin(*client, request.url.c_str())) {
    return core::Result<HttpResponse>::failure(
        network_error("HTTP client initialization failed"));
  }
  http.setConnectTimeout(static_cast<std::int32_t>(request.connect_timeout_ms));
  http.setTimeout(static_cast<std::uint16_t>(request.read_timeout_ms));
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  const char* collected_headers[] = {"Content-Type"};
  http.collectHeaders(collected_headers, 1U);
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
    const auto message = std::string("HTTP transport failed: ") +
        HTTPClient::errorToString(status_code).c_str();
    http.end();
    return core::Result<HttpResponse>::failure(network_error(message));
  }
  const auto content_length = http.getSize();
  if (content_length > static_cast<std::int32_t>(request.maximum_response_bytes)) {
    http.end();
    return core::Result<HttpResponse>::failure(
        network_error("HTTP response exceeds configured limit", false));
  }
  BoundedResponseStream response_stream(request.maximum_response_bytes);
  const auto copied = http.writeToStream(&response_stream);
  if (copied < 0 || response_stream.overflowed()) {
    http.end();
    return core::Result<HttpResponse>::failure(
        network_error("HTTP response read failed or exceeded its limit"));
  }
  HttpResponse response;
  response.status_code = status_code;
  response.content_type = http.header("Content-Type").c_str();
  response.body = response_stream.take();
  http.end();
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

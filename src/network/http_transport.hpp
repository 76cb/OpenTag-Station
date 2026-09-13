#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/result.hpp"
#include "network/operation_budget.hpp"

namespace opentag::network {

struct ParsedUrl {
  bool secure{false};
  std::string host;
  std::uint16_t port{0U};
  std::string path;
};

[[nodiscard]] core::Result<ParsedUrl> parse_http_url(const std::string& url);

struct HttpRequest {
  std::string method{"GET"};
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string ca_certificate_pem;
  std::uint32_t connect_timeout_ms{5000U};
  std::uint32_t read_timeout_ms{5000U};
  std::size_t maximum_response_bytes{16384U};
};

struct HttpResponse {
  std::int32_t status_code{0};
  std::string body;
  std::string content_type;
};

class IHttpTransport {
 public:
  virtual ~IHttpTransport() = default;
  [[nodiscard]] virtual core::Result<HttpResponse> perform(
      const HttpRequest& request) = 0;
};

class HttpTransport final : public IHttpTransport {
 public:
  // Set/cleared only by BackendWorker around each command/probe cycle.
  void begin_operation(std::uint32_t now_ms) { budget_.begin(now_ms); }
  void end_operation() { budget_.end(); }
  [[nodiscard]] core::Result<HttpResponse> perform(
      const HttpRequest& request) override;
 private:
  OperationBudget budget_;
};

}  // namespace opentag::network

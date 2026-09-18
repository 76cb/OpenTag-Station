#pragma once
#include "network/http_transport.hpp"
#include "network/gzip_stream.hpp"

namespace opentag::network {
// Portable response sink shared by HTTPClient and native transport tests.
class BoundedResponseBody {
 public:
  explicit BoundedResponseBody(const HttpRequest& request, bool gzip, std::function<void()> stop)
      : data_(request.maximum_response_bytes), request_(request), stop_(std::move(stop)) {
    if(gzip)gzip_=make_external<GzipStream>([&]{return GzipStream(request.response_consumer,request.maximum_stream_bytes);});
    stream_failed_=gzip&&!gzip_;
  }

  std::size_t write(std::uint8_t value) {
    return write(&value, 1U);
  }

  std::size_t write(const std::uint8_t* data, std::size_t size) {
    if(stream_failed_||early_complete_)return 0;
    if (request_.response_consumer) {
      if (size > request_.maximum_stream_bytes - received_) {stream_failed_=true;stop_();return 0;}
      const auto disposition=gzip_?gzip_->feed(data,size):request_.response_consumer(data,size);
      received_ += size;
      if(disposition!=StreamDisposition::next) {
        early_complete_=disposition==StreamDisposition::complete;
        stream_failed_=disposition==StreamDisposition::error;
        stop_(); // Stop before HTTPClient can fetch or retry another body chunk.
        return 0;
      }
      return size;
    }
    return data_.append(reinterpret_cast<const char*>(data), size) ? size : 0U;
  }

  [[nodiscard]] bool overflowed() const { return data_.overflowed() || stream_failed_; }
  [[nodiscard]] bool failed() const { return data_.failed(); }
  [[nodiscard]] bool early_complete() const {return early_complete_;}
  [[nodiscard]] bool complete() const {return !stream_failed_&&(early_complete_||!gzip_||gzip_->finish());}
  [[nodiscard]] ResponseBody take() { return std::move(data_); }

 private:
  ResponseBody data_;
  const HttpRequest& request_;
  std::size_t received_{0};
  bool stream_failed_{false}, early_complete_{false};
  std::function<void()> stop_;
  std::unique_ptr<GzipStream,ExternalDelete<GzipStream>> gzip_;
};

}

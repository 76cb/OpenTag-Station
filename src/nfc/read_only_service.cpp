#include "nfc/read_only_service.hpp"

#include <algorithm>
#include <new>

#include "nfc/protocols/nfcv/read_protocol.hpp"

namespace opentag::nfc {
namespace {
core::Error invalid(const char* message) {
  return {core::ErrorCategory::nfc_communication, message, false};
}
// Covers unsuccessful field enable too: partial hardware state is shut down.
class FieldScope {
 public:
  explicit FieldScope(IReadOnlyReader& reader) : reader_(reader) {}
  ~FieldScope() {
    if (!closed_) (void)reader_.field_off();
  }
  core::Result<void> close() {
    closed_ = true;
    return reader_.field_off();
  }

 private:
  IReadOnlyReader& reader_;
  bool closed_{false};
};
}  // namespace
const char* to_string(ReadState s) {
  switch (s) {
    case ReadState::deferred:
      return "deferred";
    case ReadState::starting:
      return "initializing";
    case ReadState::idle:
      return "idle";
    case ReadState::stabilizing:
      return "stabilizing";
    case ReadState::reading:
      return "reading";
    case ReadState::openprinttag:
      return "openprinttag";
    case ReadState::unsupported:
      return "unsupported";
    case ReadState::multiple:
      return "multiple";
    case ReadState::error:
      return "error";
  }
  return "error";
}
ReadSnapshot ReadOnlyService::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return published_;
}
void ReadOnlyService::publish() {
  live_.bus_errors = reader_.bus_errors();
  std::lock_guard<std::mutex> lock(mutex_);
  published_ = live_;
}
void ReadOnlyService::clear_active() {
  if (processed_ || live_.tag) ++live_.generation;
  live_.tag.reset();
  live_.geometry = {};
  live_.checksum.reset();
  processed_.reset();
}
void ReadOnlyService::fail(const core::Error& e) {
  clear_active();
  candidate_.reset();
  consecutive_ = 0;
  live_.error = e;
  live_.state = ReadState::error;
  live_.present = false;
  live_.tag_count = 0;
  live_.uid.reset();
}
core::Result<void> ReadOnlyService::confirm_uid(const nfcv::Uid& uid) {
  const auto found = reader_.inventory();
  if (!found.ok()) return core::Result<void>::failure(found.error());
  if (found.value().size() != 1 || found.value().front() != uid)
    return core::Result<void>::failure(
        invalid("Tag changed during memory read"));
  return core::Result<void>::success();
}
core::Result<void> ReadOnlyService::read_image(
    const nfcv::Uid& uid, const nfcv::TagGeometry& geometry,
    ReadImage& image, std::uint32_t started) {
  std::size_t preferred = 8U;
  for (std::size_t block = 0; block < geometry.block_count;) {
    if (static_cast<std::uint32_t>(reader_.now_ms() - started) >= 15000U)
      return core::Result<void>::failure(
          invalid("Memory read deadline exceeded"));
    auto count = std::min(preferred, geometry.block_count - block);
    if (block <= 255U) count = std::min(count, 256U - block);
    auto read = reader_.read_blocks(uid, block, count, geometry.block_size,
                                    image.data() + block * geometry.block_size);
    while (!read.ok() && read.error().retryable && count > 1U) {
      count = std::max<std::size_t>(1U, count / 2U);
      preferred = count;
      read = reader_.read_blocks(uid, block, count, geometry.block_size,
                                 image.data() + block * geometry.block_size);
    }
    if (!read.ok()) return read;
    block += count;
    const auto same = confirm_uid(uid);
    if (!same.ok()) return same;
    reader_.yield_between_chunks();
  }
  return core::Result<void>::success();
}
core::Result<void> ReadOnlyService::read_tag(const nfcv::Uid& uid) {
  reader_.stack_checkpoint("read entry");
  const auto geometry = reader_.geometry(uid);
  if (!geometry.ok()) return core::Result<void>::failure(geometry.error());
  const auto valid = geometry.value().validate();
  if (!valid.ok()) return valid;
  if (geometry.value().capacity() > maximum_memory_bytes ||
      geometry.value().block_size > 32U)
    return core::Result<void>::failure(invalid("Unsupported NFC-V geometry"));
  live_.geometry = geometry.value();
  const auto started = reader_.now_ms();
  ReadImage first(geometry.value().capacity()), second(geometry.value().capacity());
  if (!first.data() || !second.data())
    return core::Result<void>::failure(invalid("NFC memory image allocation failed"));
  auto read = read_image(uid, geometry.value(), first, started);
  if (!read.ok()) return read;
  read = read_image(uid, geometry.value(), second, started);
  if (!read.ok()) return read;
  if (!std::equal(first.data(), first.data() + first.size(), second.data()))
    return core::Result<void>::failure(
        invalid("Memory read consistency failed"));
  live_.checksum = nfcv::diagnostic_checksum(first.data(), first.size());
  auto tag = make_read_storage<IdentifiedTag>();
  if (!tag)
    return core::Result<void>::failure(invalid("NFC decode allocation failed"));
  tag->uid = uid;
  tag->geometry = geometry.value();
  tag->checksum = *live_.checksum;
  tag->identified_at_ms = reader_.now_ms();
  tag->generation = ++live_.generation;
  reader_.stack_checkpoint("before decode");
  const auto decoded =
      openprinttag::Codec::decode(core::ByteView(first.data(), first.size()), tag->decoded);
  reader_.stack_checkpoint("after decode");
  processed_ = uid;
  if (!decoded.ok()) {
    live_.state = ReadState::unsupported;
    live_.error = decoded.error();
  } else {
    live_.tag = std::move(tag);
    live_.state = ReadState::openprinttag;
  }
  return core::Result<void>::success();
}
void ReadOnlyService::poll() {
  const auto now = reader_.now_ms();
  const auto cadence = live_.state == ReadState::error ? 5000U : 500U;
  if (polled_ && static_cast<std::uint32_t>(now - last_poll_) < cadence) return;
  polled_ = true;
  last_poll_ = now;
  if (!live_.initialized) {
    const auto init = reader_.initialize();
    if (!init.ok()) {
      fail(init.error());
      publish();
      return;
    }
    live_.initialized = true;
  }
  FieldScope field(reader_);
  auto result = reader_.field_on();
  if (result.ok()) {
    const auto found = reader_.inventory();
    if (!found.ok())
      result = core::Result<void>::failure(found.error());
    else {
      if (live_.state != ReadState::unsupported) live_.error.reset();
      live_.tag_count = found.value().size();
      live_.present = !found.value().empty();
      if (found.value().size() != 1U) {
        live_.error.reset();
        clear_active();
        candidate_.reset();
        consecutive_ = 0;
        live_.uid.reset();
        live_.state =
            found.value().empty() ? ReadState::idle : ReadState::multiple;
      } else {
        const auto uid = found.value().front();
        live_.uid = uid;
        live_.last_seen_ms = now;
        if (!candidate_ || *candidate_ != uid) {
          live_.error.reset();
          clear_active();
          candidate_ = uid;
          consecutive_ = 0;
        }
        if (consecutive_ < stable_polls) ++consecutive_;
        if (!processed_) {
          live_.state = ReadState::stabilizing;
          if (consecutive_ >= stable_polls) {
            live_.state = ReadState::reading;
            publish();
            result = read_tag(uid);
          }
        }
      }
    }
  }
  const auto off = field.close();
  const auto health =
      reader_.health();  // A transport loss must not look like no tag.
  if (!health.ok()) {
    live_.initialized = false;
    result = health;
  }
  if (!off.ok()) result = off;
  if (!result.ok()) fail(result.error());
  publish();
}
}  // namespace opentag::nfc

#pragma once
#include "network/backend_memory.hpp"
#include "nfc/read_only_service.hpp"
#include <functional>

namespace opentag::nfc {
class OpenPrintTagWriter;
struct WriterSystemInformation {
  std::array<std::uint8_t, 64> bytes{};
  std::size_t length{0};
  bool operator==(const WriterSystemInformation &other) const {
    return length == other.length && bytes == other.bytes;
  }
  bool operator!=(const WriterSystemInformation &other) const {
    return !(*this == other);
  }
};
// Destructive transport is deliberately inaccessible through the read
// interface.
class IWriterReader : public IReadOnlyReader {
public:
  virtual core::Result<WriterSystemInformation>
  writer_system_information(const nfcv::Uid &) = 0;
  virtual core::Result<void> security_read(const nfcv::Uid &, std::size_t block,
                                           std::uint8_t *data,
                                           std::uint8_t &security) = 0;

private:
  friend class OpenPrintTagWriter;
  virtual core::Result<void>
  commit_openprinttag_block(const nfcv::Uid &, std::size_t block,
                            const std::uint8_t *data) = 0;
};

struct WriterPlan {
  // Only the physically validated SLIX2 profile is approved in this release.
  static constexpr std::size_t physical_bytes = 320;
  static constexpr std::size_t usable_bytes = 312;
  nfcv::Uid uid;
  nfcv::TagGeometry geometry;
  std::uint64_t generation{0};
  WriterSystemInformation system;
  std::uint32_t current_checksum{0}, target_checksum{0};
  std::array<std::uint8_t, physical_bytes> original{}, target{}, scratch{};
  std::array<std::uint8_t, 80> security{};
  std::array<std::uint8_t, 78> blocks{};
  std::size_t count{0}, completed{0};
  bool blank{false}, recovery{false}, mutable_only{false}, attempted{false},
      verified{false};
  openprinttag::DecodedTag current, proposed, verified_tag;
};

class OpenPrintTagWriter {
public:
  using Progress = std::function<void(const char *, std::size_t, std::size_t)>;
  OpenPrintTagWriter(IWriterReader &reader,
                     std::function<std::uint64_t()> generation)
      : reader_(reader), generation_(std::move(generation)) {}
  core::Result<void> read(WriterPlan &plan,
                          const WriterPlan *interrupted = nullptr);
  core::Result<void> plan(WriterPlan &plan);
  core::Result<void> execute(WriterPlan &plan, const Progress &progress);

private:
  core::Result<void> fence(const WriterPlan &plan);
  core::Result<void> full_read(WriterPlan &plan, std::uint8_t *destination);
  IWriterReader &reader_;
  std::function<std::uint64_t()> generation_;
  std::uint32_t started_ms_{0};
};
} // namespace opentag::nfc

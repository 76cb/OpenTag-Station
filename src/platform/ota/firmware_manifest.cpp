#include "platform/ota/firmware_manifest.hpp"

#include "diagnostics/build_info.hpp"

namespace opentag::platform::ota {
namespace {

template <std::size_t DestinationSize, std::size_t SourceSize>
constexpr std::array<char, DestinationSize> bounded_literal(
    const char (&source)[SourceSize]) {
  static_assert(SourceSize <= DestinationSize);
  std::array<char, DestinationSize> destination{};
  for (std::size_t index = 0U; index < SourceSize; ++index) {
    destination[index] = source[index];
  }
  return destination;
}

constexpr FirmwareManifest make_manifest() {
  FirmwareManifest manifest;
  manifest.magic = firmware_manifest_magic;
  manifest.schema_version = firmware_manifest_schema;
  manifest.structure_size = sizeof(FirmwareManifest);
  manifest.project = bounded_literal<24U>("OpenTag Station");
  manifest.hardware_id = bounded_literal<32U>("wt32-sc01-plus-rev-a");
  manifest.version = bounded_literal<32U>(OPENTAG_PROJECT_VERSION);
  manifest.git_sha = bounded_literal<48U>(OPENTAG_GIT_SHA);
  manifest.build_date = bounded_literal<32U>(OPENTAG_BUILD_DATE);
  manifest.platform = bounded_literal<32U>(OPENTAG_ESP32_PLATFORM);
  manifest.trailer = firmware_manifest_trailer;
  return manifest;
}

}  // namespace

const FirmwareManifest opentag_firmware_manifest
    __attribute__((used, section(".rodata.opentag_manifest"), aligned(4))) =
        make_manifest();

}  // namespace opentag::platform::ota

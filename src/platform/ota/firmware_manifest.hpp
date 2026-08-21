#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opentag::platform::ota {

// Arduino-ESP32's prebuilt esp_app_desc_t identifies the framework build, not
// the OpenTag application. This fixed, read-only record gives the OTA verifier
// an application-owned identity that can be recovered from an inactive image.
// It is metadata, not a signature; authenticity still depends on the trusted
// transport/operator and is documented as a current limitation.
struct FirmwareManifest {
  std::array<std::uint8_t, 16U> magic{};
  std::uint32_t schema_version{0U};
  std::uint32_t structure_size{0U};
  std::array<char, 24U> project{};
  std::array<char, 32U> hardware_id{};
  std::array<char, 32U> version{};
  std::array<char, 48U> git_sha{};
  std::array<char, 32U> build_date{};
  std::array<char, 32U> platform{};
  std::array<std::uint8_t, 16U> reserved{};
  std::array<std::uint8_t, 16U> trailer{};
};

static_assert(sizeof(FirmwareManifest) == 256U);

inline constexpr std::array<std::uint8_t, 16U> firmware_manifest_magic = {
    0x4FU, 0x50U, 0x45U, 0x4EU, 0x54U, 0x41U, 0x47U, 0x2DU,
    0x4FU, 0x54U, 0x41U, 0x2DU, 0x56U, 0x31U, 0xA5U, 0x5AU};
inline constexpr std::array<std::uint8_t, 16U> firmware_manifest_trailer = {
    0x5AU, 0xA5U, 0x31U, 0x56U, 0x2DU, 0x41U, 0x54U, 0x4FU,
    0x2DU, 0x47U, 0x41U, 0x54U, 0x4EU, 0x45U, 0x50U, 0x4FU};
inline constexpr std::uint32_t firmware_manifest_schema = 1U;
inline constexpr std::string_view firmware_project_name = "OpenTag Station";
inline constexpr std::string_view firmware_hardware_id =
    "wt32-sc01-plus-rev-a";

extern const FirmwareManifest opentag_firmware_manifest;

}  // namespace opentag::platform::ota

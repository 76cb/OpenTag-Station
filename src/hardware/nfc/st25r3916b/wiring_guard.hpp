#pragma once

#include "boards/wt32_sc01_plus_rev_a.hpp"

#ifndef OPENTAG_ENABLE_ST25R3916B
#define OPENTAG_ENABLE_ST25R3916B 0
#endif

namespace opentag::hardware::nfc {

using ActiveBoard = boards::Wt32Sc01PlusRevA;

#if OPENTAG_ENABLE_ST25R3916B
static_assert(
    ActiveBoard::nfc.complete(),
    "ST25R3916B was enabled, but the active board profile has incomplete wiring");
#endif

inline constexpr bool st25r3916b_wiring_complete = ActiveBoard::nfc.complete();

}  // namespace opentag::hardware::nfc

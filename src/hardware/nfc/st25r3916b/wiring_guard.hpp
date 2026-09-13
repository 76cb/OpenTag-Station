#pragma once

#include "boards/wt32_sc01_plus_rev_a.hpp"

#ifndef OPENTAG_ENABLE_ST25R3916B
#define OPENTAG_ENABLE_ST25R3916B 0
#endif

namespace opentag::hardware::nfc {

using ActiveBoard = boards::Wt32Sc01PlusRevA;

#if OPENTAG_ENABLE_ST25R3916B
static_assert(
    ActiveBoard::nfc_sda == 13 && ActiveBoard::nfc_scl == 14 &&
        ActiveBoard::nfc_interrupt == 12 && ActiveBoard::touch_i2c_port < 0,
    "ST25R3916B was enabled, but the active board profile has incomplete wiring");
#endif

inline constexpr bool st25r3916b_wiring_complete = OPENTAG_ENABLE_ST25R3916B != 0;

}  // namespace opentag::hardware::nfc

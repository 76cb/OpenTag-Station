# Wiring

## Release validation boundary

All wiring and polarity checks remain UNVERIFIED. Record physical evidence
against the scale and NFC procedures in
[release-validation.md](release-validation.md) before enabling NFC or claiming
scale accuracy.

## Rule

The active board profile is the only firmware source of pin assignments. Driver,
service, and UI code may not contain board GPIO numbers.

## NAU7802

The external I2C connector is currently verified as SDA GPIO 10 and SCL GPIO 11.
For the referenced NAU7802 breakout/load-cell color convention:

| Load-cell wire | NAU7802 terminal |
|---|---|
| Red | E+ |
| Black | E− |
| White | A+ |
| Green | A− |

Wire colors are not a universal electrical contract. Confirm the load-cell data
sheet or resistance measurements before power. If force produces the opposite
sign, swap A+ and A− only after confirming the four-wire mapping.

## ST25R3916B

The exact module is the ELECHOUSE `NFC_ST25R3916B` with this 1.25 mm connector:

The ELECHOUSE ST25R3916B module is assigned only for the opt-in
`wt32-sc01-plus-i2c-test` physical diagnostic:

| Diagnostic signal | WT32 pin |
|---|---|
| SDA | GPIO 13, dedicated diagnostic NFC `Wire1` bus |
| SCL | GPIO 14, dedicated diagnostic NFC `Wire1` bus |
| IRQ | GPIO 12 |
| Supply | board 5 V |
| Ground | GND |
| I2C target address | `0x50` |

The module's I2C solder bridge must be closed. CS/BSS and MOSI are not used by
this diagnostic. The NFC target runs on `Wire1` at 100 kHz and never enables an
RF field, RFAL, inventory, or tag writes.

**Production RFAL wiring remains unassigned. Do not infer production SPI pins
from the diagnostic wiring above.**

| Pin | Module signal |
|---:|---|
| 1 | IRQ |
| 2 | CS / BSS |
| 3 | SCLK / SCL |
| 4 | MOSI |
| 5 | MISO / SDA |
| 6 | +5V |
| 7 | GND |

It exposes neither reset nor power-enable. Do not assign a fake GPIO for either.
The diagnostic transport uses the documented I2C module configuration on its
own `Wire1` bus, with IRQ on GPIO12:

| Module signal | Diagnostic WT32 connection |
|---|---|
| SDA | GPIO13, dedicated diagnostic NFC `Wire1` bus |
| SCL | GPIO14, dedicated diagnostic NFC `Wire1` bus |
| IRQ | GPIO12 |
| +5V | EXT 5V |
| GND | EXT GND |
| CS / BSS, MOSI | not connected in ELECHOUSE's I2C quick-start hookup |
| Reset, power-enable | not present on the module |

**This is active only in the opt-in diagnostic, not in production firmware.**
The production board profile deliberately retains `-1` transport pins. Before
production NFC is enabled, implement the authoritative ST RFAL I2C adapter and
a bounded shared-bus lock used by both NFC and the NAU7802 scale owner, then
verify the module solder bridge and combined pull-ups. The full rationale and
ordered procedure are in [nfc-hardware-bringup.md](nfc-hardware-bringup.md).

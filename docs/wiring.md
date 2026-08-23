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
The recommended future transport is the documented I2C module configuration on
the existing scale bus, with proposed IRQ on GPIO12:

| Module signal | Proposed WT32 connection |
|---|---|
| SDA | GPIO10, shared with NAU7802 |
| SCL | GPIO11, shared with NAU7802 |
| IRQ | GPIO12 |
| +5V | EXT 5V |
| GND | EXT GND |
| CS / BSS, MOSI | not connected in ELECHOUSE's I2C quick-start hookup |
| Reset, power-enable | not present on the module |

**This is a recommendation, not an active wiring assignment. Do not wire or
power the reader from this table yet.** The board profile deliberately retains
`-1` transport pins. First implement the authoritative ST RFAL I2C adapter and
a bounded shared-bus lock used by both NFC and the NAU7802 scale owner, then
verify the module solder bridge and combined pull-ups. The full rationale and
ordered procedure are in [nfc-hardware-bringup.md](nfc-hardware-bringup.md).

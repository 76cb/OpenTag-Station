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

**Not assigned. Do not wire from this document yet.**

| Reader signal | WT32 pin |
|---|---|
| Power / I/O supply | TBD |
| Ground | TBD |
| SCK | TBD |
| MOSI | TBD |
| MISO | TBD |
| CS | TBD |
| IRQ | TBD |
| Reset | TBD |
| Power/enable or bus-select | TBD |

The exact module schematic and WT32 header availability must resolve the
[hardware checkpoint](hardware.md#required-nfc-hardware-checkpoint). Once
resolved, update only `src/boards/wt32_sc01_plus_rev_a.hpp`, enable the build
flag, and perform SPI identity and IRQ tests before turning on the RF field.

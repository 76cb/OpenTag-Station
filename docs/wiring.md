# Production wiring

Use the full [assembly guide](../documentation-site/docs/hardware/wiring.md),
[BOM](../documentation-site/docs/hardware/bill-of-materials.md),
[pinout](../documentation-site/docs/hardware/pinout.md) and
[power requirements](../documentation-site/docs/hardware/power.md).
The published manual is separate from the firmware installer:
[OpenTag Station documentation](https://76cb.github.io/OpenTag-Station-Docs/hardware/wiring/).

| WT32 EXT board contact | Production connection |
|---:|---|
| 1 / +5V | NFC pin 6; scale supply only if its exact breakout accepts 5 V |
| 2 / GND | NFC pin 7; NAU7802 GND |
| 3 / GPIO10 | NAU7802 SDA, Wire/controller 0, address 0x2A, 400 kHz |
| 4 / GPIO11 | NAU7802 SCL |
| 5 / GPIO12 | NFC pin 1 IRQ |
| 6 / GPIO13 | NFC pin 5 MISO/SDA, Wire1/controller 1, address 0x50, 100 kHz |
| 7 / GPIO14 | NFC pin 3 SCLK/SCL |
| 8 / GPIO21 | Unconnected |

NFC pin 2 CS/BSS and pin 4 MOSI stay **DISCONNECTED**. Close the module's I²C
solder bridge. GPIO uses 3.3 V logic; verify breakout pull-ups. Touch remains on
the separate software GPIO6/5 bus. Identify connector contacts from board labels
and continuity, never cable color or a mirrored photo. Install OpenTag Station
and run the normal production diagnostics for post-assembly checks.

`tools/check_hardware_docs.py` checks source GPIOs, bus ownership, clocks, addresses,
approved tag profile and generated SVGs to prevent silent documentation drift.

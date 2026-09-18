# Production pinout

This table is an electrical board-contact map. For orientation and continuity
checks, use [wiring](wiring.md); do not interpret it as a cable-side left/right view.

| WT32 EXT contact | Board label | GPIO | Production destination | Electrical role |
|---:|---|---:|---|---|
| 1 | +5V | — | NFC pin 6; qualified scale breakout supply | 5 V power |
| 2 | GND | — | NFC pin 7; NAU7802 GND | Common 0 V |
| 3 | EXT_IO1 | 10 | NAU7802 SDA | 3.3 V logic, hardware I²C 0 |
| 4 | EXT_IO2 | 11 | NAU7802 SCL | 3.3 V logic, hardware I²C 0 |
| 5 | EXT_IO3 | 12 | NFC pin 1 IRQ | 3.3 V logic interrupt |
| 6 | EXT_IO4 | 13 | NFC pin 5 MISO/SDA | 3.3 V logic, hardware I²C 1 |
| 7 | EXT_IO5 | 14 | NFC pin 3 SCLK/SCL | 3.3 V logic, hardware I²C 1 |
| 8 | EXT_IO6 | 21 | Unconnected | Unused by station |

| Internal peripheral | Pins / configuration |
|---|---|
| Touch | SDA6, SCL5, IRQ7; address `0x38`, 400 kHz; software I²C port −1 |
| LCD data D0–D7 | 9, 46, 3, 8, 18, 17, 16, 15 |
| LCD controls | WR47, command0, reset4, backlight45 |
| LCD layout | 480 × 320, rotation 1; 40 MHz parallel bus |
| Scale | Address `0x2A`, `Wire` controller 0, 400 kHz |
| NFC | Address `0x50`, `Wire1` controller 1, 100 kHz |

Firmware authority:
[`wt32_sc01_plus_rev_a.hpp`](https://github.com/76cb/OpenTag-Station/blob/main/src/boards/wt32_sc01_plus_rev_a.hpp),
[`nau7802_device.hpp`](https://github.com/76cb/OpenTag-Station/blob/main/src/hardware/scale/nau7802_device.hpp),
and the production Wire/Wire1 bindings. EXT contact numbering follows the
[manufacturer datasheet](https://docs.makehub.tw/wt32-sc01plus/WT32-SC01%2BPLUS%2BDatasheet-V1.5%2BEN.pdf).

`tools/check_hardware_docs.py` compares documented machine-readable pins, addresses,
clocks and controller ownership against those sources. It regenerates the SVG
connection diagrams and rejects stale output. Change source and documentation
together; old development profiles do not override current production settings.

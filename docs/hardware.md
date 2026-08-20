# Hardware assumptions and risks

## Confirmed baseline

| Item | Current basis | Status |
|---|---|---|
| MCU/display | WT32-SC01 Plus, ESP32-S3, 480 × 320 ST7796 | Driver implemented and compiled; hardware test pending |
| Touch | FT6336U-compatible controller at I2C `0x38` | Driver implemented and compiled; hardware test pending |
| Flash/PSRAM | 16 MB flash, QSPI PSRAM configuration | Diagnostics/buffer policy compiled; hardware test pending |
| Scale ADC | NAU7802 at I2C `0x2A` | Driver/service implemented, compiled, and host-tested; hardware test pending |
| Load cell | YZC-133, 5 kg actual/default profile; 2 kg supported | Software implemented and host-tested; physical validation pending |
| NFC frontend | ST25R3916B, SPI, IRQ, reset/control | Mandatory; module/wiring unresolved |
| Tag technology | NFC-V / ISO15693 | Confirmed by current OpenPrintTag specification |

The built-in display/touch and external scale pins are centralized in
`src/boards/wt32_sc01_plus_rev_a.hpp`. They were cross-checked against
[SpoolmanScale at `ea0515a`](https://github.com/Niko11111/SpoolmanScale/commit/ea0515ad92ec2fcb65af8c5f0e2bc1a4d01d305b)
because it uses the same physical platform. No source architecture or NFC code
is reused.

## Phase 1 bring-up behavior

The firmware configures the ST7796 at 40 MHz over the board's 8-bit parallel
bus, rotates its native 320 × 480 panel to 480 × 320 landscape, and configures
the FT6336-compatible controller on I2C port 1 at 400 kHz. Touch coordinates are
range-checked and debounced before reaching LVGL. Backlight PWM runs on the
verified GPIO/channel and supports brightness, idle dimming, explicit sleep,
and touch wake.

LVGL requests two 480 × 40-line RGB565 buffers from PSRAM. Rendering remains
available with one PSRAM buffer if the second allocation fails, or a 480 ×
20-line internal-memory buffer if PSRAM allocation fails entirely. The hardware
diagnostics screen reports the actual allocation path.

NVS stores boot count, boot-pending health, and a saturated crash streak. A
factory-blank LittleFS partition is formatted once and marked provisioned in
NVS; later mount failures are reported without automatic reformatting. The
firmware detects the coredump partition and displays reset reason, uptime, heap,
minimum heap, and PSRAM totals. These behaviors are compiled, not yet physically
verified.

## Known board signals

| Function | Signal |
|---|---:|
| NAU7802 SDA | GPIO 10 |
| NAU7802 SCL | GPIO 11 |
| Touch SDA | GPIO 6 |
| Touch SCL | GPIO 5 |
| Touch IRQ | GPIO 7 |
| LCD backlight | GPIO 45 |
| LCD WR / command / reset | GPIO 47 / 0 / 4 |
| LCD D0..D7 | 9, 46, 3, 8, 18, 17, 16, 15 |

These facts do **not** establish that any remaining WT32 header signal is safe
or suitable for the NFC reader. ESP32-S3 strapping, flash/PSRAM, display, touch,
USB, SD, and board-revision conflicts must be checked before assignment.

## ST25R3916B integration risk

ST documents the ST25R3916B as supporting NFC-V up to 53 kbit/s, a 512-byte
FIFO, SPI up to 10 Mbit/s, IRQ/control GPIO, field/RSSI measurement, and
high-output antenna drive. RFAL requires software-controlled chip select,
interrupt handling, reset/control, monotonic timers, and protected bus/IRQ
access in a multithreaded system.

Those ESP32 primitives are implemented with injected pins, clock, SPI mode, and
signal polarities. The implementation refuses incomplete configuration and uses
a bounded recursive bus mutex plus an IRQ latch/acknowledgement path. No
electrical defaults are instantiated because those values depend on the exact
module checkpoint below.

OpenPrintTag's current physical specification expects a circular reader antenna
72–80 mm in diameter, 13.56 MHz resonance, typically 1 W RF output (1.6 W max),
parallel and approximately concentric with the spool. A breakout board that only
proves register communication is not enough; the antenna/module must meet the
physical read-distance use case around the scale and LCD.

## Required NFC hardware checkpoint

Before assigning pins or enabling `OPENTAG_ENABLE_ST25R3916B`, provide or verify:

1. exact module/board manufacturer and revision;
2. schematic and connector pinout;
3. supply and I/O voltage requirements;
4. SPI/I2C selection strapping and any MCU/bus-select pin;
5. CS, SCK, MOSI, MISO, IRQ, reset, and power/enable behavior;
6. IRQ polarity/electrical type and required pull resistor;
7. oscillator and antenna/matching network already present on the module;
8. safe unused WT32-SC01 Plus header GPIOs for the exact board revision;
9. measured antenna tuning with the final enclosure, load cell, display, and
   representative spools.

Until then, all NFC pins remain `-1`, RFAL is not vendored, and the boot firmware
reports NFC disabled.

## Scale assumptions

The driver uses the NAU7802 at 3.0 V LDO, gain 128, and 10 samples/second on the
second ESP32 I2C controller so it cannot reconfigure the touch bus. Startup,
revision detection, raw reads, internal calibration, and disconnect recovery are
implemented with bounded waits.

The actual assembly specifies the 5 kg YZC-133, and fresh or uncalibrated
configuration therefore defaults to model `YZC-133`, rated capacity 5,000 g,
and overload ratio 1.10. The 2 kg YZC-133 remains supported. Configuration
schema 3 stores those values in the separate `scale_profile` fields
`load_cell_model`, `rated_capacity_grams`, and `overload_ratio`. Calibration
continues to store zero offset, signed counts-per-gram factor, reference weight,
capacity, and schema; the rollback-compatible NVS mirror also retains its CRC.

The profile object is an additive schema-3 field. When it is absent from an
older document, software infers rated capacity from an existing calibration,
preserving deployed 2 kg configurations. Older standalone NVS calibration is
handled the same way. If neither a profile nor calibration exists, the 5 kg
default applies. A calibration is accepted only when its capacity matches the
profile; changing model or rated capacity requires a new tare and reference
calibration. An overload-ratio-only change preserves calibration.

The software overload diagnostic is asserted when the absolute calibrated
weight is strictly greater than rated capacity multiplied by overload ratio:
5,500 g for the default 5 kg/1.10 profile and 2,200 g for a 2 kg/1.10 profile.
Raw ADC saturation is reported independently. These diagnostics do not establish
a safe mechanical overload limit or prevent damage above rated capacity.

The 10-sample window and stability/creep thresholds are safe software defaults,
not measured claims. Profile behavior is implemented and host-tested, but the
actual 5 kg load cell has not been physically validated. Final values,
calibration accuracy, repeatability, and mechanical overload behavior require
the hardware procedure in [scale.md](scale.md).

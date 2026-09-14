# Hardware assumptions and risks

## Phase 11 release status

No physical validation is implied by compilation or host tests. The executable
board, scale, NFC, integration, recovery, OTA, resource, and soak checklist is
in [release-validation.md](release-validation.md).

## Confirmed baseline

| Item | Current basis | Status |
|---|---|---|
| MCU/display | WT32-SC01 Plus, ESP32-S3, 480 × 320 ST7796 | Driver implemented and compiled; hardware test pending |
| Touch | FT6336U-compatible controller at I2C `0x38` | Driver implemented and compiled; hardware test pending |
| Flash/PSRAM | 16 MB flash, QSPI PSRAM configuration | Diagnostics/buffer policy compiled; hardware test pending |
| Scale ADC | NAU7802 at I2C `0x2A` | Dedicated GPIO10/11 transport physically validated; calibration/accuracy pending |
| Load cell | YZC-133, 5 kg actual/default profile; 2 kg supported | Software implemented and host-tested; physical validation pending |
| NFC frontend | ELECHOUSE NFC_ST25R3916B; 5 V module, 3.3 V logic, integrated antenna, SPI/I2C | Dedicated GPIO13/14 `Wire1` transport, chip ID, GPIO12 IRQ, NFC-V inventory, full read-only memory, and one-time guarded initialization physically validated; production read-only recognition, removal/reinsertion and stationary soak passed |
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

The RGB565 boundary is explicit: LVGL uses `LV_COLOR_16_SWAP=0`, while
LovyanGFX uses `setSwapBytes(true)` before sending pixels to the parallel
ST7796 bus. Panel inversion remains enabled, the LovyanGFX RGB-order flag
remains disabled, and rotation 1 remains the 480 x 320 landscape mapping.

For physical panel bring-up, build and flash the opt-in diagnostic environment:

```console
.venv/bin/pio run --environment wt32-sc01-plus-display-test
```

The display-test environment replaces the normal UI with labeled red, green, blue, white,
black, yellow, cyan, and magenta swatches, an eight-step grayscale ramp, a
full-panel border, true center marker, edge labels, and live touch coordinates.
The normal `wt32-sc01-plus` environment remains unchanged as the application
and web-flasher build. Correct physical color, inversion, orientation, edge
alignment, and touch mapping remain **UNVERIFIED** until checked on the panel.

LVGL requests two 480 × 40-line RGB565 buffers from PSRAM. Rendering remains
available with one PSRAM buffer if the second allocation fails, or a 480 ×
20-line internal-memory buffer if PSRAM allocation fails entirely. The hardware
diagnostics screen reports the actual allocation path.

For the physical NAU7802/ST25R3916B dual-I2C test, build the separate
`wt32-sc01-plus-i2c-test` environment. It uses GPIO10 SDA / GPIO11 SCL for the
NAU7802 on `Wire` and GPIO13 SDA / GPIO14 SCL for the ST25R3916B on `Wire1`, at
100 kHz each, with GPIO12 for NFC IRQ. The normal `wt32-sc01-plus` target
uses the same pinned ELECHOUSE object API in the shared backend/NFC task;
it does not start the diagnostic AP or page. See [production NFC](production-nfc.md).

NVS stores boot count, boot-pending health, and a saturated crash streak. A
LittleFS partition is always mounted first with formatting disabled and by its
explicit `littlefs` partition label. If that mount fails, a first format is
authorized only when the station has no provisioning/reset marker and a complete
read of the labeled partition proves every byte is erased (`0xFF`). Before
formatting, the station durably writes `fsFormatPending` in the separate control
NVS namespace, so power loss during the first format can retry without weakening
the guard. After a successful mount it durably records `fsProvisioned`, then
clears the pending intent.

Any mount failure after provisioning, or on a partition that is not proven fully
erased, preserves the data and never triggers automatic formatting. Factory
reset removes only station configuration documents and restores the no-format
guard; it does not format LittleFS. The firmware also detects the coredump
partition and displays reset reason, uptime, heap, minimum heap, and PSRAM
totals. These behaviors are compiled, not yet physically verified.

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

## Production ST25R3916B integration

The physically validated ELECHOUSE module uses Wire1, GPIO13 SDA, GPIO14 SCL,
GPIO12 IRQ, address 0x50 and 100 kHz. Scale retains Wire GPIO10/11, address
0x2A and 100 kHz. Touch uses LovyanGFX software I2C on GPIO6/5. Both hardware
I2C controllers are reserved for scale and NFC.

The module has no external reset or power-enable signal. Pinned ELECHOUSE
RFAL uses the chip's Set Default command. Legacy SPI abstractions remain only
for portable tests and future boards; they are not a second production path.
The shared 16384-byte backend task owns RFAL, NFC-V reads, guarded OpenPrintTag writing, decode
and backend HTTP sequentially. No dedicated NFC task is created.

Production inventory, geometry, full reads, decode, reboot persistence,
removal/reinsertion and stationary soak have passed. Tag mutation is post-MVP.
Use [release validation](release-validation.md) for the remaining integrated
physical test, rather than repeating the historical bring-up checkpoints.
## Scale assumptions

The driver uses the NAU7802 at 3.0 V LDO, gain 128, and 10 samples/second on
`Wire` (ESP32 I2C controller 0). NFC owns `Wire1` (controller 1); touch uses
LovyanGFX software I2C port -1 on GPIO6/5. Startup,
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

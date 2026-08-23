# NFC hardware bring-up checkpoint

Status as of 2026-08-23: **software-only; physical hardware unverified**.

The supported reader architecture is the ST25R3916B with ST RFAL and NFC-V /
ISO15693 tags. No substitute controller or NFC stack is supported. The normal
`wt32-sc01-plus` factory build keeps `OPENTAG_ENABLE_ST25R3916B=0`, all NFC
board-profile pins remain `-1`, and no NFC task or RF field is created.

## Identified module authority

The exact reader is the ELECHOUSE **ST25R3916B NFC Module**, SKU
`NFC_ST25R3916B`. Module-level electrical facts come from the
[ELECHOUSE product page](https://www.elechouse.com/product/st25r3916b-nfc-module/),
[module data sheet](https://www.elechouse.com/wp-content/uploads/2026/07/ST25R3916B_NFC_Module_Datasheet.pdf),
and [module schematic](https://www.elechouse.com/wp-content/uploads/2026/01/st25r3916_schematic.pdf).

The module accepts **5 V** power, uses **3.3 V host logic**, includes its PCB
antenna and matching network, defaults to SPI, and supports I2C after the
ELECHOUSE I2C-selection solder bridge is closed. Its 1.25 mm seven-pin connector
is authoritative in this order:

| Pin | ELECHOUSE signal |
|---:|---|
| 1 | IRQ |
| 2 | CS / BSS |
| 3 | SCLK / SCL |
| 4 | MOSI |
| 5 | MISO / SDA |
| 6 | +5V |
| 7 | GND |

There is **no external RESET signal** and **no power-enable signal** on this
connector. Firmware must not invent either GPIO. The module is quiesced through
the ST25R3916B Set Default direct command and default power-down register state;
its 5 V supply remains present.

## Implemented software boundary

`FrontendBackend` performs bounded, allocation-free-on-success direct register
transactions through the injected ESP32 RFAL platform. It validates the silicon
identity before any RFAL call, rejects `0x00`, `0xFF`, and an unexpected product
code, and validates the IRQ path by observing the oscillator-stable interrupt.
All failures leave the RF field off. A board with a real power-enable line is
powered down; the fixed-power ELECHOUSE module is returned to the chip default
power-down state without pretending that its 5 V supply was switched off.

The register contract is taken from ST's
[ST25R3916B data sheet, DS13541 Rev 11](https://www.st.com/resource/en/datasheet/st25r3916b.pdf):

- SPI is MSB-first, mode 1 (`CPOL=0`, `CPHA=1`), with active-low BSS and a
  maximum clock of 10 MHz;
- identity register `0x3F` contains product code `00110b` in bits 7:3 and the
  silicon revision in bits 2:0;
- interrupt registers `0x1A` through `0x1D` clear when read;
- enabling the oscillator with operation-control register `0x02`, bit 7,
  produces the oscillator-stable interrupt in main interrupt register `0x1A`,
  bit 7;
- the silicon IRQ output is active high.

The backend now distinguishes external-reset boards from modules that use the
ST25R3916B Set Default direct command (SPI byte `0xC1`). Likewise it distinguishes
a real external power-enable GPIO from an always-powered module. Configuration
that declares an external line still fails unless that real GPIO exists.

The read-only NFC-V domain code now has bounded UID normalization, inventory
state for zero/one/multiple tags, removal/replacement detection, and existing
4,096-byte geometry/read limits. This code is host-tested but is not connected
to physical RFAL inventory while the vendor and wiring gates below are open.

## RFAL acquisition gate

ST's official product is
[STSW-ST25RFAL002](https://www.st.com/en/embedded-software/stsw-st25rfal002.html).
Its source delivery is request-controlled. No authoritative archive was
obtained during this work, so the archive filename, untouched SHA-256, internal
RFAL version, complete delivered SLA0051 license, redistribution decision, and
exact import list cannot be recorded. No RFAL source was vendored and no mirror
was substituted.

Before RFAL import, obtain the official ST delivery and record every item above
in `third_party/ST_RFAL/README.md`. Upstream files should remain unmodified where
the adapter under `src/platform/rfal/` can provide the required hooks.

## WT32 transport decision

### Option A — shared I2C (**recommended, not yet enabled**)

The ST25R3916B seven-bit I2C address is **`0x50`**; the NAU7802 address is
**`0x2A`**, so there is no address collision. Both devices support 100 kHz and
400 kHz operation, including the scale bus's current 400 kHz rate. The ELECHOUSE
schematic provides 10 kΩ pull-ups from SCL and SDA to the module's on-board
3.3 V rail. Do not add another pull-up pair blindly: verify the effective
parallel resistance and rise time with the pull-ups already present on the
actual NAU7802 board. Both bus lines must remain 3.3 V logic.

ELECHOUSE's [I2C quick start](https://www.elechouse.com/st25r3916-esp32-i2c-quick-start/)
requires closing the module pad marked I2C and connects only SDA, SCL, IRQ, 5 V,
and GND. ST's [RFAL user manual](https://www.st.com/resource/en/user_manual/um2890-rfnfc-abstraction-layer-rfal-stmicroelectronics.pdf)
defines an I2C communication/platform adaptation, so preserving RFAL does not
require SPI.

The least-invasive intended wiring is:

| ELECHOUSE pin | Signal | WT32 connection | Status |
|---:|---|---|---|
| 1 | IRQ | GPIO12 | proposed only; not assigned in firmware |
| 2 | CS / BSS | no host GPIO in ELECHOUSE I2C configuration | module-selection procedure not yet physically verified |
| 3 | SCL | GPIO11, shared with NAU7802 | proposed only |
| 4 | MOSI | no host GPIO in I2C configuration | unused by the documented I2C hookup |
| 5 | SDA | GPIO10, shared with NAU7802 | proposed only |
| 6 | +5V | WT32 EXT 5V | proposed only |
| 7 | GND | WT32 EXT GND | proposed only |

This mapping is **not active**. The current scale owner performs all `Wire`
operations without a cross-task bus lock. Before NFC can be enabled, add one
bounded shared-I2C arbitration contract used by both the NAU7802 owner and the
RFAL I2C platform adapter. The NFC path must reuse the already-configured bus,
not independently call `Wire.begin()` or change its clock while scale traffic is
active. The exact acquired ST RFAL release must also be verified to build its
I2C path under the repository's vendor gate.

### Option B — shared on-board SD SPI (**not recommended**)

WT32 GPIO39 SCK, GPIO38 MISO, and GPIO40 MOSI can electrically form a shared SPI
bus with separate chip-selects; GPIO41 is the on-board microSD chip-select. An
NFC CS and IRQ would still require two EXT GPIOs plus a bus-wide lock and
coordinated `SPIClass` ownership. However, GPIO38/39/40 are not on the EXT
connector. The WT32 documentation identifies them only at the on-board microSD
socket and does not document a safe NFC breakout/test-point connection.
Access would therefore require an SD-socket adapter or internal board soldering,
would complicate SD ownership, and offers no RFAL reliability advantage over
I2C. It is rejected for this build unless later hardware evidence provides a
non-invasive, documented access point and a compelling need.

### Remaining enable gates

1. acquire and record the authoritative ST RFAL package, license, version, and
   checksum;
2. implement and host-test the bounded RFAL I2C adapter plus shared scale/NFC
   bus arbitration;
3. physically verify the ELECHOUSE I2C solder bridge, pull-up network, 5 V
   supply, 3.3 V bus levels, and proposed GPIO12 IRQ connection;
4. only then assign transport pins in the board profile and create an opt-in NFC
   test image.

## Physical acceptance procedure

This procedure remains blocked until the RFAL I2C adapter and shared-bus lock
are implemented and the proposed mapping is activated:

1. With power removed, close only the ELECHOUSE pad marked I2C and continuity-
   check the seven-pin connector against the module schematic. Do not add reset
   or power-enable wiring.
2. Measure the combined SDA/SCL pull-up resistance, confirm both lines pull to
   3.3 V, and verify the 5 V supply and common ground before attaching the host.
3. Connect SDA to GPIO10, SCL to GPIO11, IRQ to the board-profile-selected EXT
   pin (proposed GPIO12), +5V to EXT 5V, and GND to EXT GND only after the
   firmware gates above are complete.
4. Build a dedicated `wt32-sc01-plus-nfc-test` environment. Do not alter the
   normal factory enable flag.
5. At first power, verify normal station and scale operation and check module
   current/temperature; disconnect immediately if either is abnormal.
6. Require a valid I2C response at `0x50`, successful Set Default/identity
   initialization, and a real oscillator-stable IRQ transition before RFAL or
   field enable.
7. Require RFAL initialization and field enable; register success alone is not
   evidence of antenna tuning.
8. With zero tags, verify the reader, scale sampling, browser, and network remain
   responsive.
9. Present one known NFC-V tag; verify stable canonical `E0...` UID, reported
   geometry, and bounded raw read. Remove it and verify absence.
10. Reinsert the same tag 20 times and require the identical UID without a
    lockup. Then test multiple tags and require a distinct multiple-tag state.
11. With one Chrome tab open, verify REST self-test, WebSocket health, no
    network wedge/ENOMEM, and capture free/minimum/largest internal heap plus
    HTTP, scale, and NFC task stack margins.

No OpenPrintTag physical-validation claim is permitted until an actual
OpenPrintTag image has been read. No NFC write, format, protection, password,
AFI, or DSFID operation is part of this phase.

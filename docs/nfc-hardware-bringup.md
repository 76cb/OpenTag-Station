# NFC hardware bring-up checkpoint

Status as of 2026-08-23: **software-only; physical hardware unverified**.

The supported reader architecture is the ST25R3916B with ST RFAL and NFC-V /
ISO15693 tags. No substitute controller or NFC stack is supported. The normal
`wt32-sc01-plus` factory build keeps `OPENTAG_ENABLE_ST25R3916B=0`, all NFC
board-profile pins remain `-1`, and no NFC task or RF field is created.

## Implemented software boundary

`FrontendBackend` performs bounded, allocation-free-on-success direct register
transactions through the injected ESP32 RFAL platform. It validates the silicon
identity before any RFAL call, rejects `0x00`, `0xFF`, and an unexpected product
code, and validates the IRQ path by observing the oscillator-stable interrupt.
All failures leave the RF field off and power off where the module has a
controllable enable input.

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

Module-level reset, power-enable, IRQ, CS, timing, supply, and I/O behavior are
still injected rather than inferred from the bare silicon.

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

## Hardware checkpoint

The exact reader module and WT32-SC01 Plus rev A header contract are absent.
Provide all of the following before assigning a GPIO or creating the opt-in NFC
firmware environment:

1. reader module manufacturer, exact product, and board revision;
2. clear photographs of both module sides, connector labels, and populated
   straps, plus its schematic or authoritative pinout;
3. module supply voltage and I/O voltage;
4. SCK, MOSI, MISO, CS, IRQ, RESET, and optional power/enable labels;
5. SPI/bus-select strap settings;
6. module-level IRQ, RESET, CS, and power-enable polarities/electrical types;
7. whether the oscillator, antenna, and matching network are already present;
8. the exact WT32-SC01 Plus rev A header schematic/pinout proving exposed pins
   are free of flash, PSRAM, display, touch, SD, USB/JTAG, and boot straps.

Until those facts exist, the only honest mapping is:

| Reader signal | WT32-SC01 Plus rev A pin |
|---|---|
| Supply / I/O supply | TBD — module contract required |
| Ground | TBD — connector contract required |
| SCK | TBD |
| MOSI | TBD |
| MISO | TBD |
| CS | TBD |
| IRQ | TBD |
| RESET | TBD |
| Power/enable or bus-select | TBD |

Do not wire or power the reader from this table.

## Physical acceptance procedure

This procedure is intentionally blocked at step 1 until the checkpoint is
resolved:

1. With all power removed, verify the authoritative module-to-WT32 pin table,
   supply/I/O voltage, ground, SPI, IRQ, RESET, enable, and bus-select straps.
2. Build a dedicated `wt32-sc01-plus-nfc-test` environment only after those
   facts are committed in the board profile and the official RFAL import is
   reproducible. Do not alter the normal factory enable flag.
3. At first power, verify normal station boot and check current/temperature;
   disconnect immediately if either is abnormal.
4. Require the serial sequence power, reset, SPI product/revision, and
   `identity: VALID`. Stop before RFAL or RF field on any failure.
5. Require a real oscillator-stable IRQ count/acknowledgement transition.
6. Require RFAL initialization and field enable; register success alone is not
   evidence of antenna tuning.
7. With zero tags, verify the reader and browser remain responsive.
8. Present one known NFC-V tag; verify stable canonical `E0...` UID, reported
   geometry, and bounded raw read. Remove it and verify absence.
9. Reinsert the same tag 20 times and require the identical UID without a
   lockup. Then test multiple tags and require a distinct multiple-tag state.
10. With one Chrome tab open, verify REST self-test, WebSocket health, no
    network wedge/ENOMEM, and capture free/minimum/largest internal heap plus
    HTTP and NFC task stack margins.

No OpenPrintTag physical-validation claim is permitted until an actual
OpenPrintTag image has been read. No NFC write, format, protection, password,
AFI, or DSFID operation is part of this phase.

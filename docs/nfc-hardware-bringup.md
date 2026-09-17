# NFC hardware bring-up checkpoint

## DEVELOPMENT HISTORY — NOT SHIPPED

This is an archived engineering record, not installation guidance. The standalone
firmware, build environment and distribution are removed. Install normal OpenTag
Station and use [production wiring](wiring.md) and [release acceptance](releasing.md).


Production NFC inventory, full reads, OpenPrintTag decode, removal/reinsertion and stationary soak have passed. The historical diagnostic evidence below records how the transport was brought up; it is not another acceptance gate. Production now uses the same Wire1 mapping on the shared backend task, with guarded OpenPrintTag writing. Use [production NFC](production-nfc.md) for current ownership and [release validation](release-validation.md) for the one integrated physical procedure.

## Proven transport

The physical WT32-SC01 Plus test completed with both buses at 100 kHz:

| Device | Controller | SDA | SCL | IRQ | Address | Result |
|---|---|---:|---:|---:|---:|---|
| NAU7802 | `Wire` | GPIO10 | GPIO11 | — | `0x2A` | ACK, zero bus errors |
| ST25R3916B | `Wire1` | GPIO13 | GPIO14 | GPIO12 | `0x50` | ACK, chip ID and IRQ PASS, zero bus errors |

Both buses remained operational after the bounded coexistence test and 297
scale samples were collected. The earlier NFC SDA/SCL-low result was caused by
the old harness/splice path; replacing that path resolved it. Do not collapse
these devices onto one controller as part of this phase.

The exact reader is the ELECHOUSE **ST25R3916B NFC Module**, SKU
`NFC_ST25R3916B`. Its I2C-selection solder bridge must be closed. The module is
powered from 5 V, uses 3.3 V host logic, and has no external reset or
power-enable signal. Firmware must not invent either GPIO.

## Pinned RFAL diagnostic

The diagnostic vendors the two upstream library directories unchanged from
[`wilson-elechouse/ST25R3916` commit `16eb6c7`](https://github.com/wilson-elechouse/ST25R3916/commit/16eb6c7fb13e502d320924040d768a9e564209b2).
The import record is in
[`third_party/ELECHOUSE_ST25R3916/README.md`](../third_party/ELECHOUSE_ST25R3916/README.md).
PlatformIO exposes those copies only to `wt32-sc01-plus-i2c-test`.

The diagnostic uses ELECHOUSE's object ownership directly:

```cpp
RfalRfST25R3916Class reader(&Wire1, Board::diagnostic_nfc_interrupt);
RfalNfcClass nfc(&reader);
```

It initializes RFAL and the NFC-V poller, enables the RF field only around each
bounded inventory round, performs NFC-V presence and collision resolution, and
then disables the field with scoped cleanup. Every exit after successful field
enable calls `reader.rfalFieldOff()`. A raw `0x50` probe and chip-ID check run
immediately after inventory so a transport loss cannot be classified as normal
tag absence.

Collision resolution is authoritative for the final tag count. ELECHOUSE maps
an NFC-V inventory timeout to `ERR_NONE` with zero devices, so this is a healthy
result:

```text
ISO15693 inventory   PASS
Tag detected         NO
Devices found        0
First failing stage  NONE
```

For one device, the RFAL wire-order UID is normalized with the existing
`Uid::from_wire_lsb_first(...)` implementation, including its canonical `E0`
prefix check. Only the diagnostic presentation adds colon separators.

Touch input remains disabled because it would use the ESP32-S3 controller
assigned to NFC. The RGB display does not use that I2C controller. Scale
sampling continues on `Wire` during the bounded RF test; the diagnostic never
takes ownership of or reconfigures GPIO10/11.

## Bench acceptance

With no tag present, require all PASS/NONE fields below and `Devices found 0`.
With one known NFC-V tag present, require `Devices found 1` and its normalized
eight-byte UID:

```text
Scale 0x2A           ACK
NFC 0x50             ACK
NFC chip ID          PASS
NFC IRQ              PASS
RFAL initialize      PASS
NFC-V poller         PASS
RF field             PASS
ISO15693 inventory   PASS
Tag detected         YES/NO
Devices found        1/0
UID                   E0:xx:xx:xx:xx:xx:xx:xx
NFC bus errors       0
Scale bus errors     0
Scale after test     PASS
NFC after scale      PASS
First failing stage  NONE
```

For the known-tag test:

1. Keep the tag present through repeated inventory rounds and require the same
   normalized eight-byte UID each time.
2. Remove it and require a clean transition to inventory PASS, zero devices,
   and no failing stage.
3. Reinsert it and require recovery of the identical normalized UID.
4. Require scale sampling and both post-test transport checks to remain healthy
   throughout.

The completed follow-up also proved an 80 × 4 geometry and two matching
320-byte reads of UID `[physical tag UID omitted]`; the blank image checksum was
`97B79EC5`. The one-time guarded initializer subsequently passed and the stable
initialized image checksum is `9E639911`. Its UI and POST route are now
disabled. Production read-only recognition and soak subsequently passed; the current integrated acceptance is in [release validation](release-validation.md).

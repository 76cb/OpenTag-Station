# Blank NFC-V OpenPrintTag initialization diagnostic

This is an opt-in bench diagnostic. It does not enable the production NFC path,
does not initialize a tag at boot, and does not provide a force or
reinitialization path. Its only write operation is the explicit blank-tag
initialization transaction exposed by the `wt32-sc01-plus-i2c-test` firmware.

## Pinned references

- OpenPrintTag specification and Python utilities at
  [`7e09cc38df1c8e7824a67f5b1ae93071f52519ad`](https://github.com/OpenPrintTag/openprinttag-specification/tree/7e09cc38df1c8e7824a67f5b1ae93071f52519ad).
  The image algorithm follows `utils/nfc_initialize.py`; its NFC-V configuration
  is `data/config_nfcv.yaml`, with MIME type `application/vnd.openprinttag` and
  configuration root `nfcv`. The upstream tests use
  `--size=312 --aux-region=32` for their SLIX2-shaped vectors.
- [NXP ICODE SLIX2 data sheet, revision 4.2](https://www.nxp.com/docs/en/data-sheet/SL2S2602.pdf),
  sections 9.2 and 9.5.3.6. The IC reports 80 four-byte blocks, but only blocks
  0 through 78 are user memory. Block 79 stores the 16-bit counter and its
  protection flag.
- The vendored [ELECHOUSE reference at
  `16eb6c7`](https://github.com/wilson-elechouse/ST25R3916/commit/16eb6c7fb13e502d320924040d768a9e564209b2), especially
  `ESP32_I2C_icode_slix2_read_write_test`. It uses the RFAL object API,
  selects standard versus extended single-block commands by block index, and
  explicitly avoids the final SLIX2 counter block.

## Writable-range decision

The diagnostic advertises and writes **312 bytes, blocks 0 through 77**.

- 320 bytes is unsafe because it includes counter block 79.
- 316 bytes covers all 79 user blocks, but the Type 5 Capability Container
  represents capacity in eight-byte units and therefore cannot advertise 316.
- Rounding down gives 312 bytes. This matches the pinned upstream test vectors
  and leaves both block 78 (the unadvertised four-byte remainder) and block 79
  untouched.
- 304 bytes is a valid smaller example in the upstream documentation, but it is
  not necessary for this tag and discards another complete eight-byte CC unit.

The generated image is exactly 312 bytes: `E1 40 27 01`, an extended NDEF TLV,
one `application/vnd.openprinttag` MIME record, a definite CBOR metadata map
that records the auxiliary offset, empty main and auxiliary CBOR maps,
zero-filled allocation space, and a TLV terminator. No URI, material data, or
invented values are added. The initializer arguments request a 32-byte
auxiliary region. Upstream's block-alignment rule places it at payload offset
234 (absolute tag byte 276); because the payload ends at tag byte 310, the
decoded aligned auxiliary allocation is 35 bytes and its empty CBOR map uses
one byte. The pinned golden image has SHA-256
`9abc9642bf66ce1e91dfd765932d279b3b31850e41e0c58ff789aa525bbd418d`
and diagnostic FNV-1a checksum `6B6EABF1`.
With the physically verified zero-filled eight-byte preserved tail, the full
320-byte target checksum is `9E639911`; the preview calculates this from the
actual preserved tail rather than assuming it.

The read codec remains pinned to its separately proven revision. The initializer
records the new reference commit independently because later upstream field-key
changes have not yet been adopted by the production decoder.

## Diagnostic task stack

PlatformIO `espressif32@6.13.0` resolves to Arduino-ESP32 2.0.17
(`framework-arduinoespressif32` package `3.20017.241212+sha.dcc1105b`). That
core creates `loopTask` with 8,192 bytes by default. Its `Arduino.h` exposes
`SET_LOOP_TASK_STACK_SIZE`, backed by the core's weak
`getArduinoLoopTaskStackSize()` function, and its bundled stack-size example
uses 16 KiB.

The merged PR #20 diagnostic added a compiler-reported 7,936-byte
`Codec::decode` frame beneath 1,696 bytes of initialization preparation and the
Arduino setup call chain. That exceeded the default task before the normal
diagnostic could start. The diagnostic now uses the supported 16,384-byte
override. Its output-parameter codec overload fills heap-backed `DecodedTag`
storage at startup, read-only preview decoding, and post-write verification,
while the original value-returning API remains available.

With the pinned compiler, the audited static path is 5,536 bytes for boot decode
and 5,616 bytes for post-write decode. CI runs
`tools/check_diagnostic_stack_usage.py` against the diagnostic `.su` files and
requires at least 4,096 bytes of compiler-estimated headroom. This is a build
regression guard, not a substitute for hardware high-water measurements.
Serial reports `uxTaskGetStackHighWaterMark(nullptr)` at setup entry, after
display initialization, before image preparation, after generation, after the
startup decode, after web-server startup, and before/after the post-write decode.

## Authorization and transaction

The diagnostic boots and completes the existing read-only test first. It offers:

- `GET /api/v1/openprinttag/preview` for the current plan and safety status.
- `GET /api/v1/openprinttag/image` for the generated 312-byte binary.
- `POST /api/v1/openprinttag/initialize` for the one explicit write action.

The POST body must contain the exact canonical UID, the checksum of the current
full 320-byte image, and confirmation text `INITIALIZE`. The device then performs
a fresh RF preflight. It requires exactly one matching `E0:04:01` tag, unchanged
80-by-4 geometry, a matching before-checksum, zeros throughout bytes 0 through
311, and an unlocked status for every changed block.

Writes use a bounded `WritePlan`. Before each block, inventory must still return
the same sole UID. Each standard/extended single-block write is followed by an
immediate same-block readback comparison. The first failure stops the transaction;
there is no retry or recovery write pass. Scope-based RF-field cleanup runs on
all exits. Blocks 78 and 79 are never written, and the code issues no lock,
AFI, DSFID, EAS, password, privacy, or protect-page command.

After all block writes, the device reads the complete 320-byte memory again,
compares it to the intended image (including the preserved tail), decodes it
with the local OpenPrintTag codec, requires the auxiliary region and successful
empty main/auxiliary CBOR-map decoding, and runs the post-RF I2C/chip-ID health
check.

## Stack-overflow fix bench acceptance (no write)

1. Flash the pull-request `opentag-nfc-v-diagnostic-pr` artifact.
2. Confirm boot does not reset, the display remains on, the
   `OpenTag-I2C-Test` access point appears, and the diagnostic remains stable.
3. Confirm the existing read-only diagnostic still passes with UID
   `E0:04:01:08:66:27:D8:D4`, geometry 80 × 4, full-image checksum `97B79EC5`,
   both bus error counts zero, and scale/NFC post-test health passing.
4. Open `http://192.168.4.1` and require the initialization preview to become
   `READY`, with the 32-byte requested auxiliary allocation, the 35-byte aligned
   encoded region at tag offset 276, and target checksum `9E639911`.
5. Record the loop-task high-water checkpoints for the physical result. Do not
   press the initialization button or issue the initialization POST in this
   stack-overflow fix test.

## Future initialization write acceptance

This procedure is not part of the stack-overflow fix PR. Run it only after that
PR passes the no-write bench acceptance above and a physical write is explicitly
authorized.

1. Flash the pull-request `opentag-nfc-v-diagnostic-pr` artifact.
2. Confirm the existing read-only diagnostic passes with the expected UID,
   geometry, before checksum, bus health, and initialization preview `READY`.
3. Open `http://192.168.4.1`, review the initialization preview, press the
   initialization button, accept the first UID/checksum warning, and type the
   second exact confirmation `INITIALIZE`.
4. Require the preview to report the 32-byte requested auxiliary allocation,
   the 35-byte aligned encoded region at tag offset 276, and target checksum
   `9E639911`. Then require image generation, authorization, blank preflight,
   lock status, block writes, immediate block verifies, full-image verify,
   post-write decode, RF-field disable, and post-write transport health to pass.
   No block above 77 may be reported as written.
5. Download the post-write raw dump. Require bytes 0 through 311 to equal the
   preview image, bytes 312 through 319 to equal their before values, and a
   stable full-image checksum across two subsequent read-only runs.
6. Reboot the diagnostic, then remove and reinsert the tag. Require the same
   normalized UID, the same post-write checksum, and `Current OpenPrintTag
   decode: PASS` in the fresh preview without another write.

Any non-zero byte in the initialization range must produce exactly
`INITIALIZATION REFUSED: TAG IS NOT BLANK`. A UID/checksum/geometry/lock mismatch,
write error, readback difference, final-image difference, decode error, transport
error, or RF-field-off error is a failed bench result and must name its first
failing stage and block where applicable.

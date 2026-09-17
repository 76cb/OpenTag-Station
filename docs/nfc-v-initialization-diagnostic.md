# NFC-V OpenPrintTag read-only diagnostic

## DEVELOPMENT HISTORY — NOT SHIPPED

This is an archived engineering record, not installation guidance. The standalone
firmware, build environment and distribution are removed. Install normal OpenTag
Station and use [production wiring](wiring.md) and [release acceptance](releasing.md).


This is an opt-in diagnostic alongside the production NFC path with its separate guarded writer. It
does not initialize a tag at boot, and does not expose a force,
reinitialization, or other NFC write action. The one-time blank-tag
initialization test has passed physical acceptance. Its UI control is removed,
its POST route and loop request processing are behind the compile-time-false
`diagnostic_initialization_write_enabled` gate, and subsequent bench work is
read-only.

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

The retained reference image covers **312 bytes, blocks 0 through 77**.

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

With the pinned compiler after the read-only write gate is applied, the audited
static path is 5,520 bytes for boot decode and a conservative 5,264 bytes for
the retained decode path. CI runs
`tools/check_diagnostic_stack_usage.py` against the diagnostic `.su` files and
requires at least 4,096 bytes of compiler-estimated headroom. This is a build
regression guard, not a substitute for hardware high-water measurements.
Serial reports `uxTaskGetStackHighWaterMark(nullptr)` at setup entry, after
display initialization, before image preparation, after generation, after the
startup decode, after web-server startup, and before/after the post-write decode.

The HTTP server task has an independent budget. On the physically failing PR #21
build, `preview_handler` retained its 3,552-byte frame while the nonblank preview
added the 336-byte preview frame and 4,848-byte decode path. The resulting 8,736
project bytes already exceeded the configured 6,144-byte `httpd` stack before
ESP-IDF dispatch frames were counted. `api_handler` independently used 3,616
bytes, primarily because each handler kept a 3,072-byte JSON buffer locally.

Both bounded JSON buffers and the preview `InitializationStatus`/`Snapshot`
workspace now live on the heap. The target image and `WritePlan` block payloads
were already vector-backed heap allocations. The compiled handlers are now 416
bytes each. The worst audited nonblank preview path is 5,264 bytes. The
diagnostic uses a 12,288-byte HTTP stack, matching the existing production HTTP
task allocation, and leaves 7,024 compiler-estimated bytes. CI runs the separate
`tools/check_diagnostic_http_stack_usage.py` guard and requires at least 4,096
bytes for ESP-IDF's pre-handler dispatch frames and dynamic library use.

Serial reports current-task high-water values at API and preview handler entry,
before and after preview construction, after OpenPrintTag decode, after target
and `WritePlan` generation, and before each JSON response send.

## Read-only interface and retired write path

The diagnostic boots and completes the existing read-only test first. It offers:

- `GET /api/v1/openprinttag/preview` for the current plan and safety status.
- `GET /api/v1/openprinttag/image` for the generated 312-byte binary.

The page continues polling the diagnostic and preview endpoints every five
seconds after COMPLETE. The initialization button is absent. The historical
`POST /api/v1/openprinttag/initialize` route is not registered, and the loop
cannot consume an initialization request while the developer gate is false.
There is no reinitialize or force path. The retained transaction implementation
still contains the exact UID/checksum/blank/geometry/security/readback/decode/RF
cleanup guards, but it is unreachable in the shipped diagnostic configuration.

## HTTP stack-fix bench acceptance (read-only)

1. Flash the pull-request `opentag-nfc-v-diagnostic-pr` artifact.
2. Confirm boot does not reset, the display remains on, the
   `OpenTag-I2C-Test` access point appears, and the diagnostic remains stable.
3. Confirm the existing read-only diagnostic still passes with UID
   `[physical tag UID omitted]`, geometry 80 × 4, full-image checksum `9E639911`,
   consistent repeated reads, removal/reinsertion recovery, both bus error counts
   zero, and scale/NFC post-test health passing.
4. Open `http://192.168.4.1` and require the OpenPrintTag preview to load with
   `Current OpenPrintTag decode: PASS` and `Writable range blank: false`.
5. Leave the browser connected for several minutes while it polls both read-only
   endpoints every five seconds. Require no `httpd` panic or reset.
6. Record the HTTP task high-water checkpoints. Do not issue an initialization
   POST or perform any other NFC write.

# Production read-only NFC / OpenPrintTag

The normal `wt32-sc01-plus` / **Install OpenTag Station** image now includes the
read-only path. Diagnostic physical acceptance is complete; production-firmware
bench acceptance below is still required. No NFC write is authorized by this PR.

## Owners and boundaries

| Owner | Responsibility |
|---|---|
| Scale task | Wire, GPIO10/11, NAU7802 0x2A, 100 kHz; unchanged calibration/sampling |
| NFC logical owner, on backend task | Wire1, GPIO13/14, IRQ12, ST25R3916B 0x50, 100 kHz |
| UI task | LVGL/display; FT6336 on GPIO6/5 through LovyanGFX software I2C port -1 |
| Backend task | Serializes NFC polling and StationWorkflow/backend HTTP; no concurrent NFC task |
| Network/httpd | Snapshot serialization and invalidation, never RFAL or decode |

LovyanGFX 1.2.27 supports negative-port software I2C in `soft_i2c.inl` and
`platforms/esp32/common.cpp`. This frees both hardware I2C controllers without
moving validated NFC/scale pins. Touch remains enabled in production; only the
diagnostic disables it. The software-touch path needs physical confirmation.

`I2cReader` owns `RfalRfST25R3916Class(&Wire1, board IRQ)` and
`RfalNfcClass(&reader)`. Unchanged vendored ELECHOUSE commit
`16eb6c7fb13e502d320924040d768a9e564209b2` supplies the actual RFAL implementation
(ST25R3916 1.1.1 / NFC-RFAL 1.0.2). Legacy SPI/platform scaffolding remains only
for host abstraction tests and is excluded from production. It is not another
runtime backend. Shared diagnostic UID/sysinfo/read-response helpers were moved
to `nfc/protocols/nfcv/read_protocol.*` and remain shared by both builds.

Inventory runs every 500 ms (5 s error backoff). Three consecutive single-UID
polls trigger two complete, compared memory reads with UID checks between chunks.
System information uses standard then extended fallback; reads begin at eight
blocks and fall back toward single blocks on command failures, never on transport
or malformed-length failures. Geometry is bounded to 4096 bytes / 32-byte blocks;
the read transaction has a 15 s deadline checked between bounded RFAL operations.
Collision-resolution zero devices is normal idle, multiple devices are ambiguous.
Every field attempt closes via scoped cleanup and checks post-operation chip ID/
transport. Errors cannot silently become no-tag results. No normal raw-memory dump.

Heap-owned decoded records retain canonical UID, geometry, checksum, envelope,
material, timestamp and insertion generation. Same-tag polls do not reread or
rediscover; removal/replacement clears active state. Malformed tags remain visible
as unsupported without repeatedly decoding. A new insertion creates one waiting
StationWorkflow generation and requests a fresh scale measurement when calibrated.
Only a completed stable, non-overload weigh under five seconds old and newer than
the insertion request can be submitted. Failed/timed-out weighing remains waiting;
an explicit Weigh can complete it. Backend queueing and responses are generation
fenced so removal/replacement cannot resurrect a stale spool. Existing configuration
supplies reconciliation tolerances and existing tag/backend empty-weight precedence.

## UI/API and safety

The touchscreen Tags page and browser Tags page show recognition, canonical UID,
checksum, material/type/brand/color, full/consumed/remaining and measured weights.
Absent metadata is unavailable/null. The empty test tag is still recognized.
`GET /api/v1/nfc`, `/nfc/tag`, and `/spool` expose this through the normal API;
there is no special diagnostic AP requirement. Errors are separate from idle.

There are no bound tag mutation methods or initialization/force/reinitialize UI
actions. The diagnostic write UI/route remains retired. Generic domain writer
source and upstream library methods remain for tests/future work, but the
production ELF must not bind them. `check_production_nfc.py --elf` checks both
source allowlists and linked symbols. Blocks 78–79 are never written (nor any
other block). The existing 312-byte/aux32 golden image and checksums are unchanged.

## Stack audit and validation

The shared backend/NFC task has 16 KiB; there is no dedicated NFC stack.
See [shared-owner budget and scheduling](backend-nfc-owner.md).
`check_production_nfc_stack_usage.py` sums its compiler
frames through output-parameter decode, both envelope/material branches, CBOR
parsing including the rejecting nesting-depth frame, and separately transport.
At least 4096 bytes must remain. This is a regression estimate, not a substitute
for runtime high-water readings or a proof of every framework path. Large memory
images and DecodedTag are heap backed. UI/httpd only hold small shared snapshots;
they never call Codec. Existing task sizes remain: loopTask 16 KiB, UI 12 KiB,
network 16 KiB, httpd 12 KiB, backend 16 KiB. Existing task-margin diagnostics
remain; NFC adds entry/before/after-decode checkpoints and five-second state/stack
logging. Existing diagnostic loop/httpd guards remain unchanged.

For historical comparison, the original dedicated-task production build reported a 11,984-byte worst nested NFC decode
estimate, leaving 4,400 bytes of the 16,384-byte task (4,096 required). Its
transport estimate is 3,776 bytes. The affected HTTP path has frames of 32
(entry), 704 (handler), 2,192 (router), 4,320 (snapshot), and 208 (NFC serializer):
7,456 bytes before library leaf/runtime allowance within the existing 12 KiB
httpd allocation. The UI workflow refresh is 2,592 bytes, network publication
1,024, and application setup 1,840. None calls OpenPrintTag decode. The backend
process frame is 2,464 and accept-identification 464; network work stays there.
These are compiler estimates; the bench must record runtime minima, including
touch use, browser polling, populated tags and backend timeouts.

CI runs native tests, browser tests, production/diagnostic firmware, stack audits,
source/ELF read-only checks, pinned Python golden verification, both flasher
bundles and combined Pages validation. PR artifacts include
`opentag-production-nfc-pr` and the separate diagnostic bundle. PR branches do
not deploy to production Pages.

## Physical acceptance (pending; read-only)

Flash the PR's normal factory bundle with the normal web flasher, not the
diagnostic. Do not use any initialization/write command.

- Boot normally with no reset; normal display, four-corner touch and scale work.
- No diagnostic AP is required; normal setup/network behavior is unchanged.
- Present the initialized tag: UID `E0:04:01:08:66:27:D8:D4`, 80 × 4 = 320
  bytes, checksum `9E639911`, decode PASS. Envelope usable312, aux35 at276.
- Empty metadata is shown as unavailable, with recognition on touchscreen and
  browser/API. Stable weight advances the existing workflow; absent calibration
  or unstable weight stays waiting with the tag retained.
- Remove: active tag/spool clears. Reinsert: same UID/checksum returns. A stationary
  tag does not increment workflow generations every poll or trigger full rereads.
- Keep browser polling and touchscreen active for several minutes; no panic,
  watchdog/reset, bus errors or decreasing stack headroom. Record all task minima;
  NFC must retain at least 4 KiB under the tested production conditions.
- No NFC write occurs. No automatic merge.

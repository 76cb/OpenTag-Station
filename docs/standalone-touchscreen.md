# Standalone touchscreen workflow (1.0.0-rc.3)

The WT32 operates the normal workflow without a browser. A compatible blank
SLIX2 tag offers **Assign tag** on Home and Tag. A linked OpenPrintTag offers
Update, Reassign, and Clear / Reuse. An unlinked valid tag offers Link, Reassign,
and Clear. The API publishes the same lifecycle selector used by WT32.

Choose **My Spools**, **My Filaments**, or **Community**. Inventory requests retain
eight records and display three finger-sized rows at a time. Search, Previous,
and Next are bounded backend commands. Selecting a spool never writes: Use Spool
opens the exact writer preview. Reassignment includes a From/To review, preserves
the former spool in inventory, and uses the existing rewrite path.

My Filaments opens a physical-spool form with initial, remaining, and empty-spool
weights. Community search runs on the backend task over authenticated TLS, imports
or reuses a matching canonical filament using the existing import contract, then
opens the same spool form. Long Community source names remain intact; Edit Display
Name explicitly chooses a shorter Spoolman name before import. The keyboard never
writes, clears, assigns a printer, or submits a spool creation by itself.

Clear success and cleanup-retry success offer existing-spool selection or new-spool
creation immediately. The station performs its own NFC refresh; the user does not
remove/reinsert the tag. Cleanup retries retain PR #34's remote ownership checks
and perform zero NFC reads/writes. Recovery restores the journal; incomplete writes
return to an exact review, while verified clear journals retry cleanup only.

## Input and layout

One reusable full-screen component supplies text, numeric, URL, and password modes.
Normal keys are 44×44 px, numeric keys 112×44 px, and action/cancel keys at least
44 px tall. Shift, symbols, wide Space, and repeat Delete are explicit controls.
Cancel returns without changing the caller's value. Done validates byte limits and
numeric bounds, keeps the caret at the end of long values, and returns to review.
Only the input screen is active while typing; normal navigation is inaccessible.

The component retains 75 LVGL objects (screen, title, textarea, 36 buttons and their
36 labels); the textarea owns its internal label. Modes reuse these widgets. Tag
screens reuse eight buttons, title, and a scrollable details area. LVGL continues
using its fixed 64 KiB PSRAM pool; draw buffers and the 12 KiB UI task are unchanged.
The UI JSON allocator is separate from the backend allocator and bounded at 192 KiB.

## Community working set

The station requests gzip from the fixed public Community origin. Inflating retains
one 32 KiB dictionary, one inflater state and a bounded 1 KiB gzip header in PSRAM.
CRC and expanded-size checks reject corruption; compressed and expanded responses
remain limited to 64 MiB and the existing 20-second backend operation deadline.
Each source object is limited to 8 KiB, each retained import record to 2 KiB,
eight retained results, 100,000 scanned records, and JSON nesting depth 12.
The aggregate backend JSON allocator remains capped at 192 KiB; failure returns an
actionable error without mutations. No complete catalog is retained in RAM.
The search payload workspace is bounded by 192 KiB JSON + 8 KiB source object +
48 KiB inflater/dictionary (compile-time checked), plus the existing 24 KiB writer
snapshot: at most 272 KiB of PSRAM payload capacity. Small control objects and TLS
allocations are additional. UI JSON uses its separate 192 KiB ceiling, not internal
RAM. These are allocation ceilings, not claims of measured hardware heap peaks.

The September 17 upstream sample was 53,424 records / 44,222,456 expanded bytes,
1,202,746 gzip bytes, with a largest serialized record of 1,247 bytes. These are
observations, not unbounded input assumptions. Search latency on physical Wi-Fi
still requires acceptance testing; exceeding the existing deadline fails safely.

## Validation and physical acceptance

Native tests exercise the production screen and keyboard models, paging, exact
writer confirmations, stale-operation rejection, import/spool transitions, cleanup
reuse, gzip integrity and expansion limits. Sanitizers include these models.
`render_touch_review.py` consumes fixtures exported by those models; screenshots
are approximate fonts/layouts, **not LVGL framebuffers or hardware captures**.
Static stack guards cover new Community and UI paths with explicit allowances.

Physical acceptance remains required: type `SUNLU PLA+ 2.0`, weights, and a URL;
test long-press deletion and edge keys; then perform clear → assign → write,
Community → import → create → write, reassign, weigh, and printer assignment with
the phone/computer disconnected. Record actual UI/backend stack high-water marks,
LVGL free pool, Wi-Fi search duration, and recovery across power loss. Rendering
alone does not establish comfortable finger operation.

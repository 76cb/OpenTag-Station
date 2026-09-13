# End-to-end MVP release validation

The MVP is: place spool → recognize OpenPrintTag → obtain a fresh stable weight
→ resolve Spoolman spool → display reconciled weight → select Prusa XL T1–T5
→ assign through FilaBridge → verify exact readback.

Production NFC recognition, complete reads/decode, persistence after reboot,
removal/reinsertion, scale coexistence, and long stationary soak have already
passed. The physical shared backend/NFC free stack was 9776–9872 bytes,
RFAL=0 and bus_errors=0. Do not repeat basic bring-up as a separate phase.

Historical pre-MVP evidence is archived in
[validation history](history/release-validation-before-mvp.md).

## Software validation

The completion PR must pass every CI check: all native tests, browser tests,
production and diagnostic firmware, stack analyzers, shared backend/NFC and
httpd audits, production NFC source and ELF guards, pinned OpenPrintTag golden
reference, web assets, both flasher bundles, and assembled Pages validation.
Local completion validation covers 360 native cases across 24 suites and 47
browser cases. Production uses 175,428 bytes static RAM and 2,345,641 bytes
flash. The shared backend/NFC compiler reserve is 4,256 bytes. Ordinary httpd
routes retain 4,112 bytes after a 2,048-byte allowance; the unchanged OTA upload
audit retains 2,912 bytes. Record the final CI head in the PR. A build or host
test never substitutes for physical readings.

New regression coverage includes response bounds and fallible PSRAM allocation,
JSON allocation limits and complete release over repeated cycles, internal-heap
admission, malformed/truncated JSON, deadline expiry, health-only probe cycles,
HTTP authentication/API/server errors, explicit spool confirmation, empty-tag
identity safety, printer preservation across removal, stale generations, and
backend invalidation while the browser remains responsive.

## Runtime boundaries

- One 16384-byte backend task owns NFC, RFAL/Wire1, decode, Spoolman and
  FilaBridge. There is no separate NFC task. Compiler remaining margin must be
  at least 4096 bytes. Do not regress the observed approximately 9.8 KiB free
  runtime margin.
- UI, network and httpd only enqueue commands or read normalized snapshots.
- HTTP bodies and parser storage use bounded PSRAM allocation without a large
  internal-RAM fallback. Allocation failure publishes a retryable resource error.
- Internal free heap and largest block are checked before backend requests.
  Health checks run every 30 seconds measured from completion; full discovery
  runs on settings changes, explicit Test backends, mutations and every five
  minutes. Spool inventories are only queried on workflow demand.
- The whole backend operation has a 20-second deadline, shared by every request.
- Configuration save returns its own asynchronous persistence receipt; backend
  status arrives separately. Backend failure must not starve local management.
- Readback must be fresh after mutation and exactly match printer ID, zero-based
  toolhead ID and spool ID. HTTP 200 alone is not success.
- All production tag access remains read-only, including blocks 78–79.

## One consolidated physical acceptance

Run this procedure once after software checks pass, using the normal production
bundle. Keep serial logging and a browser open throughout. Perform mapping
steps with the printer idle; do not interrupt an active print for acceptance.

1. **Fresh provisioning.** Install the normal bundle, connect to the setup AP,
   save Wi-Fi and confirm association, DHCP, setup-page responsiveness and AP
   grace completion. The tag remains deferred until provisioning closes.
2. **Configured reboot.** Reboot, verify saved Wi-Fi and settings return, and
   open Home, Scale, Printer, Tags and Settings on the touchscreen. Verify touch
   remains responsive while both hardware I2C buses operate.
3. **Scale.** Verify the uncalibrated guidance, tare an empty platform, calibrate
   with a known mass, and verify zero/loaded readings. Save/reboot must retain
   calibration. Exercise one unstable measurement, retry, and a safe overload
   simulation only if supported by the load-cell fixture.
4. **Backends.** Configure Spoolman `http://192.168.1.215:7912`, identity field
   `opentag_instance_uuid`, NFC field `nfc_uid`, and FilaBridge
   `http://192.168.1.155:5000`. Save must return promptly and show refreshing.
   Run Test backends once. Confirm the final status in both interfaces.
5. **Printer.** Select `printer_1785006977542801400_535`, displayed as
   **Casy's Prusa XL**. Verify Toolhead 1–5/T1–T5 and backend IDs 0–4 on the
   touchscreen, browser and `/api/v1/printers`/`toolheads`. The printer is at
   `192.168.1.8`; no test address is compiled into firmware.
6. **Identify and weigh.** Place the OpenPrintTag spool with UID
   `E0:04:01:08:66:27:D8:D4`. Confirm recognition even when metadata is empty,
   UID, material/brand/color when present, physical weight and tag
   consumed/remaining values when available. The tag must remain recognized
   while waiting for stable weight. One insertion requests one fresh weighing.
7. **Resolve and reconcile.** Verify exact UUID or UID matching, or explicitly
   confirm the correct Spoolman spool ID in the browser when no/ambiguous match
   exists. Check displayed Spoolman remaining, measured remaining and the
   reconciliation result. Never silently select one of several candidates.
8. **Assign and verify.** With the printer idle, tap T1, confirm replacement
   only if needed, and wait for verified assignment. Cross-check FilaBridge's
   mapping and `/api/v1/toolheads`. Exercise unassignment and its empty readback
   from the browser. Verify active-print/occupied-toolhead guards without
   overriding a running print.
9. **Remove and reinsert.** Remove the spool; workflow/tag clear while printer
   discovery remains. Reinsert and repeat resolution and exact readback. Repeat
   removal/replacement during a slow backend request; after its bounded return,
   old completion must not attach a spool/assignment to the replacement tag.
10. **Stationary and memory soak.** Leave a spool stationary at least five
    minutes and continue for at least ten completed periodic health cycles
    (allow ten minutes or more). Exercise REST reads, the existing WebSocket,
    tab changes and explicit refresh. Verify stable tag generation, no repeated
    autonomous weighing, no resets/watchdogs, and no resource decline across
    comparable settled cycles.
11. **Backend failure and recovery.** Temporarily configure an unreachable test
    address or block its route, then restore it. Exercise connection refused,
    DNS failure and wrong reverse-proxy authentication where available. The
    station on the 192.168.3.x IoT VLAN must report actionable network/API errors
    for services on 192.168.1.x. Local pages, configuration and scale stay usable.
    Retry Test backends and verify discovery recovers. Browser disconnect and
    WebSocket reconnect must not replay mutations or overwrite newer state.
12. **Final record.** Confirm zero NFC writes, capture final firmware SHA and
    checksum `9E639911` for the known initialized tag (80×4 physical bytes,
    312 usable bytes, aux requested 32/encoded 35 at offset 276). Retain the
    serial/browser evidence with the acceptance result.

For each significant stage, record free/minimum/largest internal 8-bit heap,
free/minimum/largest PSRAM, every task's stack high-water value (loopTask,
network, UI, configuration, backend/NFC, scale, control, OTA, httpd), backend
response/parser sizes and durations, NFC bus errors, reset/watchdog counts,
and HTTP socket/WebSocket counters. `BACKEND phase=...` lines identify settings,
HTTP receipt/release, parse/body/document release and complete probe boundaries.
Require no settled-cycle ratcheting, no inaccessible management page and no
backend/NFC stack regression. The httpd compiler audit must pass; record its
physical margin under this combined workload, with at least 4096 bytes targeted.

## Post-MVP

Production NFC initialization, block writes, consumed-weight writes, locks,
passwords, AFI/DSFID/EAS and privacy/protection operations remain unbound.
Enclosure-specific antenna characterization, wider server-version support and
extended destructive power-loss matrices are follow-up work. Existing A/B OTA,
rollback, lifecycle and persistence regression checks remain mandatory.

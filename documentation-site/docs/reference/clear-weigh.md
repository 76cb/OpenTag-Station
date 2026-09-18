# Clear and Weigh contracts

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


Spoolman stays canonical. Community search and selection use the station's local,
block-compressed catalog; the browser and WT32 share that backend service.

## Community

Normal browser Community traffic is same-origin. Catalog updates use the pinned
OpenTag manifest and pack URLs on the backend task with TLS verification. Size,
SHA-256, format, block, and record coverage checks complete before atomic install.
The existing import contract stays `spoolmandb-community/0a39c9b5`. Community hides
Browse, changes the search hint, and returns to a canonical Spoolman filament after import.

## Clear / Reuse Tag

Tags offers a secondary Clear / Reuse action. Opening it reads and previews;
the separate Clear tag button confirms the exact UID, generation, current image
checksum and target checksum. WT32 uses CLEAR / REUSE followed by CONFIRM CLEAR.
This is not a factory reset and is not a generic raw erase endpoint.

Only approved NXP ICODE SLIX2, E0:04 prefix, 80 × 4-byte tags are accepted. Source
data must be a recognized OpenPrintTag, diagnostic empty envelope, blank image,
or a matching interrupted writer/clear journal image. The target zeroes bytes
0–311 and preserves bytes 312–319 exactly. Every existing inventory, double-read,
UID/generation, geometry, system, security, bus-error, checksum, per-block
readback, and final full-read fence applies. Protected blocks are refused.

Only changed blocks are written. Block 0 (envelope/header) is last, retaining the
envelope until payload clearing is complete. This is not atomic: a surviving
header never establishes integrity. Recovery classifies every physical block
against the journal's original/target images **before** decoding, including the
protected tail. Torn or unknown blocks fail closed. A different tag cannot
replace an outstanding clear recovery journal.

The checksum-protected `OPTWR003` journal adds WRITE/CLEAR, verified-blank and
owner-bound cleanup checkpoints, and the cleared instance UUID. V1/V2 records
migrate as WRITE only. Storage still writes a staging file, flushes, verifies
every byte, and atomically renames it. Journal deletion is now verified too.

Physical clearing and full blank verification happen before any ownership lookup
or Spoolman mutation. Cleanup searches all supported UID spellings, checks exact
unique ownership and the instance UUID, and binds the owner durably before PATCH.
Only the two configured UID/instance extra values are set to null. Readback
verifies both are removed; final UID and UUID queries detect ownership conflicts.
Usage, spool/filament/vendor data, notes, and unrelated extras are preserved.
The matching confirmed local mapping is then removed transactionally.

If any remote/local/checkpoint step fails, the journal and UI retain
`unlink_pending`: **Tag is blank and verified. Spoolman unlink is still pending.**
Retry unlink makes no NFC read or write, also after restart and without a tag on
the reader. It checks the captured backend/field settings and exact bound owner
again. Changed ownership or a new/different identity fails closed. If power is
lost before the blank checkpoint, presenting the same tag permits double-read
recovery; an already complete blank target proceeds to cleanup with zero writes.
Restart restoration does not require a working NFC reader for cleanup-only work.

Spoolman has no compare-and-swap transaction spanning tag, remote extras, and
local configuration. Ownership checks/readback bound that cleanup operation;
they do not turn the three systems into one atomic transaction. Canonical editor
expected-value fencing remains unchanged and rejects stale edited fields.

## Explicit Weigh and Spoolman updating

**Auto-update Spoolman after Weigh** is off by default, including existing schema
3 configurations with no saved setting. Browser Settings / Scale and the WT32
Scale toggle persist `reconciliation.auto_update_after_weigh`. Scale shows the
active policy and gross − tare = measured filament alongside canonical remaining
weight and its difference.

Only a user-requested completed Weigh session can become an update candidate.
Automatic measurements used by NFC identification explicitly opt out. Tare,
calibration, unstable samples, timeouts, repeated stable readings, and refreshes
cannot enqueue a weight mutation. A new explicit measurement captures its own
operation ID, spool generation/UID, settings revision, canonical used/remaining
weights, resolved tare, and policy, including both configured reconciliation
tolerances. Canonical readback recomputes workflow/UI reconciliation with those
same captured normal/warning thresholds, behind the settings-revision fence.
Unresolved/ambiguous/offline spools or unknown
tare are ineligible; net weight must be finite and nonnegative.

With auto off, the browser/touchscreen offer Update Spoolman for that measurement
ID. With auto on, the existing backend owner processes the captured candidate.
A session is consumed before its first attempt, so duplicate requests, polling,
failure retries, or leaving the spool in place never PATCH it again. A fresh
explicit Weigh is required after a failure/conflict; reboot does not replay an
old measurement. Settings/spool changes invalidate the capture.

The configured normal reconciliation tolerance (default 5 g) is the no-write
deadband. Updates use the existing `set_remaining_weight` path: canonical GET,
expected-used comparison (existing 0.05 g numeric tolerance), one PATCH, and GET
readback (existing 0.25 g verification tolerance). IDs are checked too. A bounded
one-tag inventory check on the existing backend/NFC owner runs immediately before
PATCH, after the canonical GET; a missing/replaced tag refuses mutation. This
check performs no decode or NFC write. Observed concurrent consumption refuses
the update and reloads canonical values. No operation automatically rewrites an
OpenPrintTag as a consequence of a weight update.

## Validation and physical review

Native tests cover clear source/fence/readback failures, several interruption
points, journal migration, cleanup failures/restart/owner conflicts, local mapping
persistence, explicit measurement policy, once-only updates, concurrency, deadband
and replacement. Browser tests mock catalog status/update failures and all mutations.
Chromium runs the real production HTML/CSS/JS/CSP at exact 1440/1280/1024/768/390
pixel widths. The CSP prevents direct browser Community traffic to the upstream
origin; catalog updates are owned by the station backend.

All previous task sizes, stack reserves, codec depth and web asset caps remain.
The stack audit includes clear decode/transport/HTTP/storage, restart recovery,
and weight HTTP/inventory paths. Clear state uses the existing PSRAM writer plan;
there is no new NFC task. HTML/JavaScript indentation is compacted to recover
source space, and the generated gzip remains deterministic.

Physical WT32 rendering/touch operation, real tag interruption recovery, and a
controlled live Spoolman measurement remain external-review acceptance items.
Local automated/visual validation uses fixtures and performs neither a live
Spoolman edit nor a physical NFC write.

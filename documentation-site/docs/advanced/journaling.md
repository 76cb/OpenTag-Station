# Writer journaling

> **1.0 scope:** Community import/search/update is disabled after physical ESP32-S3 inflater-state memory overwrite. Community internals below are retained development reference for 1.1; My Spools/My Filaments and the guarded writer remain the production workflow.

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


The Tags page supports Spoolman browsing, Community import, spool creation,
initialize/rewrite preview, explicit physical confirmation, consumed-weight
updates, and retryable identity association. Nothing writes in response to a
presence event, scale change, periodic health probe, or ordinary read.

The [browser inventory/editor guide](https://github.com/76cb/OpenTag-Station/blob/main/docs/writer-browser-workflow.md) describes
selection, paging, canonical Spoolman editing and the human-readable preview.

## Source of truth and catalog contract

Spoolman is authoritative. Every tag is generated from a fresh canonical
`GET /api/v1/spool/{id}`. Community data is only an import source. Vendor and
filament creation/reuse is followed by individual canonical GETs; spool creation
also requires a canonical GET. No Community object reaches the tag mapper.

Spoolman catalog requests use `limit=8`, `offset`, `sort=id:asc`, and documented
name, vendor, filament, material, or article filters. Responses are capped at
24 KiB. Eight results permit requesting the next page; a short page ends the
query. Offset is bounded to one million, not eight pages or a fixed total
inventory size. Live inventory edits can move records between offset pages;
refresh to restart a consistent search. Browsing is user-driven, never part of
the cheap 30-second health probe.

Community source: the public compiled JSON array at
<https://icezaza2543.github.io/SpoolmanDB-Community/filaments.json>, also used by
the upstream public application. Its contract was inspected at repository commit
`0a39c9b580048800c78850a3f0a4260b989448ea`:

- [compiled schema](https://github.com/icezaza2543/SpoolmanDB-Community/blob/0a39c9b580048800c78850a3f0a4260b989448ea/filaments.compiled.schema.json)
- [upstream application](https://github.com/icezaza2543/SpoolmanDB-Community/blob/0a39c9b580048800c78850a3f0a4260b989448ea/public/app.js)

The accepted import contract remains `spoolmandb-community/0a39c9b5`. CI compiles
the pinned source snapshot into `community.pack`; unknown source keys fail the
compiler rather than silently changing meaning. The rc.4 snapshot contains 53,424
records from 44,222,456 bytes of source JSON and compiles below the 2.5 MiB gate.
WT32 and browser submit the same backend command. Search inflates bounded index
blocks from LittleFS and matches all query terms against manufacturer, name, and
material. Detail selection inflates one independently compressed block. Results
display eight at a time. Network is used only for explicit catalog updates.

The server validates each selected entry before import. Required Spoolman density
and diameter must be positive; missing/null optional values stay absent. Source
weight zero cannot be sent as Spoolman's positive `weight`, and is omitted from
the proposed filament while remaining visible in the source preview. No material
defaults or midpoint temperatures are invented. Spoolman names must fit 64 bytes;
oversized source names require an explicit shorter display name in the browser.
The full source and stable source ID remain visible. No silent truncation.

## Imports and duplicates

The import preview shows the source, normalized filament, vendor ID or proposed
vendor creation, and filament ID or proposed filament creation. Confirmation is
tied to a specific import preview token. Searches are repeated immediately before
creation to catch records added since the preview.

`external_id = spoolmandb-community:<source id>` uses Spoolman's documented
external-database identity field. Vendor reuse uses an exact quoted name filter.
More than one matching vendor or source identity is a conflict. If a source ID
has not previously been imported, exact normalized name/vendor, material,
diameter, density, package and spool weights, colors/direction, article number,
and temperature settings are compared across bounded pages. Null and zero are
different. Equivalent existing filaments are reused; ambiguous duplicates must
be resolved in Spoolman. Spoolman has no cross-client transaction/unique constraint
for this integration, so simultaneous imports by other clients can still race;
the station serializes its own operations and never automatically retries POSTs.

Mapped import values include names, material, density, diameter, weights, color(s),
article code, and explicitly supplied extruder/bed setpoints. Ranges, spool type,
refill state, additives, appearance, country, and data-sheet references are
retained as bounded Spoolman comments where space permits. The preview exposes
the exact proposed object. These comments do not become proprietary tag fields.

Create-spool accepts only filament ID plus optional initial/used/remaining weight,
tare, price in Spoolman's currency, location, batch, and notes. Do not supply
remaining and used weight together. After an uncertain create response, inspect
the current Spoolman list before creating another physical spool record.

## Canonical Spoolman to OpenPrintTag mapping

The existing decoder baseline remains `e0dab1ae16838d2c342e7cfc509455441b7d8eba`.
Writer layout and current field definitions use specification
`7e09cc38df1c8e7824a67f5b1ae93071f52519ad`. The decoder additionally accepts that
revision's key 61, while retaining legacy key 30 reads; conflicting diameter keys
are rejected. `spoolman_mapping.cpp` is the only canonical-record mapping layer.

| Canonical value | OpenPrintTag field |
|---|---|
| Prepared unique physical spool UUID | main 0 `instance_uuid` (16 bytes) |
| Spoolman filament | main 8 `material_class=FFF` |
| Exact supported material abbreviation | main 9 `material_type`; common PLA/PETG/TPU/ABS/ASA/PC/PCTG/PP/PA6/PA11/PA12 enum values |
| Filament name, at most 63 bytes | main 10 `material_name` |
| Vendor name, at most 31 bytes | main 11 `brand_name` |
| Material text, at most 7 bytes | main 52 `material_abbreviation` |
| Article/product ID, at most 16 bytes | main 6 `brand_specific_package_id` |
| Filament nominal net weight in g | main 16 `nominal_netto_full_weight` |
| Spool instance initial net weight in g | main 17 `actual_netto_full_weight` |
| Spool tare, else filament tare, else vendor tare | main 18 `empty_container_weight`, in g |
| Single RGB/RGBA hex color | main 19 `primary_color` |
| Density in g/cm³ | main 29 `density` |
| Diameter in mm × 1000, exact positive integer | main 61 `filament_diameter_v2`, in µm |
| Canonical spool used weight in g | auxiliary 0 `consumed_weight` |

Null fields are omitted and real zero remains zero. Names exceeding a tag field's
limit are omitted, not truncated, and missing-field warnings appear with the
proposed values. Metadata that does not fit fails before writing.

Spoolman printing setpoints are neither supported print-temperature ranges nor
OpenPrintTag's load-cell-leveling preheat temperature; they are not encoded.
Price lacks an independently verified currency code; long locations, notes,
registration/usage dates, archive status, multi-color topology, material fillers,
and source URLs have no exact implemented mapping and stay in Spoolman. No UUID
is inferred from a filament ID. No dates, GTINs, limits, or defaults are invented.

Mutable updates retain the complete existing main region and untouched auxiliary
entries, changing only consumed weight from canonical Spoolman. The existing
instance UUID must match the chosen spool and an auxiliary region must exist.
The initialized SLIX2 layout has auxiliary offset 276, requested size 32 and
encoded size 35. Updates are always explicit and confirmed.

## Physical boundary

Only the validated NXP 80 × 4-byte profile is approved for writing. The image is
312 bytes, blocks 0–77; physical blocks 78–79 are preserved and verified. Other
geometries remain readable where supported, but writing fails closed.

Inventory, exact UID, insertion generation, complete system-information response,
geometry, OPTION security bytes and two complete identical physical reads precede
the preview. Blank means all 312 usable bytes are zero. Valid OpenPrintTag or the
recognized diagnostic empty envelope can be initialized/rewritten. Other NFC-V
applications, locked blocks, changing content, multiple tags, or bus errors are
refused. Neither the API nor UI accepts raw data or block-write commands.

Confirmation includes UID, generation, target spool ID, previous UID owner ID
(zero when there is no move), and target checksum. A queued
write expires after 15 seconds. Changing the configured Spoolman destination or
identity fields invalidates confirmation before any physical write. The physical transaction has its own bounded
120-second deadline. Every changed block is addressed, fenced, written once,
read back with security, and compared; failures stop immediately. Unchanged
blocks are never written. Identity blocks are committed after other changed
metadata; a changed capability-container block is committed last.

After the last write, **all 320 physical bytes** are reread. Target bytes,
preserved bytes, decoded semantics, UID, system information, protection, chip
health, field shutdown, and zero bus errors must pass. An HTTP receipt only means
queued work. The entire target is local before the destructive sequence; no
network call occurs during it.

Only then are the configured Spoolman instance UUID and NFC UID extra fields
patched. Both must be text fields. Preview checks UUID and UID ownership,
including archived spools. UID queries cover upper/lowercase hexadecimal with
no separator, colons, or hyphens, deduplicating the returned IDs. No owner or the
target owner is accepted. One other owner requires an explicit move warning
showing its ID, the target ID and physical UID; multiple owners refuse preview.
The previous owner is frozen into confirmation and the recovery journal.
Existing valid spool UUIDs are retained; unassociated target spools receive
random RFC 4122 variant version-4 identities.

After physical verification, association rechecks UUID uniqueness and UID
ownership. An approved move clears **only the configured NFC UID field** on the
previous spool, then GET verifies that it is cleared before PATCHing the target.
The previous spool UUID and other fields remain intact. The target is GET-verified
for UUID and UID, UUID uniqueness is rechecked, and a final UID query must return
exactly the target owner. A changed, unapproved owner fails closed. These separate
Spoolman requests cannot prevent concurrent edits by external clients after the
final check.

Association failure reports **“Tag written successfully; Spoolman association
pending.”** Failed previous-owner cleanup prevents the target PATCH. If cleanup
succeeds and target association fails, retry accepts the already-cleared previous
field, rechecks ownership, patches and reads back without invoking the NFC write
loop. This approved move also survives reboot. The normal read/workflow state
is invalidated after a write attempt so it reads actual bytes and requests a new
weigh; no stale pre-write decoded object is reused.

## Interruption and memory

Physical writing is not atomic. A failed rewrite can temporarily decode as mixed
metadata. Do not remove power or the tag during writing. Before the first write,
an 817-byte version-2, checksummed LittleFS recovery record is written and read
back. It retains UID, full system/security state, original/target bytes, spool ID,
approved previous UID owner ID, and configured-backend identity. It contains no
backend credentials. Legacy 813-byte version-1 records remain readable but never
authorize moving a previous UID owner.

Every preview consults the durable journal, even after another unconfirmed
preview. After two complete identical rereads, matching journal UID, geometry,
system information and security are checked **before decoding**. All 80 physical
blocks are classified: exact original resumes ordinary preview; exact target is
decoded and verified, then exposed as association pending; a mixture of complete
old/new blocks enters explicit recovery. Any block matching neither image is
refused even if the bytes decode as valid CBOR. The preserved tail must match.
An original image that was itself a previously authorized partial recovery can
still require recovery if it does not decode. A replacement UID cannot recover
the saved association. Without a usable journal, undecodable bytes cannot enter
recovery. The journal is cleared
after verified association. To discover recovery after reboot, place the tag and
request a preview; the stored spool association takes priority over a new write.

The sole owner remains `opentag-backend`, 16,384 bytes. HTTP and touchscreen code
enqueue commands and render snapshots. Source and object/ELF guards enforce the
private destructive binding; lock/password/privacy/AFI/DSFID/EAS operations remain
unbound. The compiler audit includes writer decode, transport, mapping, backend
HTTP, and at least 4,096 bytes of reserve. No stack or memory-admission threshold
was increased/lowered to accommodate this feature.

Plans, before/target/readback arrays, journal buffers, import/catalog JSON, and
serialized snapshots use fallible bounded PSRAM storage. Small codec vectors and
short control strings remain bounded ordinary allocations. The LVGL PSRAM pool,
18,000/6,144 admission thresholds, serialized REST scheduler, and single NFC owner
remain intact. The writer JS has a separate 20 KiB source budget and gzip asset;
the existing core asset budgets remain unchanged.

Software validation includes deterministic failure/recovery tests and an
independent upstream schema/decode check of a populated production-writer image.
The `native-writer-sanitized` environment repeats the writer suite under AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection. Repeated lifecycle tests cover
successful writes, failed writes, preview replacement, association retry and
journal recovery/clear; parser allocations return to their starting count.
The browser harness exercises concurrent download sharing and repeated bounded
Community searches. These host checks cannot measure ESP32 internal heap or
certify physical LittleFS durability; the combined device soak remains required.
Physical writing and end-to-end acceptance of this feature still require the
single [release procedure](https://github.com/76cb/OpenTag-Station/blob/main/docs/release-validation.md); prior PR #27 memory results are
the baseline, not a reason to repeat bring-up.

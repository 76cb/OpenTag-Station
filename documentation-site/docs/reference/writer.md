# Writer/editor contract

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


Open **Tags → Write / Rewrite**. A native modal guides Select → Review → Preview
→ Write → Verified. Its footer keeps the next action visible while the body
scrolls. Desktop width is capped at 1060 px; screens up to 760 px use a full-screen
dialog. The background is inert and cannot scroll. Focus moves inside, Escape
closes only while idle, and closing returns focus to Write / Rewrite. Active
operations disable Close, Escape and Back. A resumed active operation offers
**Check status**, without submitting another write.

The touchscreen keeps its compact spool/preview/confirmation workflow, with
44 px action buttons, clearer progress and explicit verified/pending results.

For Clear / Reuse, Community loading and explicit-Weigh inventory updating, see
[Clear, weigh, and reuse](https://github.com/76cb/OpenTag-Station/blob/main/docs/clear-weigh-workflows.md).

## Pick inventory

Choose **My Spoolman** or **SpoolmanDB Community**. My Spoolman supports spool,
filament and vendor browsing. Each result is a keyboard-operable toggle button
with a checkmark, contrasting selected border/background, and `aria-pressed`.
The selected spool ID and product appear on Review after **Continue**. Selection
survives paging, refresh, and a failed tag preview. Changing source clears it.

Spool rows show vendor, product, material, color, remaining and initial weight,
and archived status when returned by Spoolman. Unknown values display as “—”;
zero remains a real value. Vendor and filament filters appear as removable
breadcrumbs. Selecting a filament filters its physical spools and offers spool
creation, but cannot enable tag preview until a physical spool is selected.

Previous/Next retain the backend's eight-item page size. Page number and range
use the current page start, not the next offset. Previous is disabled on page 1;
Next follows `has_more`. A full final page can lead to an empty next page because
the bounded Spoolman query does not request an inventory-wide count. Community
shows the known filtered total. Refresh keeps the current offset when the search
is unchanged; a changed search starts from the first page.

Community results explicitly say **COMMUNITY — NOT YET IN SPOOLMAN**. Select one
to review/import it. Only canonical import readback becomes a normal filament
selection. Create or select its physical spool before previewing a tag. The
existing Community contract and bounded browser cache/download remain unchanged.

## Edit canonical data

**Edit Spool** applies only to the displayed physical spool ID. **Edit Filament
Definition** displays the shared filament ID and warns that changes affect every
Spoolman spool using it. Neither editor saves until **Save Changes to Spoolman**.

For the Sunlu example, select spool #28, open filament #22's editor, change
**Nominal filament weight (g)** from `777.12` to `1000`, and save. The verified
canonical filament and selected spool details replace the previous display.
The modal stays on Review and displays **Saved to Spoolman**. Conflict feedback
shows fresh values beside the retained draft and requires another explicit save.
There is no tag-only metadata override.

The high-level `/api/v1/tag-writer` operations are:

```json
{"action":"update_spool","spool_id":28,"expected":{"used_weight":0,"spool_weight":130},"changes":{"used_weight":25,"spool_weight":140}}
{"action":"update_filament","filament_id":22,"spool_id":28,"expected":{"weight":777.12},"changes":{"weight":1000}}
```

`spool_id` is optional for a standalone filament selection. If supplied, the
backend checks that this spool still uses the exact filament before PATCH and
again on readback. IDs must be positive integers. The editors cannot modify
IDs, associations, vendor IDs, `extra`, provenance or arbitrary backend paths.

| Record | Allowed fields | Bounds |
|---|---|---|
| Spool | `initial_weight`, `used_weight`, `spool_weight` | Finite 0–100,000 g |
| Spool | `price` | Finite 0–1,000,000 in configured currency |
| Spool | `location`, `lot_nr`, `comment` | Text: 64 bytes; comment 1,024 bytes |
| Filament | `name`, `material`, `article_number` | Text: 64 bytes |
| Filament | `weight`, `spool_weight` | Finite 0–100,000 g; nominal weight must be positive |
| Filament | `density`, `diameter` | Finite, positive; ≤30 g/cm³ and ≤10 mm |
| Filament | `color_hex` | Exactly 6 or 8 hexadecimal digits |
| Filament | `settings_extruder_temp`, `settings_bed_temp` | Integers 0–500 °C and 0–200 °C |

These field names follow the inspected v0.26.1 baseline at `8d9eb7395da9553bdbf14b21231afe4e153f0a79` ([compatibility record](https://github.com/76cb/OpenTag-Station/blob/main/docs/UPSTREAM_COMPATIBILITY.md)), using the [Spoolman canonical model](https://github.com/Donkie/Spoolman/blob/8d9eb7395da9553bdbf14b21231afe4e153f0a79/spoolman/api/v1/models.py).
This editor uses `used_weight`; remaining weight is displayed from Spoolman.
Blank numeric inputs preserve existing/unknown values, rather than clearing them.
Unknown optional values and unchanged fields are omitted from the PATCH.

The backend validates allowlists/types/bounds, GETs the exact existing record,
compares every changed field with its explicit `expected` value before any PATCH,
PATCHes only changed values, then GETs and verifies each requested value. A
filament edit with a selected spool additionally reloads that canonical spool
and verifies its embedded filament fields. Mismatches or network failures are
reported as failures, never as a verified save. A failed PATCH/readback can mean
Spoolman changed while verification failed: the UI keeps the last verified data
and draft, and asks for a refresh before retry.

Every edit includes the field values reviewed when its editor opened. `expected`
uses the same allowlist and must include every field in `changes`; additional
allowlisted expected fields are accepted but only changed fields are compared.
An omitted expected key is rejected. Explicit `null` represents an unknown optional
value and matches either absent or null canonical data; it never matches zero.
Required numeric fields (`used_weight`, density and diameter) cannot expect null.

If a changed field differs on the canonical GET, the entire edit is refused before
PATCH, with a conflict and the fresh canonical record when it fits the response
workspace. For example, reviewed used weight 0 → concurrent consumption 15 →
draft 25 conflicts, with no PATCH or NFC calls. Unrelated changes are allowed and
preserved because only explicitly edited fields are patched. This applies equally
to spool and shared filament edits, including no-op requests already at their target.

The browser retains the original draft and displays current canonical values for
the attempted fields in the editor message. It updates expectations only for those
reviewed conflict fields, never silently turns other stale form values into edits,
and requires another explicit **Save Changes**. Further concurrent changes conflict
again. No automatic retry occurs and the old exact-write preview stays invalidated.
This is a pre-PATCH expected-value check, not an atomic Spoolman compare-and-swap:
the upstream API does not lock out another client between GET and PATCH.

Opening an editor removes the browser's write confirmation. Every backend edit
attempt invalidates its old tag plan. Saving or cancelling never restores that
plan: request a fresh tag preview. Edits make no NFC calls. A pending tag
association must be retried before editing or starting another workflow.

## Review the tag

The primary preview shows the product, UID and spool as a receipt. Technical
mode, geometry, changed blocks and preserved range remain under Advanced. A Current/Proposed table shows brand, product,
material, weights, tare, density, diameter, consumption and color. Destructive
warnings (non-atomic write, UID move, recovery and full metadata replacement)
remain prominent. Optional metadata notices are collapsed with a count.

**Advanced details**, collapsed initially, retains the complete backend snapshot:
generation, UUID, checksums, geometry, block list, preserved range, raw
current/proposed values, recovery and repurpose state. Confirm import, Write this
tag and Retry
association are mutually exclusive by phase. Inactive actions have
`display:none !important`, independently of the HTML `hidden` attribute.

Writing shows block progress and tag/power guidance. A pending association says
the tag is already verified and offers only an association retry. Verified shows
the product, UID, linked spool and independent readback/decode/link results; **Done** returns to Tags. Printer
assignment remains on the existing Printer page.

The Tags page distinguishes detection, decoding and canonical Spoolman resolution.
A linked identity requires the current UID to match the resolved workflow or
verified writer result; a removed/different tag cannot inherit that result.
Unknown identity is hidden, UID has a Copy action, and geometry is under Advanced.

## Embedded constraints and verification

The existing writer URL serves one precompressed asset. Behavior remains in
`writer_assets.cpp`, with shared pure browser rendering/form helpers in the core
asset; static markup, styles and field schemas are concatenated
from `writer_layout.inc` at compile time. No route, MCU runtime layout buffer,
framework, task or internal-RAM allocation is added for presentation. Core
HTML/CSS/JavaScript limits are unchanged. After extracting layout/schema data,
the writer behavior cap is 22 KiB (2 KiB above its original cap), with a separate
10 KiB layout cap and 32 KiB combined compile-time cap. Gzip remains deterministic.

The writer uses a constructed stylesheet and CSSOM color properties, compatible
with the unchanged `style-src 'self'` policy. It targets current browsers with
constructable stylesheet support. It does not loosen CSP to allow inline styles.

`node --test tools/test_web_transport.mjs` covers state, selection, paging,
filters, editors, readback and failure behavior. `python tools/test_writer_display.py`
uses installed Chromium (`CHROME_BIN` may override its path) at 1440, 1280, 1024,
768 and 390 pixels, and checks the actual reported viewport width.
It runs the production CSS/JS under the station's CSP and asserts computed
display, selection contrast, swatches, editing, conflict drafts, modal focus and
scroll behavior, locked navigation, association-only retry, success and overflow
on every product page. Keyboard Tab/Escape and full-page screenshots are also
reviewed interactively. See [GUI review notes](https://github.com/76cb/OpenTag-Station/blob/main/docs/gui-polish.md).
`--output .pio/writer-review.html` creates an interactive browser-only fixture;
its host never connects to a station, Spoolman or an NFC reader.

# Browser inventory and editor workflow

Open **Tags → Write / Rewrite**. The browser workflow is Select → Review & edit
→ Preview tag → Confirm & verify. The touchscreen keeps its existing compact
spool/preview/confirmation workflow.

## Pick inventory

Choose **My Spoolman** or **SpoolmanDB Community**. My Spoolman supports spool,
filament and vendor browsing. Each result is a keyboard-operable toggle button
with a checkmark, contrasting selected border/background, and `aria-pressed`.
The selected spool ID and product appear in the adjacent details card. Selection
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
There is no tag-only metadata override.

The high-level `/api/v1/tag-writer` operations are:

```json
{"action":"update_spool","spool_id":28,"changes":{"used_weight":0,"spool_weight":130}}
{"action":"update_filament","filament_id":22,"spool_id":28,"changes":{"weight":1000}}
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

These field names follow the [Spoolman canonical model](https://github.com/Donkie/Spoolman/blob/v0.22.1/spoolman/api/v1/models.py).
This editor uses `used_weight`; remaining weight is displayed from Spoolman.
Blank numeric inputs preserve existing/unknown values, rather than clearing them.
Unknown optional values and unchanged fields are omitted from the PATCH.

The backend validates allowlists/types/bounds, GETs the exact existing record,
PATCHes only changed values, then GETs and verifies each requested value. A
filament edit with a selected spool additionally reloads that canonical spool
and verifies its embedded filament fields. Mismatches or network failures are
reported as failures, never as a verified save. A failed PATCH/readback can mean
Spoolman changed while verification failed: the UI keeps the last verified data
and draft, and asks for a refresh before retry. Concurrent external Spoolman
edits are not transactional with this sequence.

Opening an editor removes the browser's write confirmation. Every backend edit
attempt invalidates its old tag plan. Saving or cancelling never restores that
plan: request a fresh tag preview. Edits make no NFC calls. A pending tag
association must be retried before editing or starting another workflow.

## Review the tag

The primary preview shows UID, spool, mode, UUID, checksums, changed-block count,
and preserved blocks 78–79. A Current/Proposed table shows brand, product,
material, weights, tare, density, diameter, consumption and color. Destructive
warnings (non-atomic write, UID move, recovery and full metadata replacement)
remain prominent. Optional metadata notices are collapsed with a count.

**Advanced details**, collapsed initially, retains the complete backend snapshot:
generation, geometry, block list, preserved range, raw current/proposed values,
recovery and repurpose state. Confirm import, Confirm exact write and Retry
association are mutually exclusive by phase. Inactive actions have
`display:none !important`, independently of the HTML `hidden` attribute.

## Embedded constraints and verification

The existing writer URL serves one precompressed asset. Behavior remains in
`writer_assets.cpp`; static markup, styles and field schemas are concatenated
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
uses installed Chromium (`CHROME_BIN` may override its path) at 1280 and 390 pixels.
It runs the production CSS/JS under the station's CSP and asserts computed
display, selection contrast, swatches, editing, responsive columns and overflow.
`--output .pio/writer-review.html` creates an interactive browser-only fixture;
its host never connects to a station, Spoolman or an NFC reader.

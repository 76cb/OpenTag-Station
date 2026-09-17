# Appliance interface polish

The original polish pass built on merged PR #29.

The current follow-up is documented in [Clear, weigh, and reuse](clear-weigh-workflows.md),
including Community CSP, browser/WT32 clear confirmation, and explicit-Weigh
inventory updates. The notes below describe the original PR #30 baseline.

That pass It changes browser presentation and the WT32
touchscreen's existing widgets; NFC/backend implementations, request contracts,
optimistic edit fencing, task sizes, resource limits and CSP remain unchanged.

## Browser review

The writer uses one native dialog and one set of controls. The five steps are
browser navigation over existing backend phases. Idle dismissal invalidates an
exact-write preview; pending association survives dismissal and is resumed by a
read-only status request. Backend validation/writing/readback/decode/association
states keep navigation locked, including after reopening the dialog.

Shared cards, controls, status chips, result banners, form spacing and disabled
states cover Home, Scale, Printer, Tags, Settings, configuration, diagnostics,
setup and updates. Fields retain units and unknown values use an em dash. Form
rows do not stretch input heights to match adjacent cards. Technical tag data
and raw diagnostic snapshots start collapsed. Existing scale and printer
workflows are retained.

Review fixture: Sunlu PLA+ 2.0 Black, spool #28, filament #22, UID
`E00401086627D8D4`, NXP ICODE SLIX2, 80 × 4 B, 1000 g initial and 130 g tare.
Editor tests deliberately start nominal weight at 777.12 g, save 1000 g, then
simulate concurrent consumption changing 0 → 15 g against a 25 g draft.
Only the fixture performs those changes. It blocks unexpected HTTP requests.

The Chromium fixture runs the complete production HTML/CSS/JavaScript under the
station CSP at 1440, 1280, 1024, 768 and 390 px. It covers modal semantics and
focus, background scroll lock, fixed actions, paging/selection, both editors,
stale edits, preview loading, warnings/disclosures, write progress/navigation
locks, association retry with exactly one original write request, verified Done,
empty results and product-page overflow. The Node suite additionally covers
resuming pending work, failed commands with stale readback, and removed/replaced
tag identity. These tests use fake backends and perform no physical NFC writes.

## Touchscreen review boundary

Existing LVGL objects receive spacing, colors, readable 44 px writer actions and
shorter status text. No new task, persistent buffer or LVGL widget is introduced.
The Tags view reports verified completion and association-only recovery plainly.
Firmware and stack/memory checks cover the change. Actual WT32 rendering and
touch input still need a physical-device visual check during external review;
this PR does not claim a hardware test or a live Spoolman mutation.

## Resource discipline

All previous budgets remain enforced. Core CSS whitespace is compacted and
superseded rules removed. Two existing core JavaScript sections have indentation
trimmed to fit the unchanged 144 KiB source cap while keeping other code's
formatting. Shared writer helpers reduce the separate writer behavior asset.
The generated gzip remains deterministic. Full validation includes both firmware
variants, NFC source/ELF checks, all stack audits, sanitizers, both flasher bundles
and assembled Pages validation.

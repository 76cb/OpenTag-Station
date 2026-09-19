# UI state and review contract

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


The browser now has four destinations: Dashboard, Inventory, Printer, and
Settings. A present tag transforms the dashboard into the current-spool
workspace. Weigh, Assign, and Manage tag open focused dialogs. Updating the
current tag requests a fresh preview directly; selecting another spool opens
the guided inventory picker. No preview performs a write.

## Presentation sources

- `src/web/ui/product.css`: shared dark/mint design tokens, responsive layouts,
  embedded spool artwork, focus and reduced-motion rules.
- `src/web/ui/product.js`: current-spool identity, dialogs, assignment selection,
  inventory mounting/refinements, and grouped Settings.
- `src/web/web_assets.cpp`: embedded HTML, existing transport/command handlers,
  and generated presentation regions.
- `src/web/writer_assets.cpp` / `writer_layout.inc`: the shared inventory and
  guided writer, including physical-spool creation.
- `src/ui/product_layout.hpp`: production 480 x 320 geometry, shared with the
  touch review renderer. LVGL uses the existing external widget pool.

After editing either modular browser source, run
`python tools/generate_product_ui.py`. CI checks generated-source parity.
There is no browser framework, external font, or external icon dependency.

## Data and safety boundaries

The current spool must match the present tag UID before its linked identity is
shown. Remaining percentage appears only when an actual initial weight exists.
Toolheads display T1–T5. The existing printer payload reports assigned spool IDs;
other spools' filament colors, names, nozzle sizes, and remaining weights are not
invented when the payload does not supply them.

Inventory retains eight-record backend pagination. Search, material, vendor,
and filament filters use the existing catalog request. Color and status refine
only the displayed page, explicitly labeled **Refine this page**. Selecting a
vendor browses its filaments; selecting a filament browses its physical spools.
Physical spool creation starts from an existing canonical Spoolman filament. Community is disabled for 1.0. The inventory and writer use the same selection and
edit implementation, so optimistic editing and preview invalidation remain
consistent.

All mutations still use the existing routes and command handlers. Tag writes
retain UID/generation/checksum/previous-owner confirmation; clear and unlink
retain their separate retry semantics. Weigh updates retain the explicit
measurement ID and presence fence. Assignment retains spool generation,
printer revision, previous assignment, replacement/active-state confirmations,
and verified readback. Grouped settings retain the single versioned save form,
dirty-draft handling, and hidden credentials until Edit.

No backend service, NFC geometry, journal, task stack, buffer allocation, OTA
implementation, or flasher format changes. Browser asset flash caps have modest
headroom for the presentation rewrite; all RAM and stack gates are unchanged.
The touchscreen replaces its existing objects and reuses existing pointer
storage; widget allocations remain in the existing PSRAM pool. Actual peak
widget heap and physical touch/color behavior require hardware measurement.

## Review artifact

CI uploads **opentag-ui-review**. Open `REVIEW.html` after extracting it. It
contains 65 browser captures and six WT32 layout fixtures. Browser captures use
the shipped assets at 1440, 1280, 1024, 768, and 390 pixels; all Settings sections
are also captured at desktop and phone widths. The offline fixture never
connects to a station.

Generate locally with Python, Pillow, Playwright, and Chromium:

```sh
python tools/generate_product_ui.py --check
python tools/product_review.py --output .pio/ui-review
python tools/render_touch_review.py --output .pio/ui-review
node tools/capture_product_review.cjs .pio/ui-review
```

Set `NODE_PATH` if Playwright is outside the default module path and `CHROME_BIN`
to use an installed Chromium executable. Playwright and Pillow are review-only
dependencies and are never shipped to the station.

The capture runner rejects script errors and horizontal overflow. Its desktop
and phone journeys check empty/identified states, unknown initial weight,
removed tags, direct current-spool update, inventory filters and return from
preview, occupied-tool assignment fences, hidden credentials, and Community
import through physical-spool preview. The existing transport and Chromium
suites continue to exercise mutation, failure, clear/retry, and edit behavior.

WT32 PNGs are **layout approximations, not LVGL framebuffers**. They use the
production coordinates and check bounds, text fit, and minimum action size;
font rasterization and live content remain hardware acceptance items. External
visual review is required before merging this presentation rewrite.

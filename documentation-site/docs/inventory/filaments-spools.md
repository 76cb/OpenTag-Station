# Filaments, spools and remaining weight

A **filament definition** describes a shared product: manufacturer, material,
nominal mass, color and temperatures. A **spool** describes one physical item:
its ID, usage, empty-spool weight, initial mass and archive state. Many spools can
share one filament. A vendor is the manufacturer record, not a spool identity.

| Action | Scope | Review before saving |
|---|---|---|
| Edit Spool | One physical spool | ID, used/initial weight, empty-spool tare |
| Edit Filament Definition | Every spool sharing the filament | Shared filament ID and affected properties |
| Write / update tag | Current approved physical tag | Selected canonical spool and fresh tag preview |
| Update Spoolman after Weigh | One measured spool | Measurement receipt, expected canonical usage and same current tag |

Choose a physical spool before requesting a tag preview. Selecting a Community
entry or a filament definition alone cannot supply a physical spool ID. After
import, create or select that filament's physical spool.

Remaining mass is compared to `gross − empty spool`. A missing empty-spool mass
is not zero. The UI displays unknown values as a dash and keeps zero as a real
value. Verify tare in Spoolman before accepting a surprising difference.
Edits use expected-value fences; if another client changes a field, review fresh
values alongside your draft and explicitly save again. Do not overwrite concurrent
print consumption just to match an old screen.

For workflow details see [editing](editing.md), [Community](community.md) and
[Weigh](../daily-use/weigh.md).

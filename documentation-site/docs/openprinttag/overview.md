# OpenPrintTag and station support

OpenPrintTag stores filament metadata on a physical NFC tag so a spool can carry
its description with it. OpenTag Station uses the pinned OpenPrintTag codec and
reference fixtures recorded in the firmware repository. It does not treat every
arbitrary NFC payload as an OpenPrintTag.

| Identity or value | Meaning |
|---|---|
| NFC UID | Hardware tag identity observed from RF inventory |
| Instance UUID | OpenPrintTag instance identity used for spool association |
| Spoolman ID | Canonical physical inventory record |
| Material fields | Portable brand/material/color/mass information |
| Image checksum/generation | Fences for a specific observed tag image and preview |

Routine inventory polling is read-only. Physical writes require the approved
profile, complete system/security information, stable single-tag inventory,
matching reads, a fresh preview and explicit confirmation. Updating canonical
remaining weight does not automatically modify the portable tag image.

The supported hardware writer is deliberately narrower than the full reader's
RF capabilities. Read [supported tags](supported-tags.md) before purchasing
consumables, [writing](writing.md) before a first write, and [recovery](recovery.md)
before handling interrupted work. The technical [format reference](../reference/openprinttag.md)
explains encoding and upstream compatibility without requiring ordinary users to
edit raw blocks.

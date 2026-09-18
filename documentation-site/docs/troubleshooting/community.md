# Community catalog failure

Recover catalog download or import without creating duplicate records.

## Before you start

Working Spoolman for the canonical import stage. Internet is required only to
install or update the local Community catalog.

Firmware `1.0.0-rc.4` searches a compact catalog on the station. Results are
paged eight at a time and continue to work when the station cannot reach the
internet. Search failures describe a missing or damaged local catalog. Network
guidance appears only after an explicit catalog update fails.

The physical acceptance checklist and memory measurements are tracked in
[the hardware regression test](https://github.com/76cb/OpenTag-Station/blob/main/docs/community-hardware-regression.md).

## Steps

1. Check whether the catalog is Ready, Not installed, or Damaged in Settings.
2. For Not installed or Damaged, choose Download/Redownload Catalog while the station is idle.
3. If an update fails, keep using the previous valid catalog when available and retry later.
4. After an import uncertainty, inspect canonical Spoolman inventory before importing the same product again.

## Expected result

Community results appear and a selected import returns a canonical filament record.

## If it fails

The updater checks the fixed origin, size, SHA-256, binary format, block checksums,
and record metadata before atomic installation. A failed replacement retains the
old catalog. A Community product still needs a physical spool before tag preview.

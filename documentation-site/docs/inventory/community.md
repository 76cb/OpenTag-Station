# SpoolmanDB Community import

Find a Community filament product and import it into your own canonical Spoolman inventory.

## Before you start

Internet access from the browser, reachable Spoolman from the station and a supported import contract. Community entries are not already your physical spools.

## Steps

1. Open Inventory or the writer’s selection view and choose **SpoolmanDB Community**.
2. Wait for the loading state. Search by a recognizable manufacturer/material/product and inspect the **COMMUNITY — NOT YET IN SPOOLMAN** label.
3. Select the intended product and review its fields. Choose the import action once.
4. Wait for the import’s canonical Spoolman readback. The resulting selection is now a normal filament record.
5. Create or select a physical spool for that filament before requesting a tag preview. Verify nominal mass and empty-spool tare rather than assuming every imported field is correct.

## Expected result

The imported record has a canonical Spoolman filament ID and can be used for a physical spool. Only that canonical result feeds the writer.

## If it fails

A failed download or timeout remains in the dialog with Retry. The browser fetches and caches the catalog; the ESP32 does not download the full file. Only the specific allowed Community origin is enabled by CSP. Do not broaden CSP to arbitrary HTTPS as a workaround. Live Community acceptance remains pending.

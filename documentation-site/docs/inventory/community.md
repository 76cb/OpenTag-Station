# SpoolmanDB Community import

Find a Community filament product and import it into your own canonical Spoolman inventory.

## Before you start

A Ready local Community catalog, reachable Spoolman from the station, and a supported import contract. Internet is needed only to install or update the catalog. Community entries are not already your physical spools.

## Steps

1. On WT32, choose Tag → Assign/Reassign → Community. In the browser, open Inventory or the writer selection view and choose **SpoolmanDB Community**.
2. Search the station-local catalog by manufacturer, material, or product and inspect the **COMMUNITY — NOT YET IN SPOOLMAN** label.
3. Select the intended product and review its fields. Choose the import action once.
4. Wait for the import’s canonical Spoolman readback. The resulting selection is now a normal filament record.
5. Create or select a physical spool for that filament before requesting a tag preview. Verify nominal mass and empty-spool tare rather than assuming every imported field is correct.

## Expected result

The imported record has a canonical Spoolman filament ID and can be used for a physical spool. Only that canonical result feeds the writer.

## If it fails

A missing or damaged catalog offers Download/Redownload. Failed updates retain the
previous valid catalog. WT32 and browser use the same backend search with eight-result
pages; normal search works offline. Use the full-screen keyboard to refine the search;
choose Edit Display Name if a source name exceeds Spoolman’s 64-byte limit. Live
Community acceptance remains pending.

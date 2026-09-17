# Initial setup

Connect canonical inventory and prepare a scale profile before using mutation workflows.

## Before you start

The station is reachable from your trusted network. Spoolman must be running and reachable from the station; FilaBridge is optional. Use real service values locally, never in public screenshots.

## Steps

1. Open **Settings → Integrations**. Enter the Spoolman URL, for example the documentation-only `http://spoolman.example:7912`, replacing it with your actual service address.
2. Save and test the connection. Confirm the reported backend version/capabilities and correct inventory, rather than accepting a reachable host as sufficient.
3. If using printer assignment, configure FilaBridge and select its stable printer ID. Check that the displayed printer and T1–T5 mapping match the actual printer.
4. Open **Settings → Scale**, choose the actual YZC-133 5 kg or 2 kg profile, and complete [tare and calibration](../scale/calibration.md).
5. Review network, display brightness and optional local API authentication. Empty credential fields preserve saved secrets unless you explicitly choose to clear them.
6. Export a configuration backup from the advanced configuration controls. Keep network credentials separately because the browser export is redacted.

## Expected result

Inventory loads canonical records, the scale reports its matching calibrated profile, and the selected printer is recognizable if configured. Home can now resolve an approved tagged spool.

## If it fails

An offline integration disables dependent operations. A saved URL does not prove capability; inspect the connection result. A profile change invalidates old calibration. Reload after a settings conflict instead of overwriting another client’s newer revision.

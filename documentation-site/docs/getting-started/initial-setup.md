# Initial setup

Connect canonical inventory and prepare a scale profile before using mutation workflows.

## Before you start

The station is reachable from your trusted network. Spoolman must be running and reachable from the station; FilaBridge is optional. Use real service values locally, never in public screenshots.

## Steps

1. Open **Settings → Integrations**. Enter the Spoolman URL, for example the documentation-only `http://spoolman.example:7912`, replacing it with your actual service address.
2. In Spoolman, create the two required **Spool** extra fields `opentag_instance_uuid` and `nfc_uid` (type Text). See [Spoolman extra fields](../inventory/custom-fields.md).
3. Back on the station, choose **Test** under Settings → Integrations. Spoolman should show as connected with no warning below it. A warning names any missing field or an untested Spoolman version (weights can't be saved to an untested version).
4. If using printer assignment, configure FilaBridge and choose your printer (on the touchscreen setup, pick it from the list FilaBridge reports). Check that the displayed printer and T1–T5 mapping match the actual printer.
5. Open **Settings → Scale**, choose the actual YZC-133 5 kg or 2 kg profile, and complete [tare and calibration](../scale/calibration.md).
6. Review network, display brightness and optional local API authentication. Empty credential fields preserve saved secrets unless you explicitly choose to clear them.
7. Export a configuration backup from the advanced configuration controls. Keep network credentials separately because the browser export is redacted.

## Expected result

Inventory loads canonical records, the scale reports its matching calibrated profile, and the selected printer is recognizable if configured. Home can now resolve an approved tagged spool.

## If it fails

An offline integration disables dependent operations. A saved URL does not prove capability; inspect the connection result. A profile change invalidates old calibration. Reload after a settings conflict instead of overwriting another client’s newer revision.

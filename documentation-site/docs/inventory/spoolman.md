# Connect Spoolman

Make Spoolman the authoritative source of spool and filament data.

## Before you start

A supported Spoolman service reachable from the station’s network. Documentation examples use `http://spoolman.example:7912`; substitute your private deployment locally.

## Steps

1. In **Settings → Integrations**, enter the base URL and authentication/CA settings required by your deployment.
2. Save, run the connection test and inspect version, health and capability results.
3. Open Inventory → **My Spoolman**, browse a known vendor, filament and physical spool, and confirm IDs.
4. Check configured NFC UID and OpenPrintTag instance UUID field keys before linking tags. Do not rename keys while cleanup is pending.
5. Back up the resulting nonsecret configuration; store service credentials separately.

## Expected result

The station reads canonical records and enables only capabilities supported by the adapter checks. The compatibility reference records Spoolman 0.26.0 as the inspected/live discovery baseline, not a guarantee for every future version.

## If it fails

A working URL with unexpected API shape can remain read-only. Check routing, TLS trust, authentication and service version. Never place access tokens in URLs or screenshots. An offline station does not queue inventory writes for later replay.

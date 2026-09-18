# Community catalog failure

Recover catalog download or import without creating duplicate records.

## Before you start

Browser internet access and working Spoolman for the import stage.

On the standalone WT32, Community uses the station's Wi-Fi. Firmware
`1.0.0-rc.3` fixes the rc.2 full-catalog scan that could exhaust the backend
deadline or trigger the CPU0 idle watchdog. Broad searches stop after eight
results and one lookahead match. Sparse, no-match, and final-page searches can
take up to 60 seconds. A timeout shows “Community search timed out. Check Wi-Fi
and try again.” with Retry and Back. Active searches currently wait for completion
or the deadline; Back is unavailable during the backend-owned operation.

The physical acceptance checklist and memory measurements are tracked in
[the hardware regression test](https://github.com/76cb/OpenTag-Station/blob/main/docs/community-hardware-regression.md).

## Steps

1. Check whether failure is downloading/searching Community or importing into Spoolman; they use different paths.
2. Use Retry in the dialog after checking browser connectivity. Loading hides stale ranges and pagination.
3. If the browser reports a CSP/network error, verify the expected Community endpoint; do not permit arbitrary HTTPS origins.
4. After an import uncertainty, inspect canonical Spoolman inventory before importing the same product again.

## Expected result

Community results appear and a selected import returns a canonical filament record.

## If it fails

The catalog has a bounded download/schema check and timeout; an oversized or incompatible feed is rejected. The station never proxies the full catalog. A Community product still needs a physical spool before tag preview.

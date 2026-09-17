# Community catalog failure

Recover catalog download or import without creating duplicate records.

## Before you start

Browser internet access and working Spoolman for the import stage.

## Steps

1. Check whether failure is downloading/searching Community or importing into Spoolman; they use different paths.
2. Use Retry in the dialog after checking browser connectivity. Loading hides stale ranges and pagination.
3. If the browser reports a CSP/network error, verify the expected Community endpoint; do not permit arbitrary HTTPS origins.
4. After an import uncertainty, inspect canonical Spoolman inventory before importing the same product again.

## Expected result

Community results appear and a selected import returns a canonical filament record.

## If it fails

The catalog has a bounded download/schema check and timeout; an oversized or incompatible feed is rejected. The station never proxies the full catalog. A Community product still needs a physical spool before tag preview.

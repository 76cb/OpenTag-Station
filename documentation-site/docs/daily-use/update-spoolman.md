# Update remaining weight

Apply one reviewed explicit measurement to canonical Spoolman remaining weight.

## Before you start

A completed eligible [Weigh](weigh.md), the same spool/tag still present and reachable Spoolman. The scale must have a valid tare and calibration.

## Steps

1. Check the receipt’s spool number, measured filament mass and current canonical remaining mass.
2. Choose **Update Spoolman** when auto-update is off, or inspect the automatic result if you deliberately enabled that policy.
3. Keep the same tag in place. The station refreshes canonical data and performs a bounded tag inventory check immediately before PATCH.
4. Wait for readback. Reopen the spool in Spoolman to inspect the canonical remaining/used weights if performing release acceptance.
5. If the station reports conflict or failure, resolve the cause and press **Weigh again** for a fresh measurement. Do not keep resubmitting the old receipt.

## Expected result

The station verifies the requested canonical value and refreshes the displayed reconciliation. A measurement is consumed before its first attempt and cannot replay after polling or reboot.

## If it fails

Concurrent print consumption or another editor can invalidate the expected used weight. Removing/replacing the tag also refuses mutation. Network ambiguity requires fresh canonical state; success is not inferred from a timeout. This workflow never rewrites the tag.

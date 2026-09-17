# Unsupported tag or write failure

Distinguish safe refusal from an interrupted physical write.

## Before you start

The reported phase, original tag and stable power; preserve any pending journal.

## Steps

1. Inspect type, geometry, security and exact tag identity. Approved geometry is 80 × 4 bytes with NXP E0:04 prefix.
2. If refused before writing, correct tag positioning/compatibility and request a fresh preview. Never bypass protection checks.
3. If work began, keep the original tag and inspect recovery state. Present that same tag for the offered recovery procedure.
4. If physical verification completed but association is pending, restore backend connectivity and retry association only.

## Expected result

A fresh verified physical image and intended canonical association, or a clear safe refusal without mutation.

## If it fails

A changed UID, torn block, missing security data or bus errors must fail closed. Do not erase flash/journal or swap in a blank replacement to dismiss the state.

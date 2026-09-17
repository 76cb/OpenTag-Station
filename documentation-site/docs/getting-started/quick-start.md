# Your first spool

Run the normal identify → weigh → assign sequence with a single known spool.

## Before you start

Complete hardware validation, network setup and scale calibration. Create or identify a physical spool in Spoolman. Use one approved SLIX2 tag; a filament definition alone is not a physical spool.

## Steps

1. If the tag is blank, open Inventory, select the physical spool and follow [writing](../openprinttag/writing.md). Confirm its verified association before continuing.
2. Place the tagged spool in its intended reader/platform position. Wait for Home to show the expected spool number and **Linked to Spoolman**.
3. Select **Weigh**. Leave the platform still until the receipt shows gross, empty-spool tare, measured filament and canonical Spoolman remaining weight.
4. With the default auto-update policy off, review the difference and explicitly choose **Update Spoolman** only when intended. Wait for verified readback.
5. Select **Assign**, review printer and toolhead, and confirm the chosen T1–T5 slot. Replacing an occupied slot requires an explicit replacement confirmation.
6. Remove the spool and verify Home returns to its empty state. Retain the calibration/setup backup.

## Expected result

The identified spool, measurement receipt and confirmed printer mapping all refer to the intended physical spool. Each completed mutation has explicit confirmation or verified status.

## If it fails

If identity is ambiguous, confirm the correct canonical spool before mutations. If weight differs unexpectedly, inspect tare and stability. If an operation is pending, inspect its state instead of starting a duplicate. See [troubleshooting](../troubleshooting/index.md).

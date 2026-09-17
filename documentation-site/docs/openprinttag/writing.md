# Write or update a tag

Create portable tag data from a canonical Spoolman spool using the guarded writer.

## Before you start

One [approved tag](supported-tags.md), stable power, resolved physical spool and no unrelated pending recovery. Writer operations are non-atomic and must be allowed to finish.

## Steps

1. Open Inventory or the write workflow, choose **My Spoolman**, and select the physical spool. Review vendor, material, color and mass values.
2. Use the canonical editors first if data is wrong. Choose **Continue** to review the selected spool.
3. Request **Preview** with the intended tag in place. Review UID, geometry, current/target data, changed blocks and warnings.
4. Confirm **Write** only after checking the exact spool/tag pairing. Do not remove the tag or power during writing and verification.
5. Wait for final full-read verification and canonical association. A pending association is not the same as complete success.
6. Remove and reinsert the tag to verify normal identity resolution after the operation completes.

## Expected result

Every changed block and the final full image have been read back; the writer shows a verified result and the intended canonical association. Unchanged blocks are not rewritten.

## If it fails

Changed UID/generation/checksum, security, geometry, bus errors or inconsistent double reads abort before unsafe mutation. An interrupted write must be recovered with the same tag. If only association is pending, follow its retry action instead of writing the physical data again.

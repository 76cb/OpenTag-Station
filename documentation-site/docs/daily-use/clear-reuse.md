# Clear / Reuse a tag

Erase the supported OpenPrintTag payload, verify blank memory and remove only the matching Spoolman identity association.

## Before you start

An approved writable tag, stable power and a clear understanding of the displayed spool. This is not a station reset or a deletion of the Spoolman spool.

## Steps

1. Open **Manage tag → Clear / Reuse**. On WT32 use **CLEAR / REUSE**.
2. Review the exact tag identity and proposed clear. Confirm with **Clear tag** (WT32: **CONFIRM CLEAR**) only when the intended tag is present.
3. Keep the tag and power stable. The station clears changed bytes in blocks 0–77, preserves blocks 78–79, writes the header last and verifies the full blank target.
4. Wait for Spoolman ownership lookup, identity-field cleanup and readback. Usage, filament, notes and unrelated extras are retained.
5. If the result is **unlink_pending**, use **Retry unlink** after restoring the original backend/field configuration. Cleanup-only retry does not need the tag and performs no NFC writes.
6. After complete cleanup, select the new canonical spool and use the normal writer to reuse the tag.

## Expected result

The tag is blank and verified, matching UID/instance fields have been removed from the correct Spoolman owner, and the matching local confirmed mapping is cleared.

## If it fails

Do not interpret **Tag is blank and verified. Spoolman unlink is still pending** as complete reuse. Changed ownership or an unavailable backend keeps cleanup pending. Power interruption before blank verification requires the same tag for recovery. Unknown/torn memory fails closed; do not delete the journal to force progress.

# Recovery journal states

Complete the original operation using durable checkpoints.

## Before you start

Preserved journal, original tag when physical recovery is needed, and original backend/field configuration.

## Steps

1. Read the reported phase before taking any action. Note whether physical work or remote cleanup is pending.
2. For write/clear recovery, present the same original tag and request the offered inspection/preview.
3. For association or unlink pending, restore backend access and use that explicit retry; cleanup-only unlink does not require NFC presence.
4. Wait for verified completion and inspect the intended canonical association or its removal.

## Expected result

The matching physical target and required canonical cleanup complete, allowing normal operations again.

## If it fails

Unknown/torn blocks, changed owner or changed configuration fail closed. Do not manually delete journal files or treat a surviving tag header as integrity proof. See [recovery model](../openprinttag/recovery.md).

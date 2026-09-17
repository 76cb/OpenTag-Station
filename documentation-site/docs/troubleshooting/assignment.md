# FilaBridge offline or assignment failure

Verify the printer/toolhead mapping without replaying an uncertain command.

## Before you start

Stable selected printer ID, current spool identity and access to FilaBridge.

## Steps

1. Test FilaBridge connectivity and compare printer discovery with the configured stable ID.
2. Refresh status and mappings; confirm UI T1–T5 corresponds to backend 0–4.
3. Check printer state, disabled local toolheads, version/capability restrictions and any duplicate-spool conflict.
4. After a failed mapping, inspect current backend mappings before opening a new confirmation.
5. When appropriate, confirm a new assignment once and wait for exact readback.

## Expected result

The requested spool is on the selected toolhead and the station reports confirmed assignment.

## If it fails

Active printing or stale revisions can intentionally block normal mapping. An accepted POST with mismatched readback is not success. No offline assignments are replayed later.

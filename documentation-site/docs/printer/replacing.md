# Replace a toolhead assignment

Change an occupied toolhead deliberately while making the displaced spool visible.

## Before you start

Current printer mappings, resolved new spool and a suitable idle printer. FilaBridge owns displaced-spool location and consumption rules.

## Steps

1. Open Assign and choose the occupied toolhead.
2. Read both the new spool and displaced spool in the confirmation. Check the printer name and T number again.
3. Choose **Replace** only if this matches the physical printer setup.
4. Wait for the single mapping request and fresh backend state readback.
5. Inspect the final toolhead mapping. On uncertainty, refresh before deciding whether another action is needed.

## Expected result

The requested spool appears on the intended toolhead and the result is explicitly confirmed. No offline assignment is queued for later execution.

## If it fails

If another client changed the printer revision, the confirmation is stale and must be reviewed again. If the backend rejects duplicate ownership or an active print, resolve that condition first. Repeating the request cannot fix an incompatible backend state.

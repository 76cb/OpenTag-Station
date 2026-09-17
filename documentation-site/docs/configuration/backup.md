# Back up and restore configuration

Save a portable nonsecret configuration snapshot before hardware changes or updates.

## Before you start

A reachable station and private storage for the exported file. The browser export redacts credentials; keep Wi-Fi/backend/API secrets separately.

## Steps

1. Open the advanced configuration export controls and save the JSON snapshot.
2. Inspect its product/hardware/schema metadata and confirm it is the intended station. Treat even redacted configuration as operational information.
3. Before restoring, keep a backup of the destination’s current settings. Select a compatible export and use import.
4. Wait for validation and persisted completion. Unsupported schema, wrong hardware, oversized or malformed documents are rejected before live changes.
5. Recheck calibration/profile, service addresses, selected printer and credential configured-state flags. Re-enter missing secrets locally where required.

## Expected result

The validated document is applied as a coherent revision. Noncredential imports preserve existing credentials rather than clearing them or accepting secrets from a redacted file.

## If it fails

A stale edit must reload the current revision. A calibration from a different capacity is invalid. Do not assume a redacted export can provision all secrets after an erase; see the [configuration reference](../reference/configuration.md) for exact persistence and migration behavior.

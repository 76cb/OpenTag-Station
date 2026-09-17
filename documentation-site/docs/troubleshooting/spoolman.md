# Spoolman offline

Restore canonical inventory access while preserving mutation safety.

## Before you start

Service address, locally held credentials and access to station connection results.

## Steps

1. Confirm Spoolman is reachable from the station network, not just the browser’s different network.
2. Check base URL/port, proxy authentication and TLS trust where applicable.
3. Run the station connection test and inspect version/capability messages.
4. Refresh Inventory. For a failed weight update, inspect canonical data and run a new explicit Weigh; for pending cleanup use its dedicated retry.

## Expected result

Known canonical records load and required capabilities are enabled.

## If it fails

Unexpected API shape can keep a backend read-only. A timeout after a request is not evidence that nothing changed. Do not queue or repeatedly replay mutations while offline.

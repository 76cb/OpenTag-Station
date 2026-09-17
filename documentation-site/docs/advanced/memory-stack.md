# Memory and stack design

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


The PR #26 physical follow-up found admission starvation before successful
backend HTTP. See [browser memory repair](https://github.com/76cb/OpenTag-Station/blob/codex/current-spool-experience/docs/browser-memory.md) for the allocation
audit, 65 KiB static-RAM recovery, response lifetimes and focused physical test.

The configured station previously held large HTTP strings and ArduinoJson
trees in internal RAM while also serving local pages. The MVP candidate uses
fallible, move-only PSRAM response bodies and PSRAM JSON documents. It retains
the shared 16 KiB backend/NFC task, 12 KiB httpd stack and the approved NFC writer boundary gates.

## Allocation and lifetime

Responses grow from 512 bytes only as data arrives, up to the endpoint's bound
(1–4 KiB health/info/mutations, 32 KiB printer configuration, 64 KiB maximum
inventory/status). There is no large internal fallback. The backend JSON
allocator limits all simultaneously live backend documents to 192 KiB; failed
allocations return an error. Parsing copies into PSRAM, releases the raw body,
extracts bounded domain values, then destroys the document. FilaBridge releases
printer configuration JSON before requesting status. Small mutation documents
are checked for allocation failure before any request is sent.

Before HTTP and normalized collection growth, admission checks preserve at
least 18,000 free internal bytes and a 6,144-byte largest block (TLS requires
48,000/20,000). Spoolman pages contain at most eight spools. Result vector growth
is checked against its actual contiguous allocation; extra fields are bounded
to 64 keys, 1,024 bytes each and 2,048 total key/value bytes per spool. A query
that cannot safely fit asks for an exact identity or an explicit spool ID.
These conservative thresholds are checked with real free/largest-block values;
the combined physical soak must establish the resulting runtime margins.

Configuration parsing and large route/backend snapshots use fallible PSRAM
holders, reducing automatic objects and copies. Local API parsing uses a
separate allocator from backend parsing. Network event serialization uses a
per-call allocator, so no allocator is shared unsafely between runtime owners.
Configuration persistence uses an allocator owned by the configuration service
and protected by its existing mutex. Confirmation cannot create a second large
internal JSON tree; allocation failure retains the prior saved settings.

## Work scheduling and errors

Health-only requests run every 30 seconds from completion. Full discovery runs
on configuration changes, Test backends and every five minutes. FilaBridge's
normalized discovery result is transferred once to the workflow. Mutations
discard this cache and perform fresh exact readback. Idle probes do not scan
Spoolman inventory. Configuration persistence returns its queued receipt without
performing remote HTTP; backend revision events refresh browser status later.

One 20-second budget spans each backend command/probe. DNS callbacks retain
persistent storage after a timeout and cannot overwrite a newer lookup. HTTP
connections preserve the actual socket error, including refused connections and
unreachable routes, which Arduino 2.0.17 otherwise collapses into a generic
failure. Connect waits use the remaining budget; the deadline client interrupts
trickling response reads. Authentication, missing endpoints, server failures,
malformed/truncated JSON and API shape mismatches remain distinct errors.

`BACKEND phase=...` lines record internal free/minimum/largest, PSRAM
free/minimum/largest, body size, parser allocation and HTTP/probe duration at
settings, request, parse and release boundaries. They contain no credentials or
response bodies. A settled parser cycle releases its entire allocator balance.

## Regression evidence

The native harness exercises body bounds/move ownership, injected allocation
failure, aggregate JSON limits, 200 parse/release cycles, 100 health-only cycles
per adapter, a real loopback TCP success/refusal, late DNS completion, malformed
responses, deadline expiry, identity ambiguity, generation fencing and exact
assignment readback. Browser tests cover queued saves, stale-response rejection,
REST/WebSocket recovery, candidate confirmation and repeated backend errors.

CI enforces `check_production_nfc_stack_usage.py` and the new
`check_production_http_stack_usage.py`; the latter requires 4 KiB headroom on
ordinary routes after a 2 KiB framework allowance and retains the existing OTA
upload allowance/reserve. Compiler estimates do not certify physical high-water
marks. Use the single [physical acceptance procedure](https://github.com/76cb/OpenTag-Station/blob/codex/current-spool-experience/docs/release-validation.md).

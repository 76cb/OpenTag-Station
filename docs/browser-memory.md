# Browser startup internal-memory repair

PR #26 physical firmware `68ac260bc8a23ee81a162cb874dde0acc7a1d32e`
started httpd with 33,700 internal bytes free (largest 25,588). At 30 seconds
only 5,652 remained (largest 1,524); at 60 seconds 14,876 remained. Backend
admission correctly refused work. These samples establish a roughly 28 KiB
peak drop, but contain no per-operation attribution. They do **not** establish
which allocator owned each byte. No successful live backend transaction was
reported. The station's last known network address was unreachable during
this software change, so new physical heap readings are not claimed here.

## Owners and changes

| Owner | Previous allocation/lifetime | Repair and evidence |
|---|---|---|
| LVGL widget allocator | A 65,536-byte `work_mem_int` array permanently occupied internal `.bss`, separately from the already external draw buffers. This is baseline pressure, not the post-httpd drop. | Keep the same bounded TLSF allocator and pool size, supply its pool explicitly from SPIRAM. Preflight failure returns UI initialization failure before `lv_init`; no internal fallback. The linked ELF no longer contains `work_mem_int`. |
| Saved configuration JSON | `parse_document` created a default-allocator document; moving it into `Impl::document` also moved the allocator, defeating the intended PSRAM owner. | Parse, migrate, import, rollback, persist and export with the service's stable SPIRAM allocator under the configuration mutex. Repeated load/save/export/failed-import tests verify nonzero persistent usage and release of candidates. |
| Network task boot settings | A complete `Configuration`, including credentials, certificates, profiles and mappings, remained alive across the perpetual network loop. | Limit the boot copy to Wi-Fi initialization. The network service retains only its own device/Wi-Fi settings. |
| REST snapshot copies | Health/status/spool/printers/toolheads copied full `WorkflowSnapshot` graphs; config/toolheads copied configuration graphs. Top-level `make_external` did not move their nested string/vector/map allocations. | Encode directly from consistent borrowed views under the owner mutexes. No deep copies of those graphs on these HTTP paths. Configuration → workflow is the only nested visitor-lock order. Callbacks only encode JSON, never perform I/O or retain references. |
| Log response | Snapshot vector copied up to 32 inline log records (roughly 7 KiB) before JSON encoding. | Encode the bounded ring directly under its mutex, preserving order, cursors, redaction and dropped counts. |
| Serialized API output | Default `std::string` payload plus a second envelope string; operation polling and WS return paths also copied output. | Fallible SPIRAM `JsonBody` writer. Reserve once, serialize directly, wrap the envelope in place, move ownership through the router, retain until synchronous send returns. A 24 KiB/200-cycle test proves one output allocation per cycle, pointer identity through wrapping/moving and complete release. |
| HTTP/WS transport | httpd/lwIP own session state, TCP queues and packet buffers. WS already has a single bounded 4 KiB asynchronous send batch. | Retain five session slots/two WS clients, serialized browser REST and the existing static-asset `Connection: close` policy. Record opens/closes, payload/send/release boundaries and counters. WS copies into its existing batch before releasing serialized output; completion releases batch ownership. |

The first production build reduced `.dram0.data + .dram0.bss` from 175,428 to
109,924 bytes: **65,504 bytes reclaimed**, net of the new counters. CI now caps
static internal RAM at 120,000 bytes and checks the actual ELF for the external
pool provider and absence of the old internal array. Stack reservations and
18,000/6,144 admission thresholds are unchanged. This saving provides substantial
room for the previously observed 28 KiB transient without weakening admission.
It is not a substitute for the connected physical test below.

### Nested C++ allocation audit

Canonical configuration strings (including two bounded 4 KiB CA certificates),
eight toolhead profiles and up to 64 identity mappings still use their existing
standard-container allocation policy. `VersionedConfiguration` has the same
graph. Moving the containing object does not change this. Browser serialization
now borrows them; saving serializes to an explicit PSRAM body. Boot document
reads and the storage readback check still use bounded default strings.

Workflow spools/candidates, printer/toolhead vectors, spool `extra_json` map
nodes and optional string/error payloads likewise retain their existing bounded,
admission-checked canonical storage. Their web views now go directly into PSRAM
JSON containers rather than duplicating the C++ containers. Backend/UI callers
that explicitly request owned snapshots can still copy these bounded members;
the `make_external` before/after traces expose that cost. `optional` has no
separate control allocation. Configuration decoding now uses a fallible PSRAM
unique owner instead of `make_shared`, eliminating its default control block.
NFC shared ownership, read images and task architecture are untouched.

Backend status/error strings and request/header strings are small bounded
default allocations, not full response storage. The pinned Arduino SDK's
default allocation policy prefers internal memory for allocations up to
4,096 bytes and can prefer PSRAM for larger allocations; therefore a plain
`std::string` is **not** proof of either placement. Explicit response/JSON/pool
allocations use `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` without internal fallback.
No global allocator policy or unsafe throwing STL allocator was introduced.

## Reading one serial capture

`MEMORY` lines include milliseconds, task, owner, phase, internal
free/min/largest, PSRAM free/min/largest, owned bytes, JSON allocator outstanding
bytes, active/max sockets, WS clients, active REST and browser-reported
active/queued/max REST. No paths, payloads or credential values are printed.
httpd handles REST synchronously and has no application REST queue
(`rest_queued=0`); TCP receive queues are separate. Client gauges are bounded
telemetry, not trusted authorization or a measure of other browsers.

Follow `lvgl_pool_*`, `config_initialize_psram`, `mdns`, `ntp`, `http_client`,
`static_asset_send`, then the named status/health/config/spool/printers/toolheads/
diagnostics/logs owners. `http_client accepted_before_session` is the earliest
httpd open callback: the TCP accept has already occurred, so compare the preceding
sample too. `api_json encoded` reports live JSON bytes; the subsequent allocator
`released` must return to zero. Configuration's balance intentionally remains
nonzero while its persistent document exists.

`rest serialized_before_send` and `after_send` retain the PSRAM body; only
`rest send_and_response_released` is the settled per-request sample. For WS,
follow event serialization, `before_batch_copy`, `after_batch_copy`, and
`send_complete`. `browser_initial_sync complete` follows consumption of all
initial responses, including logs, and an additional scheduler-controlled health
read. The browser regression test checks both that ordering and max REST = 1.
Every `make_external` logs the caller and object size before/after construction;
destruction logs after freeing the object and its nested members. Interleaved
task events must be considered when assigning a global-heap delta to an owner.

## Next physical test: real backends

Use the production PR artifact with existing saved configuration. No separate
NFC bring-up or diagnostic-only firmware is needed.

1. Capture serial from configured reboot through opening the browser and its
   `browser_initial_sync complete` marker. Confirm `lvgl_pool_psram` and the
   unchanged healthy read-only NFC/shared-task state.
2. Keep the browser connected. Confirm settled internal free is comfortably
   above 18,000 and largest block above 6,144; record actual values and socket
   counts. Expect substantial improvement from the 65 KiB baseline saving.
3. Test the real saved Spoolman and FilaBridge endpoints. Require actual
   `before_http → after_http → before_parse → after_parse → body_released →
   document_released → probe_complete` cycles. Repeated admission rejection
   is a failed acceptance result, even if the interface stays responsive.
4. Continue into the existing spool resolution/weight/toolhead assignment and
   verified readback procedure. Across repeated cheap probes and full discovery,
   verify approximately the same settled baseline, zero transient JSON balance,
   stable socket counts, responsive scale/UI and the preserved stack margins.

Record the actual firmware SHA, endpoints/versions without secrets, free/min/
largest internal and PSRAM values, and every task's high-water value in the PR.
Physical backend acceptance remains open until this evidence exists.

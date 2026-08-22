# Local web UI and API

The station serves a dependency-free administration UI at `http://<station-ip>/`
and a transport-neutral JSON API under `/api/v1`. The UI covers device status,
scale controls and calibration, NFC status, spool resolution, printers and
toolheads, backend probes, configuration, diagnostics, logs, controlled device
actions, and the validated A/B firmware-update workflow.

## Validation status

The current firmware preserves the Phase 11 routes and adds four bounded
network provisioning routes, for 30 metadata-declared routes. The hardware
stabilization pass adds bounded browser scheduling, explicit configuration
state, one managed live connection per tab, fallback polling, and a read-only
transport self-test. Browser request epochs, payload revision guards, and socket
identity checks prevent stale REST/WebSocket responses from replacing newer
state. The grouped contract/security review and physical browser/LAN matrix are in
[release-validation.md](release-validation.md).

The 2026-08-22 pre-commit stabilization gates pass 262/262 native cases across
twenty suites in 00:06:27.085 and 33/33 deterministic browser-transport cases.
Embedded JavaScript syntax validation passes for the 119,132-byte shipped
source, and the warning-free WT32 build uses 170,752/327,680 RAM bytes (52.1%)
and 2,090,973/5,242,880 flash bytes (39.9%). Stack and pre-commit factory-bundle
measurements are recorded in [release-validation.md](release-validation.md).
Portable router, parser, patch, and bounded-ledger logic executes in host tests;
the embedded browser, production context, HTTP/WebSocket transport, and
device-control integration compile and link but were not executed on target. No
browser, target-LAN, reset-recovery, NFC, display, touch, scale, or other
physical-hardware result is claimed here.

## Network and security boundary

The local server is plain HTTP and does **not** provide TLS. A bearer token sent
to it is not encrypted by the station. Use the interface only on an isolated,
trusted LAN protected by link-layer encryption such as WPA2 or WPA3. Do not
port-forward it, expose it to the public Internet, or use it on a network where
untrusted clients can capture or alter traffic.

All read-only `GET` routes are public, including operation status and the
read-only WebSocket event stream. When the station has a nonempty local API
token, protected mutations require an exact `Authorization: Bearer <token>`
value. When the token is blank, local mutations are allowed without bearer
authentication: **Local API authentication: DISABLED** and **Local browser
control: ENABLED**. This trusted-LAN mode is healthy and complete; the empty
optional token does not degrade health, block setup completion, or disable
scale, configuration, backend, device-control, or update mutations. Even when a
token is configured, `POST /api/v1/network/scan`
and `POST /api/v1/network/connect` are also authorized for a socket whose peer
is verified as an active `192.168.4.0/24` setup-AP client. That authority is
never accepted from a request header and cannot reach any other mutation.
Authentication policy is checked before mutation JSON is parsed or work is
submitted.

The optional token may be created through the physically local setup AP or the
masked touchscreen field. With no saved token, leaving the field blank keeps
local API authentication disabled without prompting or generating a credential.
When a token already exists, a blank touchscreen field preserves it. Recovery
provisioning cannot replace it. Once configured, an
authenticated `PATCH /api/v1/config` can rotate it or explicitly clear it. The browser prompts
for the current token only when a mutation needs it, retains it only in
JavaScript memory for that tab, clears it after HTTP 401, and never stores it in
local storage, session storage, cookies, a URL, or a prefilled form.

A nonempty token is 16–128 ASCII characters drawn from letters, digits, `-`,
`.`, `_`, and `~`. Static responses apply a restrictive policy for
self-hosted scripts and styles, `nosniff`, no-referrer, frame denial, and
`no-store`. The UI connects events to the current `location.host` and inserts
remote and device text with DOM text nodes rather than HTML parsing.

## Route catalog

The router has exactly 30 metadata-declared REST routes. `/api/v1/events` is a
separate WebSocket transport endpoint and is not included in that count.

### Station and diagnostics

| # | Method and path | Purpose |
| ---: | --- | --- |
| 1 | `GET /api/v1/status` | Combined system, backend, spool-generation, printer-revision, and operation-revision status. |
| 2 | `GET /api/v1/device` | Device identity, local address, and firmware/build metadata. |
| 3 | `GET /api/v1/health` | Local-service health, backend degradation, optional-authentication/control state, and NFC availability. |
| 3a | `GET /api/v1/network` | Safe connection, setup AP, scan progress/results, hostname, and configuration revision. |
| 3b | `POST /api/v1/network/scan` | Start one asynchronous bounded scan; setup-AP clients or normal conditional-authentication policy. |
| 3c | `POST /api/v1/network/connect` | Revision-checked SSID/password/hostname and optional-token provisioning; setup-AP clients or normal conditional-authentication policy. |
| 3d | `POST /api/v1/network/setup-mode` | Deliberate setup-AP activation under normal conditional-authentication policy. |
| 4 | `GET /api/v1/diagnostics` | System, memory/stack/transport, scale, backend, queue, operation, and NFC diagnostics. |
| 5 | `GET /api/v1/logs` | Bounded redacted log history, cursors, drop count, and history-gap state. |

### Scale

| # | Method and path | Purpose |
| ---: | --- | --- |
| 6 | `GET /api/v1/scale` | Current scale snapshot and command-queue depth. |
| 7 | `POST /api/v1/scale/tare` | Queue a tare; body is `{}`. |
| 8 | `POST /api/v1/scale/calibrate` | Queue calibration with `{"reference_grams": number}` in `(0, 5000]`. |

### NFC and OpenPrintTag

| # | Method and path | Purpose |
| ---: | --- | --- |
| 9 | `GET /api/v1/nfc` | NFC reader status. |
| 10 | `GET /api/v1/nfc/tag` | Current decoded tag snapshot. |
| 11 | `POST /api/v1/nfc/read` | Queue an explicit read; body is `{}`. |

### Spool, printers, and toolheads

| # | Method and path | Purpose |
| ---: | --- | --- |
| 12 | `GET /api/v1/spool` | Current spool workflow and weight-reconciliation state. |
| 13 | `GET /api/v1/printers` | FilaBridge printer snapshot and revision. |
| 14 | `GET /api/v1/toolheads` | Flattened toolhead assignments and local profiles. |
| 15 | `POST /api/v1/toolheads/{id}/assign` | Queue a guarded assignment to toolhead ID `0`–`4`. |
| 16 | `POST /api/v1/toolheads/{id}/unassign` | Queue a guarded unassignment from toolhead ID `0`–`4`. |

### Configuration and backends

| # | Method and path | Purpose |
| ---: | --- | --- |
| 17 | `GET /api/v1/config` | Read the revisioned, allowlisted, redacted configuration view. |
| 18 | `PATCH /api/v1/config` | Queue a typed partial configuration update guarded by `expected_revision`. |
| 19 | `POST /api/v1/backends/test` | Queue Spoolman and FilaBridge probes; body is `{}`. |

### Update and device control

| # | Method and path | Purpose |
| ---: | --- | --- |
| 20 | `GET /api/v1/update` | Read update generation, state, partitions, current/candidate build, progress, validation, rollback, capabilities, and the last bounded error. |
| 21 | `POST /api/v1/update/upload` | Stream one firmware binary to the inactive-slot owner; this route never enters the buffered JSON router. |
| 22 | `POST /api/v1/update/reboot` | Activate the exact validated candidate and queue its owned reboot. |
| 23 | `POST /api/v1/update/cancel` | Cancel the exact staged candidate while retaining the running firmware. |
| 24 | `POST /api/v1/device/reboot` | Queue reboot with exact body `{"confirmation":"REBOOT"}`. |
| 25 | `POST /api/v1/device/factory-reset` | Queue reset with exact body `{"confirmation":"FACTORY RESET"}`. |

### Operation status

| # | Method and path | Purpose |
| ---: | --- | --- |
| 26 | `GET /api/v1/operations/{id}` | Read one positive, canonical decimal operation ID. |

## JSON protocol and HTTP status

Every REST response is a versioned JSON envelope. A successful read is:

```json
{"api_version":"v1","ok":true,"data":{}}
```

An error is:

```json
{
  "api_version": "v1",
  "ok": false,
  "error": {
    "code": "stable_machine_code",
    "message": "bounded human-readable detail",
    "retryable": false
  }
}
```

Clients should branch on the HTTP status and `error.code`, not parse the
message. The principal statuses are:

| Status | Meaning |
| ---: | --- |
| `200` | Read succeeded. |
| `202` | Mutation was accepted into an owner queue, or an idempotent retry reused the original operation. This is not completion. |
| `400` | Invalid path shape, headers, JSON, field set, type, value, or operation ID. |
| `401` | Authentication is enabled and the bearer credential is missing, malformed, or wrong. A blank configured token does not produce 401. |
| `404` | Route/version is unavailable, or an operation is no longer in bounded history. |
| `405` | The path exists but does not support that method; `Allow` is returned. |
| `408` | The complete request body was not received within the bounded transport deadline. |
| `409` | Stale revision/state, idempotency-key conflict, unstable scale, or another state conflict. |
| `413` | Global or route-specific body bound was exceeded. |
| `415` | Firmware upload media type is not `application/octet-stream`. |
| `422` | A context-level configuration/domain validation or tag validation failed. |
| `500` | Internal routing, serialization, or snapshot safety failure. |
| `502` | Backend authentication, API contract, or response failed. |
| `503` | Network, backend, scale, NFC, or operation queue is unavailable. |
| `507` | Persistent storage failed. |

Unsupported `/api/<version>/...` requests return HTTP 404 with
`unsupported_api_version`; only `v1` is defined.

## Mutation headers, operations, and idempotency

Every mutation requires the browser-source header and an idempotency key.
Bearer authentication is required when a token is configured, except for
network scan/connect requests whose transport peer was verified on the active
setup AP. Buffered JSON mutations require all of the following:

- `Authorization: Bearer <current-token>` when a token is configured, except
  for the two scoped setup-AP routes;
- `Content-Type: application/json` with optional UTF-8 charset;
- `X-OpenTag-Request: web`;
- an `Idempotency-Key` of 1–64 letters, digits, `-`, `_`, `.`, or `:`;
- one JSON object whose fields exactly match the selected route.

Firmware upload is the deliberate exception to the JSON-body rule. Its exact
transport contract is documented under **Validated A/B update surface** below.

An accepted mutation returns HTTP 202:

```json
{
  "api_version": "v1",
  "ok": true,
  "data": {
    "operation_id": 42,
    "kind": "configuration",
    "state": "queued"
  }
}
```

Poll `GET /api/v1/operations/42` until `state` is `succeeded`, `failed`, or
`confirmation_required`. Intermediate states are `queued` and `running`.
Operation records include creation/update uptime, a bounded message, and a
structured error when present. The embedded browser polls only the returned ID
at about one-second intervals and stops after 45 seconds. Reboot and factory
reset are receipt-only in the browser: it reports acceptance and enters its
reconnect flow instead of polling across the intentional restart. A polling
timeout does not prove that the owner task failed.

Operation history is a volatile 24-record bounded registry. It never overwrites
a queued or running record; an empty or terminal slot may be reused, and a
registry containing 24 nonterminal operations rejects new work with a retryable
503 instead of losing an active receipt. A missing or reused ID returns 404.
Reboot clears the history, and reboot/reset operations naturally interrupt the
connection before a terminal result can be observed.

Idempotency is a volatile 32-entry ledger with a ten-minute TTL. The digest
covers mutation kind, path, and the exact validated request body. Reusing the
same key for the same payload during retention returns the original operation
ID; using it for a different payload returns HTTP 409 `state_conflict`. More
than 32 unique mutations inside the retention window cannot evict a live key:
the station rejects new work before creating side effects until a slot expires.
Expired slots are reused deterministically, and restart clears the ledger.
Clients must not treat it as durable deduplication.

## Browser request and configuration lifecycle

Before hardware stabilization, one tab could begin with nine concurrent reads,
start further phase-two reads, repeat three reads when the WebSocket opened, and
treat the first heartbeat as an unknown event that requested eight more reads.
That produced roughly 25 startup REST transactions plus the WebSocket, then
another eight-read burst on each 15-second heartbeat. It competed against a
four-socket, two-backlog server with LRU purging and one-second socket waits.

The embedded client now uses one bounded queue with these priorities:

| Priority | Work |
| ---: | --- |
| 1 | Mutation receipts, operation polling, configuration save, scale/device/update controls |
| 2 | `device`, `network`, `config`, `scale`, and `health` reads |
| 3 | `status`, `spool`, `printers`, `toolheads`, and `update` reads |
| 4 | Logs, diagnostics, and unavailable NFC reads |

At most two ordinary REST requests dispatch at once, and at most one background
request may occupy those slots so priority-one work retains headroom. Identical
pending GETs share one result; there cannot be concurrent `GET /scale` or
`GET /config` requests. A newer refresh may supersede its older queued
background read, but never a mutation. The queue is bounded and dispatch
timeouts begin only when a request actually leaves it.

Startup is progressive and ordered: load `device`, `network`, `config`, and
`scale`; establish the WebSocket; then load secondary resources through the
scheduler. Heartbeats only prove liveness and do not trigger REST. Manual
refresh is also sequenced rather than issued as a large `Promise.all`.

Configuration has explicit `UNLOADED`, `LOADING`, `READY`, and `ERROR`
states. Fields, Save, import, and export remain disabled until `READY`.
Opening Configuration automatically retries a missing/failed load and displays
the actual transport, HTTP, or API error. Dirty edits are not overwritten by a
late background response. A successful PATCH is not reported as persisted until
its operation completes and a forced `GET /config` returns the new revision.

Each mutation creates one idempotency key and one exact body, submits once, and
polls only a known receipt. An interrupted receipt is treated as uncertain and
is not blindly replayed; the matching control remains protected from a duplicate
manual submission. Transient polling GET failures retry without replaying the
mutation. Priority-one operation work pauses/yields background refresh and
reports transport, HTTP, API, operation, and domain/precondition failures
separately. Firmware upload enters a maintenance mode that pauses background
REST and the live/fallback loop until the upload finishes or aborts.

## Diagnostics local interface self-test

**Run Local Interface Self-Test** performs sequential, read-only checks of
`/device`, `/health`, `/network`, `/config`, `/scale`, `/spool`,
`/printers`, `/toolheads`, `/logs`, `/diagnostics`, and `/update`, then
reports the existing WebSocket's connectivity. Each row shows endpoint, HTTP
result, latency, API-envelope result, and a bounded error. Response bodies are
not rendered or retained by the report, it does not open a second WebSocket,
and it performs no mutation or secret-bearing request.

## Live WebSocket events

Connect to `ws://<station-ip>/api/v1/events`. The endpoint is read-only and is
not routed through the REST router.

- `{"type":"scale","data":...}` is published at most every 500 ms (2 Hz).
- `{"type":"heartbeat","data":{"uptime_ms":...}}` is published about every
  15 seconds.
- `{"type":"update","data":...}` publishes a bounded update snapshot when
  update state/progress changes. The browser also reloads update, device, and
  health state whenever the WebSocket reconnects after a reboot.
- If a scale snapshot cannot be encoded safely, the server emits a bounded
  `invalidate` event so the client can refresh the resource.
- The server permits at most two WebSocket clients within seven total open HTTP
  sockets. It enumerates current descriptors for each publication and uses one
  fixed shared asynchronous batch with at most two in-flight sends. Publications
  coalesce while that batch is busy; queue/send failures and excess sessions are
  closed. There is no per-client task or unbounded queue.
- Incoming frames are not an API. Every post-handshake data or control frame is
  rejected and the session is closed without reading its payload. Outgoing event
  JSON is at most 4096 bytes.

Each tab owns exactly one WebSocket object and one reconnect timer. Connection
attempts have an eight-second deadline; a connection with no heartbeat or event
for 35 seconds is stale and is replaced. Retry delay grows exponentially from
one to 30 seconds with bounded jitter and resets after verified traffic. The
client cancels stale timers/sockets across visibility changes, offline/online
events, page navigation, reload, and station reboot, so an obsolete socket
cannot update current state.

Scale and update events update only their relevant resource. A heartbeat updates
liveness only; an invalid/invalidate event requests only the named or required
resource. If live updates remain unavailable, a single scheduler-driven fallback
loop polls scale every two seconds and rotates health/network/update reads at a
lower rate. The UI distinguishes **Connecting**, **Connected**,
**Disconnected — retrying**, and **Live updates unavailable — using polling**.
WebSocket loss never disables REST controls or targeted operation polling.

Two simultaneous tabs fit the server's documented WebSocket limit and socket
budget. Excess live clients are closed and must fall back to REST rather than
crashing the station; each tab still obeys its own two-request scheduler.

## Revisioned and redacted configuration

`GET /api/v1/config` is built from an explicit allowlist rather than the stored
JSON document. It returns a boot-local runtime `revision`, nonsecret settings,
and only these credential-state flags:

- `web.access_token_configured`;
- `wifi.password_configured`;
- `spoolman.authentication_token_configured` and `custom_ca_configured`;
- `filabridge.authentication_token_configured` and `custom_ca_configured`.

It never returns passwords, bearer/backend tokens, authorization values, or CA
certificate text. The router rejects the entire snapshot if a forbidden secret
key reaches serialization.

`PATCH /api/v1/config` requires the revision read by the editor as
`expected_revision`. Both the request-side proposal and the serialized
configuration worker compare that revision. A stale proposal returns HTTP 409
without a partial write; reload the configuration and deliberately merge/retry.
The revision is a boot-local compare-and-swap token, not a persisted sequence.

Sections are typed partial objects for `device`, `wifi`, `web`, `spoolman`,
`filabridge`, `scale_profile`, `toolheads`, and `reconciliation`. Unknown fields,
empty section objects, wrong JSON types, unsafe text, invalid ranges, and a
patch with no changed section are rejected. Omitted fields preserve their
current values. For Wi-Fi password, backend token/CA material, and
`web.access_token`, an explicit empty string clears only that credential; a
valid nonempty value replaces it. Omitting the credential preserves it.
Including `toolheads` replaces the complete bounded profile list.

## Assignment and unassignment safety

Assignment accepts exactly these eight fields:

```json
{
  "printer_id": "printer-a",
  "expected_spool_id": 123,
  "expected_current_spool_id": 45,
  "expected_printer_state": "idle",
  "spool_generation": 9,
  "printer_revision": 17,
  "replace_occupied_confirmed": false,
  "advanced_override": false
}
```

`expected_current_spool_id` may be `null` for an expected empty toolhead.
Unassignment accepts exactly six fields: it omits `expected_spool_id` and
`replace_occupied_confirmed`, and requires a positive
`expected_current_spool_id`.

The workflow checks spool generation, resolved spool ID, printer revision,
printer identity/state, and current assignment before acting. The backend is
then refreshed immediately. Occupied replacement requires an explicit
confirmation flag; active or unverified printer states require an explicit
advanced override. A missing confirmation produces the terminal
`confirmation_required` state instead of performing the mutation. Queued
assignment/unassignment work expires after 15 seconds rather than applying a
stale destructive request.

After FilaBridge map/unmap succeeds, the station performs an independent
readback. Assignment succeeds only when the requested spool is observed (or was
already present); unassignment succeeds only when the mapping is absent (or was
already absent). Local workflow state is not advanced on failed verification.

## NFC behavior in Phase 9

The ST25R3916B transport remains compile-time disabled until the exact module,
wiring, and RFAL binding are resolved. All three NFC API routes exist, but
`GET /api/v1/nfc`, `GET /api/v1/nfc/tag`, and authenticated
`POST /api/v1/nfc/read` explicitly return HTTP 503 `nfc_unavailable`. The UI
shows the reader as unavailable rather than simulating a tag or silently
returning an empty result.

## Reboot and factory reset

Both controls require bearer authentication when a token is configured, plus
normal mutation headers, idempotency, and exact case-sensitive confirmation
text. Reboot preserves data and schedules an ESP restart after a bounded
response-flush delay.

Factory reset has deliberately narrow storage scope. It removes only:

- `/configuration.bak`;
- `/configuration.new`;
- `/configuration.json`;
- all key/value state in the station-owned `opentag` NVS namespace, including
  configuration/calibration mirrors and local boot state.

It does not format LittleFS, erase firmware/OTA partitions, clear unrelated NVS
namespaces, or perform a firmware downgrade. Before deletion it persists a
`resetPending` intent marker in the separate station-owned `opentagCtl`
control namespace. The marker itself blocks automatic formatting after a mount
failure. Storage writers remain blocked until restart. The marker stays durable
until the three documents and the `opentag` application namespace are cleared
and the `fsProvisioned` no-format guard is restored; only then is the marker
removed. If any post-marker step is interrupted or fails, the device-control
owner schedules a reboot and early boot repeats the idempotent recovery path.
Successful recovery returns to first-run setup. The setup-AP connect route may
create a missing optional token but cannot replace an existing token; when the
token remains blank, unrelated local mutations are intentionally tokenless.

Power-loss timing, exact erase scope, marker recovery, restart, and first-run
return still require physical target validation.

## Validated A/B update surface

`GET /api/v1/update` is public like the other safe read snapshots. It exposes a
nonnegative monotonic `generation` (`0` before the first upload and positive
thereafter), update `state`, the upload operation ID,
running/boot/inactive partition labels and capacities, current and candidate
version/Git/build metadata, declared/calculated SHA-256, byte progress,
validation result, rollback status, capability booleans, and one bounded last
error. It never exposes a flash address or accepts a client-selected partition.

Capabilities include `owner_ready`; upload, cancel, and reboot are unavailable
when the sole OTA owner is not ready. `maximum_image_bytes` is `0` when no
inactive application topology exists, rather than implying a generic 5 MiB
target.

`POST /api/v1/update/upload` is registered as a streaming-binary route with a
hard 5 MiB ceiling matching one application slot. It is explicitly rejected by
the normal 16 KiB buffered router. The dedicated transport requires:

- `Authorization: Bearer <current-token>` when a token is configured;
- `X-OpenTag-Request: web`;
- a valid `Idempotency-Key`;
- `Content-Type: application/octet-stream`;
- a nonzero explicit `Content-Length` no larger than the inactive slot;
- `X-OpenTag-Image-SHA256: <64 lowercase hexadecimal characters>`;
- `X-OpenTag-Expected-Generation: <canonical nonnegative decimal generation>`.

Candidate version, Git SHA, build date, board, and project identity come only
from the embedded image metadata; request headers and the local filename are
not trusted or forwarded. The browser calculates SHA-256 before upload, sends
the `File` object directly with `XMLHttpRequest`, and shows transport progress
as unvalidated until the station reports validation and inactive-slot install.
It never stores the bearer token or firmware body.

Reboot and cancel are normal bounded JSON mutations. Each body contains exactly
four fields:

```json
{
  "upload_operation_id": 91,
  "expected_generation": 7,
  "expected_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "confirmation": "REBOOT INTO UPDATE"
}
```

Cancel uses the exact confirmation `CANCEL UPDATE`. A successful upload reaches
`ready_to_reboot` only after validation and inactive-slot installation, but the
boot partition remains unchanged until the explicit reboot mutation accepts the
exact candidate. If activation was durably selected but the restart response is
ambiguous, the same exact reboot request may be retried only while the boot
target still matches the validated inactive target and that target is not yet
running. The exact request may also retry first-update rollback seeding when
durable activation intent and the full validated target topology prove that
boundary. Cancellation remains forbidden after activation intent. Both IDs
must be positive, the digest must be exactly 64 lowercase hexadecimal
characters, and every precondition must still match the device-owned candidate.
This prevents a stale tab, old operation receipt, or replayed reboot from
activating or cancelling a newer image. Upload abort in the browser closes the
transfer so the service can abort the incomplete OTA handle; staged-candidate
cancellation uses the JSON endpoint. Reboot is receipt-only in the UI, which
reconnects and resumes status through candidate boot, health validation,
confirmation, or rollback.

## Resource bounds

| Resource | Bound |
| --- | ---: |
| Global request body / configuration PATCH | 16 KiB |
| Firmware streaming upload | 5 MiB maximum; never buffered by the JSON router |
| Tare, NFC read, backend probe, reboot, factory reset body | 256 bytes each |
| Calibration body | 512 bytes |
| Assignment or unassignment body | 2048 bytes |
| Request path | 256 bytes |
| Request headers | 16 headers, 1024 bytes total; collected value at most 512 bytes |
| JSON nesting | 8 levels |
| Application snapshot / complete response body | 24 KiB / 32 KiB |
| HTTP sockets / WebSocket clients | 7 / 2; LRU purge disabled |
| WebSocket post-handshake input / output | rejected / 4096 bytes |
| HTTP server task stack / backlog | 20,480 bytes / 5 connections |
| Receive / send wait; buffered JSON body deadline | 5 seconds / 5 seconds; 5 seconds absolute |
| Firmware upload buffers / deadlines | one 4096-byte HTTP receive buffer plus four 4096-byte OTA command-slot buffers; 5 seconds without receive progress / 180 seconds absolute through validation |
| Idempotency / operation / log history | 32 / 24 / 32 entries |
| Embedded HTML / CSS / JavaScript | Compile-time bounded by `web_assets.hpp`; `tools/check_web_assets.py` extracts, syntax-checks, and reports the JavaScript byte count |

GET routes accept no body. The transport reads the declared body exactly and
rejects incomplete, oversized, or over-deadline input. Route limits are enforced
again by the transport-neutral router. Responses are `no-store`, and no API
owner allocates an unbounded request queue for browser clients.

## Physical validation and known limitations

Before this stabilization can be called target-validated, exercise the assembled firmware
on the WT32-SC01 Plus over an isolated encrypted LAN:

1. Verify blank-token health, setup completion, scale/config/backend/device
   mutations, and the displayed DISABLED/ENABLED authentication state. Then set
   an optional token and test missing/wrong/correct authentication, rotation,
   explicit clear, setup-AP recovery, and touchscreen recovery.
2. Run the Diagnostics local interface self-test; every REST row and the
   existing WebSocket must pass without exposing a response body or secret.
3. Inspect wire traffic, configuration snapshots, logs, and browser state for
   secret leakage; remember that HTTP traffic itself remains plaintext.
4. Race two configuration editors and stale assignment snapshots; confirm CAS,
   confirmation, expiry, and exact FilaBridge readback behavior.
5. With one tab, perform 20 manual refreshes, F5, navigate away/back, transition
   setup AP to LAN, disconnect/reconnect LAN, and reboot the station. Confirm one
   WebSocket, bounded REST concurrency, fallback polling, and recovery without
   stale state or stuck controls.
6. Run two tabs long enough to cover both WebSockets and simultaneous controls;
   then attempt an excess client. Confirm graceful fallback/rejection, socket
   reclamation, and no device reset, watchdog, or task starvation.
7. Verify scale events/tare/calibration against real hardware without starving
   LVGL, network, configuration, or backend work.
8. Confirm all NFC routes stay explicitly unavailable on the wiring-gated build.
9. Exercise reboot and factory reset, including power interruption at each erase
   stage, exact-scope retention, durable recovery, and return to first-run.
10. Confirm responsive/accessibility behavior and safe rendering of backend,
   printer, spool, and log strings in supported desktop and mobile browsers.

Known limitations include plain HTTP with public reads, small connection and
history rings, volatile idempotency/operation state, no physical NFC transport,
unsigned firmware images, and no completed target-browser/LAN/rollback hardware
validation. Pinned ESP-IDF returns the first matching request-header value but
does not expose a duplicate count to the upload handler, so duplicate-header
behavior must be characterized on the target rather than claimed as rejected.
The legacy NVS calibration mirror and authoritative LittleFS configuration writes
are safety-ordered but not one power-atomic transaction. ESP-IDF header receipt
uses bounded socket waits rather than a separate whole-header wall-clock
deadline.

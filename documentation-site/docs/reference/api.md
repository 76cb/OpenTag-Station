# REST API

The station serves a JSON API on its local network. The browser page uses the same API.

- Base URL: `http://<station>/api/v1` (`http://opentag-station.local/api/v1` with the default
  hostname). Plain HTTP only; there is no HTTPS listener.
- The route table is `routes` in
  [`src/web/api_router.hpp`](https://github.com/76cb/OpenTag-Station/blob/main/src/web/api_router.hpp).
- Paths are exact. A `?` or `#` in the path is rejected with `400 invalid_request`.

## Authentication

| Request | Rule |
|---|---|
| `GET` routes, WebSocket | Never need a token. |
| `POST` / `PATCH` routes | Need `Authorization: Bearer <token>` when `web.access_token` is set. When it is empty, every request on the network is accepted. |
| `POST /network/scan`, `POST /network/connect` | Also accepted without a token from a client joined to the setup network while setup mode is active. |

The token is empty, or 16–128 characters of `A–Z a–z 0–9 - . _ ~`. The header value must be
`Bearer`, one space, then the token with no further whitespace. A failed check returns
`401 authentication_required` with `WWW-Authenticate: Bearer`.

How to set the token: [Access token and network safety](../configuration/security.md).

## Requests and responses

Responses carry `Content-Type: application/json; charset=utf-8`, `Cache-Control: no-store` and
`X-Content-Type-Options: nosniff`.

```json
{"api_version": "v1", "ok": true, "data": {}}
{"api_version": "v1", "ok": false,
 "error": {"code": "validation_failed", "message": "…", "retryable": false}}
```

`error.message` is at most 512 bytes.

### Read requests

`GET` requests must not have a body (`400 unexpected_body`).

### Change requests

Every `POST` and `PATCH` except the firmware upload needs these headers, each exactly once:

| Header | Value |
|---|---|
| `Content-Type` | `application/json` (optionally `; charset=utf-8`) |
| `X-OpenTag-Request` | `web` |
| `Idempotency-Key` | 1–64 characters of `A–Z a–z 0–9 - _ . :` |

The body is one JSON object, nested at most 8 levels. Unknown fields are rejected. Routes that
take no parameters need the empty object `{}`.

An accepted change returns `202` with a receipt; the work happens afterwards:

```json
{"api_version": "v1", "ok": true,
 "data": {"operation_id": 17, "kind": "scale_weigh", "state": "queued"}}
```

Poll `GET /operations/{id}` until `state` is `succeeded`, `failed` or `confirmation_required`.
The station keeps the 24 most recent operations in memory; older ones and all operations from
before a restart return `404 operation_not_found`.

### Idempotency

The station remembers up to 32 keys for ten minutes each, in memory only.

| Situation | Result |
|---|---|
| Same key, same method, path and body, within ten minutes | `202` with the original `operation_id`; nothing runs twice. |
| Same key, different request | `409 state_conflict` |
| All 32 slots hold live keys | `409 state_conflict`, `retryable: true` |

If a receipt is lost, do not send the request again under a new key. Poll the operation if you
have its ID, until it finishes or returns `404`. If no ID was received, repeat the identical
request with the same key, or wait until the ten-minute window has passed.

## Routes

"Token" means the bearer rule above applies. Body limits are in bytes.

### Status and diagnostics

| Method and path | Auth | Response `data` |
|---|---|---|
| `GET /status` | – | `system`, `backends.spoolman`, `backends.filabridge`, `spool_generation`, `printer_revision`, `operations_revision` |
| `GET /device` | – | `device` (`hostname`, `hardware_id`, `ip_address`, `local_url`), `build` (`version`, `git_sha`, `build_date`, …) |
| `GET /health` | – | `status` (`healthy` / `degraded` / `unhealthy`), `local_services_ready`, `backend_degraded`, `local_api_authentication_enabled`, `local_browser_control_enabled`, `nfc_available` |
| `GET /diagnostics` | – | `system`, `scale`, `backends`, `queues`, `operations`, `ota_owner_ready`, `nfc_available` |
| `GET /logs` | – | `logs[]` (`sequence`, `uptime_ms`, `level`, `source`, `message`, `truncated`, `redacted`), `oldest_cursor`, `latest_cursor`, `dropped_count`. At most 32 entries, 192 bytes each, lost on restart. |
| `GET /operations/{id}` | – | `operation_id`, `kind`, `state`, `created_at_ms`, `updated_at_ms`, `message`, optional `error` (`category`, `message`, `retryable`). `id` is a positive decimal without leading zeros. |
| `POST /backends/test` | Token | Body `{}` (≤ 256). Re-checks Spoolman and FilaBridge. |

Operation `state` is one of `queued`, `running`, `succeeded`, `failed`, `confirmation_required`.
The `kind` in an operation record can differ from the `kind` in the receipt (for example
`weight_update` for a `scale_update` receipt, `firmware_reboot` for `update_reboot`); match
operations by `operation_id`.

### Network

| Method and path | Auth | Body | Notes |
|---|---|---|---|
| `GET /network` | – | – | `system`, `networks[]` (`ssid`, `rssi_dbm`, `secured`), `config_revision`, `hostname`, `access_token_configured`, `local_browser_control_enabled` |
| `POST /network/scan` | Token or setup network | `{}` (≤ 256) | Results appear in `GET /network`. |
| `POST /network/connect` | Token or setup network | ≤ 1024, see below | Saves Wi-Fi settings and joins. |
| `POST /network/setup-mode` | Token | `{}` (≤ 256) | Starts the setup network. |

`POST /network/connect` fields:

| Field | Required | Rule |
|---|---|---|
| `expected_revision` | yes | `config_revision` from `GET /network`; a stale value returns `409`. |
| `ssid` | yes | 1–32 bytes |
| `password` | no | ≤ 64 bytes. Omitted with a new `ssid`: the saved password is cleared. Omitted with the same `ssid`: kept. |
| `hostname` | no | 1–63 characters of `a–z 0–9 -`, not starting or ending with `-` |
| `access_token` | no | 16–128 characters. A setup-network client cannot replace a token that already exists (`422`). |

### Scale

| Method and path | Auth | Body | Notes |
|---|---|---|---|
| `GET /scale` | – | – | `scale`, `command_queue_depth`, `weigh_sync` (`measurement_id`, `phase`, `message`, `spool_id`, `can_update`, …) |
| `POST /scale/weigh` | Token | `{}` (≤ 256) | Starts one measurement. |
| `POST /scale/update` | Token | `{"measurement_id": n}` (≤ 256) | Saves that measurement to Spoolman. `n` is the positive `weigh_sync.measurement_id`; no other field is allowed. Each measurement can be saved once. |
| `POST /scale/tare` | Token | `{}` (≤ 256) | Sets zero from the empty platform. |
| `POST /scale/calibrate` | Token | `{"reference_grams": n}` (≤ 512) | `0 < n ≤ 5000`. Tare first. |

Rules and tolerances: [Scale behaviour](scale.md) and [Spoolman integration](spoolman.md).

### Tag and spool

| Method and path | Auth | Body | Notes |
|---|---|---|---|
| `GET /nfc` | – | – | Reader and tag state. |
| `GET /nfc/tag` | – | – | The same state under `tag`. |
| `POST /nfc/read` | Token | `{}` (≤ 256) | Inert. Always fails with `503 nfc_unavailable`: the reader reads a tag when it is placed. Use `GET /nfc`. |
| `GET /spool` | – | – | `workflow`: `tag_lifecycle`, `stage`, `spool_generation`, `printer_revision`, `tag`, `spoolman`, `filabridge`, `candidates[]`, `spool`, `reconciliation`, optional `error` |
| `POST /spool/confirm` | Token | ≤ 512 | Exactly `spool_generation` (> 0), `spool_id` (> 0) and `confirmed: true`. Confirms one of `candidates[]` as the spool for the tag on the station. Needs a stable weight on the scale. |
| `GET /tag-writer` | – | – | Current writer view: `phase`, `message`, `operation_id`, `completed_blocks`, `total_blocks` and the data of the last action. |
| `POST /tag-writer` | Token | ≤ 4096 | `action` plus the fields of that action, below. |

`POST /tag-writer` actions:

| `action` | Fields | Purpose |
|---|---|---|
| `catalog` | `entity` (`vendor` / `filament` / `spool`), `offset`, optional `search`, `search_field` (`name` / `vendor`), `material`, `article_number`, `vendor_id`, `filament_id` | One page of the Spoolman catalog. |
| `create_spool` | `spool`: object with `filament_id` (> 0) and optional `initial_weight`, `remaining_weight` or `used_weight` (not both), `spool_weight`, `price`, `location`, `lot_nr`, `comment` | Create a spool in Spoolman. |
| `preview` | `spool_id` (> 0), optional `mode` (`update` writes only the consumed weight) | Read the tag on the station and build the content to write for that spool. |
| `write` | `uid`, `generation`, `target_checksum` (strings), `spool_id` (> 0), `previous_spool_id` (≥ 0) | Write the previewed content to the tag and link it. |
| `retry_association` | none | Retry the Spoolman link after a completed write. |
| `clear_preview` | none | Prepare a clear. |
| `clear` | exactly `uid`, `generation`, `current_checksum`, `target_checksum` (strings) | Clear the tag and unlink it. |
| `retry_unlink` | none | Retry the Spoolman unlink after a completed clear. |
| `discard_recovery` | exactly `confirm: "SKIP RECOVERY"` | Abandon an interrupted operation. |
| `update_spool` | `spool_id` (> 0), `expected`, `changes` (non-empty) | Edit a spool in Spoolman. |
| `update_filament` | `filament_id` (> 0), `expected`, `changes` (non-empty), optional `spool_id` (> 0) | Edit a filament in Spoolman. |

There is no route that writes raw tag memory. The values for `write` and `clear` come from the
preceding preview in `GET /tag-writer`. Engine details:
[tag-writer.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/tag-writer.md).

### Printer and toolheads

| Method and path | Auth | Body | Notes |
|---|---|---|---|
| `GET /printers` | – | – | `revision`, `printers[]` |
| `GET /toolheads` | – | – | `revision`, `toolheads[]` (`printer_id`, `printer_state`, `backend_id`, `display_number`, `display_name`, `assigned_spool_id`, `profile_enabled`, `profile_name`) |
| `POST /toolheads/{id}/assign` | Token | ≤ 2048 | `id` is the 0-based toolhead, 0–4. |
| `POST /toolheads/{id}/unassign` | Token | ≤ 2048 | Same `id` rule. |

Assign needs all of these fields and no others. Unassign takes the same set without
`expected_spool_id` and `replace_occupied_confirmed`.

| Field | Rule |
|---|---|
| `printer_id` | 1–128 bytes, from `GET /printers` |
| `expected_spool_id` | > 0; the spool to assign |
| `expected_current_spool_id` | Spool now on the toolhead, or `null` if empty. Unassign: must be > 0. |
| `expected_printer_state` | `unknown`, `idle`, `printing`, `paused`, `attention`, `finished`, `stopped`, `error`, `offline` or `not_configured` |
| `spool_generation` | From `GET /spool` |
| `printer_revision` | From `GET /printers` |
| `replace_occupied_confirmed` | `true` to replace a spool already on the toolhead |
| `advanced_override` | `true` to change a toolhead while the printer is printing, paused, needs attention, or its state is unknown |

Behaviour: [FilaBridge integration](filabridge.md).

### Configuration

| Method and path | Auth | Body | Notes |
|---|---|---|---|
| `GET /config` | – | – | Settings without secrets, plus `revision`. |
| `PATCH /config` | Token | ≤ 16384 | `expected_revision` plus at least one section. |

```json
{"expected_revision": 12,
 "spoolman": {"url": "http://192.168.1.20:7912"},
 "reconciliation": {"auto_update_after_weigh": true}}
```

Sections: `device`, `wifi`, `web`, `spoolman`, `filabridge`, `scale_profile`, `toolheads`,
`reconciliation`. An object section that is present must not be empty. `toolheads` is a list; an
empty list removes every profile. An omitted field keeps its value.
A stale `expected_revision` returns `409 state_conflict`. Keys, defaults, bounds and the redaction
rules: [Configuration keys](configuration.md).

### Firmware update

| Method and path | Auth | Body | Notes |
|---|---|---|---|
| `GET /update` | – | – | `state`, `generation`, `revision`, `operation_id`, `upload_operation_id`, `expected_sha256`, `calculated_sha256`, `image_size`, `received_bytes`, `progress`, `partitions`, `current`, `candidate`, `capabilities`, optional `last_error` |
| `POST /update/upload` | Token | raw image, ≤ 5 MiB | Headers below. Not JSON. |
| `POST /update/reboot` | Token | ≤ 512 | Restart into the uploaded image. |
| `POST /update/cancel` | Token | ≤ 512 | Discard an upload in progress or a validated image that has not been started. |

Upload headers:

| Header | Value |
|---|---|
| `Content-Type` | `application/octet-stream` |
| `Content-Length` | 1 to 5,242,880 |
| `X-OpenTag-Request` | `web` |
| `Idempotency-Key` | as for other change requests |
| `X-OpenTag-Image-SHA256` | SHA-256 of the file, 64 lowercase hex characters |
| `X-OpenTag-Expected-Generation` | `generation` from `GET /update`, decimal without leading zeros |

The upload answers when the image has been written and checked: `200` with `operation_id`,
`upload_operation_id`, `kind: "firmware_upload"`, `state: "succeeded"`, `generation`,
`update_state`, `validated`, `activated`. Repeating the same upload with the same key returns
`202` with `state: "duplicate"`. The upload fails if no data arrives for 5 seconds or the whole
transfer exceeds 180 seconds (`408 upload_timeout`).

Reboot and cancel take exactly these four fields and act only on the upload they name:

```json
{"upload_operation_id": 21, "expected_generation": 4,
 "expected_sha256": "<64 lowercase hex>", "confirmation": "REBOOT INTO UPDATE"}
```

For cancel the confirmation is `CANCEL UPDATE`. A mismatch with the current upload returns
`409 state_conflict`. States and rollback: [Firmware update internals](ota.md).

### Device control

| Method and path | Auth | Body |
|---|---|---|
| `POST /device/reboot` | Token | `{"confirmation": "REBOOT"}` (≤ 256) |
| `POST /device/factory-reset` | Token | `{"confirmation": "FACTORY RESET"}` (≤ 256) |

Factory reset deletes the saved settings, Wi-Fi credentials and scale calibration. It does not
change the firmware. Reboot, factory reset and update reboot are refused with `409` while a tag
or Spoolman operation is queued or running.

### Static pages

`GET /`, `/assets/app.css`, `/assets/app.js` and `/assets/writer.js` serve the browser page. Any
other path returns `404 route_not_found`, except for a setup-network client, which is redirected
(`302`) to `http://192.168.4.1/`.

## Live events

`ws://<station>/api/v1/events` is a WebSocket that only sends. No token is needed. Any data frame
from the client closes the connection. At most two clients are served at once. Messages are JSON,
at most 4096 bytes:

| `type` | `data` | When |
|---|---|---|
| `scale` | Same object as `scale` in `GET /scale` | During a measurement, at most every 500 ms, and once when it ends |
| `update` | Same object as `GET /update` | When the update state changes, at most every 500 ms |
| `invalidate` | `{"resource": "backends" \| "nfc" \| "scale" \| "update"}` | Fetch that resource again |
| `heartbeat` | `{"uptime_ms": n}` | Every 15 seconds |

## Status and error codes

| HTTP | `error.code` |
|---:|---|
| 400 | `invalid_request`, `invalid_request_headers`, `unexpected_body`, `incomplete_request_body`, `invalid_operation_id`, `invalid_firmware_length`, `invalid_upload_headers`, `invalid_upload_precondition`, `incomplete_firmware_upload` |
| 401 | `authentication_required` |
| 404 | `route_not_found`, `unsupported_api_version`, `operation_not_found` |
| 405 | `method_not_allowed` (with an `Allow` header) |
| 408 | `request_timeout`, `upload_timeout` |
| 409 | `state_conflict`, `tag_state_conflict`, `scale_unstable` |
| 413 | `request_too_large`, `firmware_too_large` |
| 415 | `unsupported_media_type` (upload only) |
| 422 | `validation_failed`, `invalid_tag`, `firmware_validation_failed` |
| 500 | `internal_error`, `internal_route_error`, `invalid_snapshot`, `unsafe_configuration_snapshot`, `response_too_large`, `streaming_transport_required` |
| 502 | `backend_authentication_failed`, `backend_api_changed`, `invalid_backend_response` |
| 503 | `network_unavailable`, `backend_unavailable`, `nfc_unavailable`, `scale_unavailable`, `update_unavailable`, `operation_not_queued`, `resource_unavailable` |
| 507 | `persistence_failed` |

A `503 resource_unavailable` with `retryable: true` means the station was briefly out of working
memory; repeat the request with the same `Idempotency-Key`.

## Limits

| Item | Limit |
|---|---|
| Request body | 16,384 bytes, or the lower limit given per route |
| Response body | 32,768 bytes |
| Path | 256 bytes |
| Request headers | 16 headers, 1,024 bytes in total, 512 bytes per value |
| Time to receive a request body | 5 seconds (`408 request_timeout`) |
| Open HTTP connections | 5, of which at most 2 WebSocket clients |
| Queued assign, unassign, spool confirmation, tag write or tag clear | Fails if it waits more than 15 seconds before it starts |

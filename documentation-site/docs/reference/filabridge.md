# FilaBridge integration

What the station reads from and sends to FilaBridge when it assigns a spool to a toolhead.
Requests go to `<filabridge.url>` directly (no path prefix).

## Accepted versions

The version is the `version` string in `GET /healthz`, compared exactly.

| Version string | Read printers and toolheads | Assign / unassign |
|---|---|---|
| `v1.2.2`, `1.2.2`, `v1.2.1`, `1.2.1` | yes | yes |
| `dev` | yes | yes (reported as an untested version) |
| Anything else, or no version | yes | no |

If the version seen by a routine health check differs from the last full check, assign and
unassign are switched off and the status shows "FilaBridge version changed; use Test backends to
verify capabilities". Run the integration test (`POST /api/v1/backends/test`, or **Test** under
Settings → Integrations in the browser) to check again.

See also [Supported versions](upstream.md).

## Endpoints used

| Method and path | Purpose | Expected |
|---|---|---|
| `GET /healthz` | Online check and version | `{"status": "ok", "version": "…"}` |
| `GET /api/printers` | Printer IDs, names, toolhead counts and names | `printers` object keyed by printer ID |
| `GET /api/status` | Printer states and current toolhead-to-spool mappings | `printers` and `toolhead_mappings` objects |
| `POST /api/map_toolhead` | Assign or unassign | Any 2xx status |

Assign body, where `spool_id` `0` unassigns:

```json
{"printer_name": "<printer display name>", "toolhead_id": 0, "spool_id": 42}
```

The station accepts at most 16 printers with 1–10 toolheads each. The mapping list for a printer
must contain every toolhead; an incomplete or malformed answer is rejected as a whole.

## Toolhead numbering

FilaBridge numbers toolheads from 0. The station shows them from 1.

| FilaBridge `toolhead_id` | Shown as | Station API `{id}` |
|---:|---|---:|
| 0 | T1 | 0 |
| 1 | T2 | 1 |
| 4 | T5 | 4 |

A toolhead name from FilaBridge replaces the `T<n>` label. The station API accepts toolhead IDs
0–4.

## Assign, replace and unassign

Before sending anything the station reads printers and status again and checks, in this order:

| Check | Outcome when it fails |
|---|---|
| Printer and toolhead still exist | Failed: "Selected printer no longer exists in FilaBridge" / "Selected toolhead no longer exists in FilaBridge" |
| The toolhead's current spool and the printer state equal what the request expected | Failed: "Toolhead assignment precondition is stale; refresh before confirming" / "Toolhead unassignment precondition is stale; refresh before confirming" |
| The toolhead already holds the requested spool (assign) or is already empty (unassign) | Succeeds without sending anything |
| Printer is `printing`, `paused` or `attention` | Needs `advanced_override` |
| Printer state is `unknown`, `offline` or `not_configured` | Needs `advanced_override` |
| Toolhead holds a different spool (assign only) | Needs `replace_occupied_confirmed` |

A request that lacks a needed confirmation changes nothing; its operation ends in state
`confirmation_required` with the message "Replacement confirmation required" or "Advanced
printer-state override required". Send it again with the flag set.

After `POST /api/map_toolhead` the station reads the mapping back:

| Result | Outcome |
|---|---|
| Mapping shows the requested spool (or no spool after unassign) | Succeeded: "Assignment verified by readback" / "Unassignment verified by readback" |
| Mapping shows something else | Failed: "FilaBridge assignment verification failed: mapped spool does not match" / "FilaBridge unassignment verification failed: toolhead is still mapped" |
| The read failed | Failed: "Sent to FilaBridge but could not be verified (…). Refresh printers before retrying." The change may have been applied. |

Replacing a spool is one `map_toolhead` call with the new spool ID; the station does not unassign
first.

Request fields: [REST API](api.md). Touchscreen and browser steps:
[Assign a spool to a toolhead](../daily-use/assign.md).

## Connection

| Item | Value |
|---|---|
| Connect timeout | 5 seconds |
| Read timeout | 7 seconds |
| Automatic retry | None |
| Queue expiry | An assign or unassign that waits more than 15 seconds before it starts fails with "Assignment expired in the backend queue; refresh and retry" (or "Unassignment expired…"). |
| Authentication | `filabridge.authentication_token` is sent as `Authorization: Bearer <token>`. A value that starts with `Basic ` or `Bearer ` is sent unchanged. |
| HTTPS | An `https://` URL works only when a CA certificate is stored in `filabridge.ca_certificate_pem` ([Configuration keys](configuration.md)). |

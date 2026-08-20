# FilaBridge adapter strategy

## Baseline

The correct integration target is the maintained
[`sargonas/filabridge`](https://github.com/sargonas/filabridge) repository. The
baseline is main commit
[`f35cde8`](https://github.com/sargonas/filabridge/commit/f35cde87505e7a617307527b8e8431dd2dc65f62)
from 2026-08-11. The latest tagged release found during research is v1.2.2; main
contains subsequent changes. Runtime version is available from `GET /healthz`.

Initial adapter dependencies:

| Operation | Current endpoint/shape |
|---|---|
| Health/version | `GET /healthz` → `status`, `version` |
| Printers | `GET /api/printers` → map keyed by printer ID |
| Live state/mappings | `GET /api/status` |
| Map/unmap | `POST /api/map_toolhead` |
| Live updates | `WS /ws/status` |

`GET /api/status` returns `printers`, `toolhead_mappings`, and a timestamp.
Printer status and mapping maps are keyed by the configured printer ID. Each
mapping includes `printer_name`, zero-based `toolhead_id`, `spool_id` (`0` means
empty), optional `mapped_at`, and `display_name`. The maintained implementation
returns entries for unmapped toolheads as well as mapped ones.

The current command body is:

```json
{
  "printer_name": "Prusa XL",
  "toolhead_id": 0,
  "spool_id": 147
}
```

Passing `spool_id: 0` unmaps. Current validation rejects unknown printers and
out-of-range/negative toolhead IDs and returns conflict when one spool is mapped
elsewhere.

## Normalization

The adapter joins `/api/printers` and `/api/status` so the domain stores all of:

```text
printer_id = stable FilaBridge configuration-map key
backend_id = zero-based toolhead_id
display_number = backend_id + 1, computed once in the adapter
display_name = T1..T5 for this product UI (backend custom name retained separately)
```

Application and UI code never perform ad-hoc `+1` conversions.

## Verified assignment workflow

1. Read `/api/printers` and `/api/status`; confirm the target printer/toolhead.
2. Check live print state. Warn or block normal remapping while printing.
3. If occupied, present the exact displaced spool and require replacement
   confirmation.
4. POST the mapping command with a request deadline.
5. Treat the HTTP result only as command acceptance.
6. Perform one fresh bounded re-read of `/api/printers` and `/api/status`.
7. Locate the response by printer ID and backend toolhead ID.
8. Confirm its `spool_id` exactly equals the requested Spoolman ID.
9. Publish `assignment_confirmed`; otherwise return a structured mismatch or
   availability error.

There is no automatic retry of a destructive command and no offline assignment
is queued for future replay. FilaBridge remains
responsible for displaced-spool location rules and all print-consumption logic.

The production adapter and transaction policy are implemented. The touchscreen
shows the configured stable printer's T1–T5 state and sends commands to a
bounded backend worker. Occupied mappings require `Replace`; active printing
and unverified/offline printer states require a clearly labeled advanced action.
Disabled local toolhead profiles cannot be assigned. The UI remains read-only
when the selected printer, resolved spool, or guarded FilaBridge mapping
capability is unavailable.

## Security and compatibility notes

FilaBridge documents no built-in authentication and is intended for a private
network. The adapter supports a configured Basic/Bearer header for an
authenticated reverse proxy without exposing it in state or diagnostics.
Unknown versions remain connected/read-capable when concrete probes succeed,
but write capabilities stay guarded. Any changed path/shape is isolated to this
adapter and returned as a structured error, never as raw credential-bearing
request data.

Formal release support currently covers FilaBridge v1.2.2. Current main commit
`f35cde8` was separately source-inspected; its runtime reports `dev`, so it is a
tracking fixture rather than a formal version declaration. A live v1.2.2
instance and real Prusa XL still must pass mapping, reassignment, active-print,
and unmapping acceptance tests before release signoff.

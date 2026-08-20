# Spoolman adapter strategy

## Baseline

The current source baseline is
[Spoolman `master` at `8d9eb73`](https://github.com/Donkie/Spoolman/commit/8d9eb7395da9553bdbf14b21231afe4e153f0a79),
inspected 2026-08-20. The source and latest release still identify as v0.26.1.
Its REST API remains under `/api/v1`; runtime information is exposed by
`GET /api/v1/info` and health by `GET /api/v1/health`.

Initial adapter dependencies:

| Operation | Current endpoint |
|---|---|
| Runtime/version | `GET /api/v1/info` |
| Health | `GET /api/v1/health` |
| List/filter spools | `GET /api/v1/spool` |
| Retrieve spool | `GET /api/v1/spool/{id}` |
| Update explicit remaining weight | `PATCH /api/v1/spool/{id}` |
| Apply gross scale measurement | `PUT /api/v1/spool/{id}/measure` |
| Record incremental use | `PUT /api/v1/spool/{id}/use` |
| List locations | `GET /api/v1/location` |
| List/create field definitions | `GET/POST /api/v1/field/{entity_type}[/{key}]` |
| Observe spool changes | WebSocket on `/api/v1/spool` or `/api/v1/spool/{id}` |

These paths are implementation details of `SpoolmanAdapter`, not part of the
device's local API.

## Weight semantics

Spoolman exposes `initial_weight`, `spool_weight`, `used_weight`, derived
`remaining_weight`, and `remaining_length`. The `/measure` operation expects
**gross physical spool weight**. An explicit reconciliation may instead patch
`remaining_weight`. The adapter will select only a capability proven by a
runtime probe, perform one intentional write after stable measurement/user
policy, then retrieve the spool and verify the returned value within rounding
tolerance.

The device does not race FilaBridge's print-consumption updates. Before a write,
it re-fetches the spool and detects whether `used_weight` changed since the
comparison screen was generated. A concurrent change invalidates the proposed
write and requires a new reconciliation.

## Identity and extra fields

Current list filtering accepts spool custom fields as `extra.<key>`. Extra-field
values in entity responses are JSON-encoded strings. The user configures which
field keys represent OpenPrintTag instance UUID and NFC UID.

A critical current contract changed after the earlier Phase 0 inspection:
Spoolman's database implementation now merges only the keys present in an
`extra` patch, and a key sent as `null` is removed. The adapter therefore sends
only the one intended key and verifies it with a fresh spool read. It never
reconstructs or replaces unrelated values, and never silently creates field
definitions; field creation remains an explicit user action.

Identity resolution is ordered and terminal at each exact match: configured
OpenPrintTag instance-UUID field, confirmed local mapping, configured NFC UID
field/local mapping, GTIN/package/material identifiers, and finally strict
vendor/material metadata. More than one exact or plausible result is returned
as a conflict or manual-selection list; no first-result fallback exists.
Confirmed instance UUID and NFC UID links persist in configuration schema 3.
The store rejects duplicate identities that point at different spool IDs.

## Parsing and capability behavior

Response bodies have strict byte/depth limits. Required fields are type-checked;
optional and unknown fields are ignored. An unfamiliar runtime version remains
connected and retains every successfully probed read capability. Mutating
operations are disabled until their contract is formally supported because a
safe probe cannot exercise them without changing inventory. Unexpected shapes
become structured `api_changed` diagnostics scoped to the operation.

Basic or Bearer authentication, if configured, is transport policy. Credentials are
stored/redacted by the settings service and never included in diagnostic bodies
or logs. TLS verification is not globally disabled.

The native contract fixtures cover health/version discovery, unknown-version
read-only degradation, filtered list/get parsing, explicit creation, location
and custom-field discovery, concurrent-use rejection, one-shot remaining-weight
patches, read-after-write mismatch rejection, and one-key extra-field updates.
A live/containerized Spoolman instance is still required for integration signoff.

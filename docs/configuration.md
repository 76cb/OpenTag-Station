# Configuration and migrations

## Phase 11 release status

Fresh/current/legacy, malformed/truncated, missing/unknown field, secret
preservation, CAS, profile, backup, and interrupted-write behavior are part of
the release audit. The durable ordering and power-cut expectations are recorded
in [release-validation.md](release-validation.md).

## Storage boundary

Application code uses `ConfigurationService`; it does not address arbitrary
NVS keys. The persisted `/configuration.json` document has a schema version and
logical categories:

- device identity and hardware revision;
- Wi-Fi credentials and reconnect settings;
- Spoolman URL/auth and configurable identity field keys;
- confirmed OpenPrintTag instance UUID/NFC UID to Spoolman ID mappings;
- FilaBridge URL/auth and selected printer ID;
- the local web API access credential;
- scale calibration and the independently persisted load-cell profile;
- toolhead enhancement profiles;
- display/user preferences;
- update channel/provider policy;
- first-run setup progress.

Each local toolhead profile stores backend ID, display name, nozzle diameter,
enabled state, nozzle material, maximum temperature, and operator notes.

Default service export removes the Wi-Fi SSID/password, backend authentication
tokens, custom CA certificates, and local web access token, replacing them with
configured-state flags. Credential-inclusive service export is a separate,
explicit operation; it is not the response used by the local HTTP API.
Noncredential import preserves the station's existing credentials rather than
clearing or accepting secrets from the input document.

## Schema baseline

Schema 3 is the current write format. Schema 1 is migrated to schema 2, then
schema 2 is migrated additively to schema 3 with the NFC UID field key and an
empty confirmed-identity mapping list. Unknown top-level and nested object
fields survive subsequent known-field edits and exports. Newer schemas, a
different hardware ID, invalid types/ranges, malformed JSON, and documents
above 16 KiB are rejected before the live configuration changes.

Phase 9 adds two optional, backward-compatible schema-3 objects:

- `web.access_token` is empty or a 16–128 byte ASCII token containing only
  letters, digits, `-`, `.`, `_`, and `~`. An absent `web` object behaves
  as an empty token, so older schema-3 documents continue to load.
- `scale_profile` contains `load_cell_model`,
  `rated_capacity_grams`, and `overload_ratio`. New writes always persist the
  profile independently of calibration. A schema-3 document with a calibration
  but no profile infers the rated capacity from that calibration; one with
  neither uses the YZC-133 5 kg default.

On the first central-configuration boot, the Phase 5 CRC-protected schema-1 NVS scale record
is imported into the document. New scale calibrations update the central
document and mirror the legacy record so an older firmware rollback retains a
usable calibration.

## Scale profile and calibration

The hardware profile describes the attached load cell; the optional `scale`
object describes a calibration measured for that profile. A calibration is
valid only when its capacity matches `scale_profile.rated_capacity_grams`.
Changing the profile model or rated capacity through the API clears the old
calibration atomically because its factor can no longer be trusted. Changing
only the overload ratio preserves calibration. The browser API currently
offers the tested `yzc-133-2kg` and `yzc-133-5kg` profiles, model
`YZC-133`, and an overload ratio greater than 1.0 and no greater than 2.0.

## Migration rules

Each migration is a host-tested `N -> N+1` transform. It is idempotent at its
declared target, preserves unrelated/unknown values, and commits through
write-new/readback-verify/backup/rename. If the primary document is unreadable
or invalid, initialization validates `/configuration.bak`, restores it through
the same transaction, and reports that backup recovery occurred. A failed
write never replaces the live in-memory settings.

An OTA candidate must not destructively migrate the only readable copy before
firmware validation. If a change cannot remain backward-readable, the updater
creates a pre-migration backup and records minimum/maximum schema compatibility
in the manifest. Rollback selects the matching configuration snapshot.

Confirmed identity mappings are bounded to 64 entries. A mapping requires a
positive Spoolman ID and an instance UUID, a 16-hex-character NFC UID, or both.
UUID comparison is case-insensitive, NFC UIDs are normalized uppercase, and an
identity already linked to another spool is rejected transactionally.

## Backup/import

Export produces versioned JSON with hardware/product metadata and excludes
credentials by default. Including credentials requires an explicit option and a
fresh local confirmation. Import validates size, schema range, value types, and
hardware compatibility before staging; it never partially applies a document.

## Runtime revision and ownership

Initialization and migration occur during boot. Interactive UI saves use the
bounded configuration queue; its worker serializes flash writes and requests a
Wi-Fi reconfiguration only when hostname or Wi-Fi policy changes. The LVGL task
never writes flash. Scale calibration uses the same service from the scale
owner.

The service exposes one coherent configuration snapshot with a runtime
`revision`. A successful initialization establishes the first revision, and
each successful persisted mutation advances it exactly once. Validation errors,
storage failures, and stale writes do not advance it. The revision is a
boot-local compare-and-swap token, not a persisted sequence number.

Touchscreen and HTTP edits capture the configuration and revision together,
then submit both to the configuration worker. `PATCH /api/v1/config` requires
`expected_revision`; the request-side merge and the worker-side flash commit
both check it. A stale request is rejected as a conflict without partially
applying its proposal. Clients reload `GET /api/v1/config` and retry from the
new revision rather than overwriting a concurrent change.

## Local web configuration boundary

`GET /api/v1/config` is built from an explicit allowlist, not from the raw
persisted document. It returns the runtime revision, schema/hardware identity,
nonsecret device and integration settings, scale profile and calibration state,
toolhead profiles, and reconciliation tolerances. It never returns credential
values. The response exposes only:

- `web.access_token_configured`;
- `wifi.password_configured`;
- `spoolman.authentication_token_configured` and
  `spoolman.custom_ca_configured`;
- `filabridge.authentication_token_configured` and
  `filabridge.custom_ca_configured`.

Configuration PATCH requests are authenticated mutations and require
`Authorization: Bearer ...`, `Content-Type: application/json`,
`X-OpenTag-Request: web`, a bounded `Idempotency-Key`, and one JSON object.
The body is limited to 16 KiB and nesting depth 8. Unknown keys, ambiguous
aliases, empty section objects, mismatched JSON types, non-finite
numbers, unsafe control characters, and a patch with no changed section are
rejected before dispatch.

Omission and empty strings have deliberately different meanings. An omitted
field preserves its current value. For Wi-Fi password, backend authentication
tokens/CA certificates, and `web.access_token`, an explicitly supplied empty
string clears only that credential; a valid nonempty string replaces it.
Including `toolheads` replaces the complete bounded profile list.

The main PATCH bounds are:

| Section | Accepted values |
| --- | --- |
| `device` | hostname 1–63 lowercase letters/digits/hyphens with no edge hyphen; brightness 5–100%; dim/sleep 1–86,400,000 ms with sleep not before dim; channel `stable`, `beta`, or `development` |
| `wifi` | SSID at most 32 bytes; password at most 64; Boolean reconnect policy; connect timeout 1,000–60,000 ms; initial backoff 500–60,000 ms; maximum backoff 500–600,000 ms and not below the initial value |
| `spoolman` / `filabridge` | HTTP(S) URL at most 256 bytes; authentication token at most 512; CA certificate at most 4,096; identity keys at most 64; selected printer ID at most 128 |
| `web` | access token empty, or 16–128 ASCII characters from the credential alphabet above |
| `scale_profile` | `yzc-133-2kg`/2,000 g or `yzc-133-5kg`/5,000 g, model `YZC-133`, overload ratio 1.01–2.0 |
| `toolheads` | at most 8 complete profiles; unique backend IDs 0–31; names/materials 1–32 bytes; nozzle 0.1–2.0 mm; maximum temperature 100–500 °C; notes at most 256 bytes |
| `reconciliation` | finite normal/warning tolerances 0–1,000 g, with warning not below normal |

## First-run behavior

The on-device sequence is Welcome, Wi-Fi, Spoolman, FilaBridge, printer
selection, scale calibration, NFC status, and Ready. All steps remain navigable
when incomplete. Backend reachability is informational and does not gate the
ability to reach diagnostics or revisit setup.

Physical power-loss recovery, flash wear, and backup/restore still require
verification on the target board.

The Ready step contains a masked local API token field and saves through the
same revision-checked configuration worker. With no token configured, every
browser mutation fails closed, so initial provisioning and recovery after a
clear require physical touchscreen access. Once a token exists, an
authenticated administrator may rotate it or explicitly clear it through the
configuration PATCH. The token itself is never returned to the browser.

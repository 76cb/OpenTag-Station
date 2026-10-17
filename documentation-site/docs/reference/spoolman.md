# Spoolman integration

What the station reads from and writes to Spoolman, and the rules it applies. Spoolman is the
source of truth for spools, filaments and weights; the station stores only a small cache of
confirmed tag-to-spool links.

All requests go to `<spoolman.url>/api/v1`.

## Version gate

The station reads `version` from `GET /info` and compares it with the tested version, `0.26.1`,
as an exact string. See also [Supported versions](upstream.md).

| Spoolman version | Effect |
|---|---|
| Exactly `0.26.1` | Everything is available. |
| Anything else, including other patch releases | Saving a weight is refused: "Saving weights is turned off: this Spoolman version has not been tested with this firmware, or Spoolman is offline. See Settings." Reading, searching, creating and editing records, and writing, linking and clearing tags still work. |
| `/info` unreachable right after a successful health check | The last version read from this server is kept. |

`GET /health` must answer `{"status": "healthy"}` for Spoolman to count as online.

## Extra fields

Two extra fields must exist on the **Spool** entity with type **Text**.

| Setting | Default key | Holds |
|---|---|---|
| `spoolman.identity_field` | `opentag_instance_uuid` | Instance UUID stored on the tag |
| `spoolman.nfc_uid_field` | `nfc_uid` | The tag's hardware UID |

- Only `GET /field/spool` is read. A field created under Filament or Vendor is not seen.
- The station never creates the fields.
- If one is missing or not of type Text, lookups by tag and tag writes fail with: "Spoolman is
  missing the Spool extra field '<key>'. In Spoolman open Settings > Extra Fields > Spool and add
  it with type Text."
- Values are written as JSON-encoded strings, as Spoolman expects for Text fields.

Setup steps: [Required extra fields](../inventory/custom-fields.md).

## Finding the spool for a tag

The station tries these in order and stops at the first that gives an answer:

1. Spoolman spools whose `identity_field` equals the tag's instance UUID.
2. The station's cache of confirmed links, by instance UUID.
3. Spoolman spools whose `nfc_uid_field` equals the tag's UID.
4. The cache of confirmed links, by UID.
5. Spools matching the tag's vendor and material (up to 128 unarchived spools are compared).
   Product identifiers on the tag (GTIN, package or material IDs) are preferred over plain
   vendor/material agreement.

Rules:

- Steps 1 and 3 include archived spools and ask for at most 3 results. More than one result is a
  conflict, not a match.
- A spool found by UID whose stored instance UUID differs from the tag's is a conflict.
- A cached link is a conflict if Spoolman now shows that spool with a different UUID or UID.
- Step 5 never selects a spool by itself. Its results are suggestions that must be confirmed on
  the station, even when there is only one.
- A tag with no vendor, material or product identifier skips step 5.

## Catalog search and paging

Used by the spool, filament and vendor lists on the touchscreen and browser.

| Rule | Value |
|---|---|
| Page size | 8 rows, sorted by ID ascending (`limit=8&offset=N&sort=id:asc`) |
| More pages | Offered when a page returns 8 rows |
| Search text | ≤ 64 bytes. Sent as `name=` for filaments and vendors, `filament.name=` for spools. Spoolman matches case-insensitive substrings. |
| Vendor fallback | If the first page of a filament or spool name search is empty, the search is repeated once against the vendor name (`vendor.name=` / `filament.vendor.name=`). |
| Other filters | `material` (filaments and spools), `article_number` (filaments only), vendor ID, filament ID |
| Not searched | Color, comment, location, lot number, ID |
| Archived spools | Not requested: the catalog query does not send `allow_archived`, so which spools come back is up to Spoolman. (The uniqueness checks before a write send `allow_archived=true`.) |
| Oversized page | A page larger than the station can hold fails with "This page is too large to show; narrow the search." |

## What the station writes

| Action | Request | Checks |
|---|---|---|
| Save a weight | `PATCH /spool/{id}` with `{"remaining_weight": x}` | See below |
| Link a tag | `PATCH /spool/{id}` setting both extra fields | The spool is read before and after; the stored values must match, and no other spool may hold the same UUID. |
| Move a tag to another spool | `PATCH` on the previous spool setting the UID field to `null` | Only if that spool still holds this tag's UID |
| Unlink after clearing a tag | `PATCH /spool/{id}` setting both extra fields to `null` | Refused if the spool's stored UUID changed meanwhile |
| Create a spool | `POST /spool` with `filament_id` and optional `initial_weight`, `remaining_weight` or `used_weight`, `spool_weight`, `price`, `location`, `lot_nr`, `comment` | Read back by ID |
| Edit a spool or filament | `PATCH /spool/{id}` or `PATCH /filament/{id}` with the changed fields | The record must still match what was shown; read back after |

The station does not call Spoolman's `use` or `measure` endpoints and does not delete records.

### Saving a weight

Measured filament = scale reading − empty-spool weight. The source of the empty-spool weight is
described in [Scale behaviour](scale.md).

A measurement cannot be saved when: the reading did not settle; the tag is not linked to a spool;
the empty-spool weight is unknown; Spoolman has no remaining weight for the spool; or the reading
is below the empty-spool weight. The station shows the reason.

When a save is requested (or `reconciliation.auto_update_after_weigh` is on):

1. If the measured filament is within `reconciliation.normal_tolerance_grams` (default 5 g) of
   Spoolman's remaining weight, nothing is written: "Spoolman already matches this weight.
   Nothing to update."
2. The spool is read again. If its used weight moved by more than 0.05 g since the measurement,
   the save is refused and nothing is overwritten.
3. A spool without an initial weight is refused: "This spool has no initial weight in Spoolman.
   Set its initial weight in Spoolman, then weigh again."
4. Up to 10 g above the initial weight is saved as the initial weight (a full spool). More than
   that is refused with a message naming both weights.
5. `remaining_weight` is sent. The spool is read once more and must show the new value within
   0.25 g.

An HTTP 400 from Spoolman on step 5 is reported as "Spoolman refused the new weight (HTTP 400).
Check that the spool has an initial weight in Spoolman."

## Endpoints used

| Method and path | Purpose |
|---|---|
| `GET /health`, `GET /info` | Online check and version |
| `GET /field/spool` | Extra-field definitions |
| `GET /location` | Location list |
| `GET /spool`, `GET /spool/{id}` | Lookups, catalog pages, checks before and after a write |
| `GET /filament`, `GET /filament/{id}` | Catalog pages, checks |
| `GET /vendor` | Catalog pages |
| `POST /spool`, `PATCH /spool/{id}` | Create, weight, extra fields, edits |
| `PATCH /filament/{id}` | Edits |

## Connection

| Item | Value |
|---|---|
| Connect timeout | 5 seconds |
| Read timeout | 7 seconds |
| Automatic retry | None. A failed action reports its error. |
| Largest response | 64 KiB for spool lookups; 24 KiB for catalog pages and edits |
| Authentication | `spoolman.authentication_token` is sent as `Authorization: Bearer <token>`. A value that starts with `Basic ` or `Bearer ` is sent unchanged. |
| HTTPS | An `https://` URL works only when a CA certificate is stored in `spoolman.ca_certificate_pem` ([Configuration keys](configuration.md)); without one the request fails with "HTTPS requires a configured CA certificate". |
| Unexpected response format | The action fails with a message; nothing is written. |

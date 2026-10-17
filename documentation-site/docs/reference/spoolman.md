# Spoolman integration

What the station reads from and writes to Spoolman, and the rules it applies. Spoolman is the
source of truth for spools, filaments and weights; the station stores only a small cache of
confirmed tag-to-spool links.

All requests go to `<spoolman.url>/api/v1`.

## Version gate

The station reads `version` from `GET /info` and parses it as `MAJOR.MINOR.PATCH` (digits only,
optional leading `v`, nothing after the patch number). Two independent rules apply. See also
[Supported versions](upstream.md).

| Rule | Condition | Effect |
|---|---|---|
| Weight saving | Major `0` and minor `26` or `27` | Saving a weight is allowed. Otherwise (including any string that does not parse, such as `0.27.0-dev` or `unknown`) it is refused: "Saving weights is turned off: this Spoolman version has not been tested with this firmware, or Spoolman is offline. See Settings." Reading, searching, creating and editing records, and writing, linking and clearing tags still work. |
| Native tags (capability `native_tags`, bit 20 of `capabilities_bits` in `GET /status`) | Parses and is `0.27.0` or later (any major ≥ 1 too) | The tag list is used as described in [Native tag list](#native-tag-list-spoolman-027). Detected from the `GET /info` version only. |

`/info` unreachable right after a successful health check: the last version read from this server
is kept. While the version is still unknown (after start-up, or after Spoolman was offline), the
identity lookup, the tag writer and weight saving ask `GET /info` once before they act, and the
first successful status check after an outage is a full one.

`GET /health` must answer `{"status": "healthy"}` for Spoolman to count as online.

## Extra fields

Two extra fields on the **Spool** entity with type **Text**. Required when `native_tags` is
absent (Spoolman < 0.27); optional with `native_tags`.

| Setting | Default key | Holds |
|---|---|---|
| `spoolman.identity_field` | `opentag_instance_uuid` | Instance UUID stored on the tag |
| `spoolman.nfc_uid_field` | `nfc_uid` | The tag's hardware UID |

- Only `GET /field/spool` is read. A field created under Filament or Vendor is not seen.
- The station never creates the fields.
- Without `native_tags`: if one is missing or not of type Text, lookups by tag and tag writes
  fail with: "Spoolman is missing the Spool extra field '<key>'. In Spoolman open Settings > Extra
  Fields > Spool and add it with type Text." The status reports `identity_fields_ready: false`
  with that message (`backends.spoolman` in `GET /status`), and the browser shows it under **Integrations**.
- With `native_tags`: `identity_fields_ready` is `true` whether or not the fields exist. Missing
  fields mean the station sends no `extra.<key>` queries or patches; an extra-field lookup step
  counts as "no match". When both fields exist they are still filled and emptied exactly as on
  older Spoolman.
- Without the fields, the instance UUID exists only on the tag and in the station's cache.
- Values are written as JSON-encoded strings, as Spoolman expects for Text fields.

Setup steps: [Required extra fields](../inventory/custom-fields.md).

## Finding the spool for a tag

The station tries these in order and stops at the first that gives an answer
(`SpoolIdentityResolver::resolve()` in `src/services/spool_identity_resolver.cpp`):

0. With `native_tags` only: `GET /spool?allow_archived=true&tag=<UID>&limit=3&offset=0`, then the
   same with the UID's bytes reversed. A miss in both falls through to step 1.
1. Spoolman spools whose `identity_field` equals the tag's instance UUID.
2. The station's cache of confirmed links, by instance UUID.
3. Spoolman spools whose `nfc_uid_field` equals the tag's UID.
4. The cache of confirmed links, by UID.
5. Spools matching the tag's vendor and material (up to 128 unarchived spools are compared).
   Product identifiers on the tag (GTIN, package or material IDs) are preferred over plain
   vendor/material agreement.

The lookup runs once per tag placement, after the scale reports a stable weight.

Rules:

- Steps 0, 1 and 3 include archived spools and ask for at most 3 results. More than one result is
  a conflict, not a match.
- A spool found in step 0 that holds a non-empty instance UUID different from the tag's is a
  conflict (it belongs to an earlier use of the tag).
- Tags without OpenPrintTag data (a blank SLIX2 tag, or an NFC-V tag with other data) are looked
  up by UID only (steps 0, 3 and 4). Only a single exact match identifies them; a conflict,
  ambiguity or miss leaves the tag shown as blank or unsupported. Step 5 never applies to them.
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
| Link a tag (extra fields exist) | `PATCH /spool/{id}` setting both extra fields | The spool is read before and after; the stored values must match, and no other spool may hold the same UUID. |
| Move a tag to another spool (extra fields exist) | `PATCH` on the previous spool setting the UID field to `null` | Only if that spool still holds this tag's UID |
| Unlink after clearing a tag (extra fields exist) | `PATCH /spool/{id}` setting both extra fields to `null` | Refused if the spool's stored UUID changed meanwhile |
| Link, move or unlink (`native_tags`) | `POST /spool/{id}/tag`, `DELETE /spool/{id}/tag/{uid}` | See [Native tag list](#native-tag-list-spoolman-027) |
| Report a scan (`native_tags`) | `POST /tag/scan` | Best effort; see below |
| Create a spool | `POST /spool` with `filament_id` and optional `initial_weight`, `remaining_weight` or `used_weight`, `spool_weight`, `price`, `location`, `lot_nr`, `comment` | Read back by ID |
| Edit a spool or filament | `PATCH /spool/{id}` or `PATCH /filament/{id}` with the changed fields | The record must still match what was shown; read back after |

The station does not call Spoolman's `use` or `measure` endpoints and does not delete records.

## Native tag list (Spoolman 0.27+)

Used only with `native_tags`. Code: `TagWriterService::link_native()`, `unlink_native()` and
`prepare_uid_owner()` in `src/services/tag_writer_service.cpp`.

**Byte orders.** The station's canonical UID is upper-case hex starting `E004…`. Some phones and
readers report NFC-V UIDs reversed (`…04E0`). Every lookup and removal tries both forms; a new link
is always created with the canonical form.

**Who holds the tag.** `GET /spool?allow_archived=true&tag=<form>&limit=2&offset=0` for each form.
More than one spool for one form is an error ("Spoolman returned an unexpected answer for this
tag"); different spools for the two forms is refused ("Spoolman links this tag to two different
spools; remove one of the links in Spoolman first"). Filament links are checked with
`GET /filament?tag=<form>&limit=1&offset=0`.

**Preview (before any NFC write).** The previous owner shown as "Move this tag?" is taken from the
`nfc_uid` field (when the fields exist) and from the tag list. Refused, nothing written:

| Condition | Message |
|---|---|
| Field owner and tag-list owner are different spools, neither the target | "This tag is linked to spool #A by its extra field and to spool #B in Spoolman's tag list. Remove one of the links in Spoolman first." |
| A filament holds the tag | "Spoolman links this tag to filament #N. Remove it from that filament in Spoolman first." |
| Two spools hold it | "Spoolman links this tag to two different spools; remove one of the links in Spoolman first" |

**Link** (in the `associating` phase, after the tag is written and verified, and after the extra
fields when they exist):

1. Find the holders (both forms).
2. A holder other than the target must be the preview's previous spool, else "Spoolman now links
   this tag to spool #N, which was not part of this change. Check it in Spoolman, then try again."
   The previous spool's link is removed with `DELETE /spool/{previous}/tag/{form}` (404 counts as
   removed).
3. If the target holds neither form: `POST /spool/{id}/tag` with
   `{"uid": "E004…", "format": "openprinttag"}`. HTTP 409 with `filament_id` → "Spoolman links this
   tag to filament #N. Remove it from that filament in Spoolman, then try again."; otherwise →
   "Spoolman already links this tag to spool #N. Remove it from that spool in Spoolman, then try
   again." HTTP 404 → "Spool #N no longer exists in Spoolman".
4. Read back: no holder → "Spoolman did not keep the tag link; try again"; a holder other than the
   target → "Spoolman tag link readback names another spool".

**Unlink** (in the `unlinking` phase, after the tag is erased and verified, and after the extra
fields are emptied when they exist):

1. Owner check: if the `nfc_uid` owner and the tag-list owner are different spools → "This tag is
   linked to one spool by its extra field and to another in Spoolman's tag list; unlink refused".
   Without a field owner, the tag-list owner becomes the spool to unlink.
2. Every holder must be that spool, else "Spoolman now links this tag to another spool; unlink
   refused". Each is removed with `DELETE /spool/{id}/tag/{form}` (404 counts as removed).
3. Read back: any remaining holder → "Spoolman still links this tag after removing it; try again".

All of these failures after the NFC step leave the existing association-pending or unlink-pending
recovery in place; see [Write or update a tag](../daily-use/manage-tags.md) and
[Finish an interrupted tag operation](../troubleshooting/journal.md).

The station never removes a filament's tag link.

**Scan report.** After the identify command for a placement is processed, the station sends
`POST /tag/scan` with `{"uid": "<canonical UID>", "reader_id": "<device.hostname>", "name":
"OpenTag Station"}` (`SpoolmanAdapter::report_tag_scan`, called from
`src/application/backend_worker.cpp`). The response is ignored; a failure does not change the
Spoolman status. Spoolman can pair a browser with that reader id under **Settings → Tag Scanning**.

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
| `GET /spool`, `GET /spool/{id}` | Lookups (including `?tag=` with `native_tags`), catalog pages, checks before and after a write |
| `GET /filament`, `GET /filament/{id}` | Catalog pages, checks; `?tag=` with `native_tags` |
| `GET /vendor` | Catalog pages |
| `POST /spool`, `PATCH /spool/{id}` | Create, weight, extra fields, edits |
| `PATCH /filament/{id}` | Edits |
| `POST /spool/{id}/tag`, `DELETE /spool/{id}/tag/{uid}` | Native link and unlink (`native_tags`) |
| `POST /tag/scan` | Scan report (`native_tags`) |

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

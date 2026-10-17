# OpenPrintTag format

What the station reads from a tag and what it writes to one.
For which tags to buy, see [Which tags work](../openprinttag/supported-tags.md).

## Tag profile

| Property | Value |
|---|---|
| Technology | NFC-V (ISO 15693). Other NFC types are never detected. |
| Writable tag | NXP ICODE SLIX2: UID starts `E0:04`, 80 blocks × 4 bytes |
| Memory used | Bytes 0–311 (blocks 0–77), 312 bytes |
| Never written | Blocks 78–79 (bytes 312–319) and the UID |
| Blank tag | All 312 usable bytes are `0x00` |
| Protected tag | Refused if any of blocks 0–77 is locked, or the tag data declares write protection |

A tag that is neither blank nor a valid OpenPrintTag is refused for writing and for clearing.
The station does not overwrite data it cannot identify.

Messages for a refused tag:

| Message | Cause |
|---|---|
| `Incompatible tag: this release approves NXP 80 x 4-byte SLIX2 only` | Wrong chip or memory size |
| `Place exactly one stable NFC-V tag` | No tag, or more than one |
| `Tag has protected blocks` | A block in 0–77 is locked |
| `OpenPrintTag declares write protection` | The tag data marks itself read-only |
| `Unsupported or incomplete OpenPrintTag; refusing unknown data overwrite` | Other data on the tag |

## Memory layout

The tag holds an NDEF message with one MIME record of type `application/vnd.openprinttag`.
The record payload contains CBOR maps in three regions:

| Region | Content |
|---|---|
| Meta | Offsets and sizes of the other regions |
| Main | Material and package data that rarely changes |
| Auxiliary | Data that changes during use, such as consumed weight |

Fields are identified by integer keys.

Reader limits: tag image at most 4,096 bytes, each CBOR region at most 512 bytes, at most 128
map entries, nesting depth at most 12. A tag outside these limits is reported as invalid.

## Fields the station writes

A full write builds a new 312-byte image from the Spoolman spool. The mapping lives in
[`spoolman_mapping.cpp`](https://github.com/76cb/OpenTag-Station/blob/main/src/nfc/formats/openprinttag/spoolman_mapping.cpp).

| Region, key | Tag field | Source in Spoolman | Rule |
|---|---|---|---|
| Main 0 | Instance UUID (16 bytes) | The spool's `opentag_instance_uuid` extra field | Always written |
| Main 8 | Material class | Fixed: FFF filament | Always written |
| Main 9 | Material type | Filament `material` | Only if exactly PLA, PETG, TPU, ABS, ASA, PC, PCTG, PP, PA6, PA11 or PA12 |
| Main 10 | Material name | Filament `name` | At most 63 bytes |
| Main 11 | Brand | Vendor `name` | At most 31 bytes |
| Main 52 | Material abbreviation | Filament `material` | At most 7 bytes |
| Main 6 | Brand-specific ID | Filament `article_number` | At most 16 bytes |
| Main 16 | Nominal weight (g) | Filament `weight` | |
| Main 17 | Actual initial weight (g) | Spool `initial_weight` | |
| Main 18 | Empty spool weight (g) | Spool `spool_weight`, else filament `spool_weight`, else vendor `empty_spool_weight` | |
| Main 29 | Density | Filament `density` | |
| Main 61 | Diameter (µm) | Filament `diameter` × 1000 | Must be a whole number of micrometres, at most 65,535 |
| Main 19 | Colour | Filament `color_hex` | 6 or 8 hex digits |
| Auxiliary 0 | Consumed weight (g) | Spool `used_weight` | Required |

Rules that apply to every row:

- A missing Spoolman value is left out. A real zero is written as zero.
- Text longer than its limit, or empty, is left out. It is never shortened.
- Numbers must be finite and not negative.
- Archived spools are refused: `Canonical active Spoolman spool required`.

Errors that stop a write before any byte changes:

| Message | Cause |
|---|---|
| `Canonical used_weight is missing or invalid` | Spool has no valid `used_weight` |
| `Spoolman diameter is not representable in integer micrometres` | Diameter is not a whole number of µm |
| `Canonical filament color is malformed` | `color_hex` is not 6 or 8 hex digits |
| `Canonical metadata does not fit this tag; no bytes written` | The encoded values need more space than the tag has, or a numeric field is negative, not finite or not a number, or a text field is not a string |

Not written: print and bed temperatures, price, location, lot number, comment.
These stay in Spoolman.

### Consumed-weight update

**Update consumed weight** in the browser writer changes only the consumed weight in the
auxiliary region. The main region stays byte for byte the same.
It needs a tag that already carries the same spool's instance UUID and an auxiliary region.

## How a write is carried out

1. The whole tag is read twice. Both reads must match.
2. Only blocks that differ from the new image are written. Each block is read back and compared
   right after it is written.
3. The blocks holding the instance UUID are written second to last and block 0 last.
4. The whole tag is read again, compared with the new image and decoded.
5. The station stores the tag UID and instance UUID on the spool in Spoolman.

Clearing writes zeros to every non-zero block in 0–77, block 0 last, then checks that all 312
bytes read back as zero.

One physical write or clear may take at most 120 seconds.
Saving a weight to Spoolman never writes to the tag.

## Fields the station reads

The reader decodes the main and auxiliary fields of the pinned specification revision and
ignores keys it does not know. Filament diameter is read from key 61 (µm) and from the older
key 30 (mm); if both are present they must agree.

## Link between tag and spool

Two Spoolman extra fields on the Spool entity tie a tag to a spool.
See [Required extra fields](../inventory/custom-fields.md).

| Default key | Content |
|---|---|
| `opentag_instance_uuid` | The UUID written to main key 0 |
| `nfc_uid` | The tag's UID |

## Specification revisions

| Use | Pinned commit |
|---|---|
| Reader field definitions | `e0dab1ae16838d2c342e7cfc509455441b7d8eba` |
| Writer and blank-image layout | `7e09cc38df1c8e7824a67f5b1ae93071f52519ad` |

CI checks out the writer revision from
<https://github.com/OpenPrintTag/openprinttag-specification> and compares the station's images with
the upstream tools.

## Not supported

- Tags other than NXP ICODE SLIX2, and any tag memory size other than 80 × 4 bytes, for writing.
- NFC-A tags (NTAG, MIFARE). The reader does not see them.
- Erasing tags that carry other data, such as a URL written by a phone.
- Locking, password protection or privacy mode.
- Writing raw blocks. The API accepts only a reviewed write or clear of the tag on the reader.

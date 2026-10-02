# What is stored on a tag

OpenPrintTag is an open format for describing a filament spool on an NFC tag.
The station writes a spool's details from Spoolman to the tag,
so the spool carries its own description and the station recognises it when you put it down.

## What the station writes

When you write a tag, the station builds the tag content from the Spoolman spool you chose.

| On the tag | Taken from Spoolman | Note |
|---|---|---|
| Unique ID of this tag's content | Spool extra field `opentag_instance_uuid` | The station creates one if the spool has none. Used to link tag and spool. |
| Kind of material | — | Always "filament" |
| Filament name | Filament **name** | Left out if empty or longer than 63 bytes |
| Brand | Vendor **name** | Left out if empty or longer than 31 bytes |
| Material | Filament **material** | Left out if empty or longer than 7 bytes |
| Material type | Filament **material** | Only when it is exactly PLA, PETG, TPU, ABS, ASA, PC, PCTG, PP, PA6, PA11 or PA12 |
| Maker's article number | Filament **article number** | Left out if empty or longer than 16 bytes |
| Nominal filament weight | Filament **weight** | |
| Filament weight of this spool when full | Spool **initial weight** | |
| Empty spool weight | Spool **empty weight**; if not set, the filament's; if not set, the vendor's | |
| Density | Filament **density** | |
| Diameter | Filament **diameter** | Must be a whole number of micrometres, for example 1.75 mm |
| Colour | Filament **colour** | 6 or 8 hex digits |
| Filament used so far | Spool **used weight** | Required |

Text that is too long is left out, not shortened.
A plain letter or digit is one byte; an accented or non-Latin letter counts as two or more.
Archived spools cannot be written to a tag.

## What stays only in Spoolman

Print and bed temperatures, price, location, lot number and comments are not written to the tag.

The remaining weight on the tag is a copy from the moment of writing.
Saving a new weight to Spoolman after weighing does **not** change the tag.
To bring the tag up to date, use **UPDATE TAG** (browser: **Update tag**):
see [Write or update a tag](../daily-use/manage-tags.md).
If nothing has changed, the station says `Tag already up to date` and writes nothing.

## How a tag is linked to a spool

After the tag is written and checked, the station saves two values on the spool in Spoolman:

| Spoolman extra field | Contains |
|---|---|
| `nfc_uid` | The tag's permanent ID |
| `opentag_instance_uuid` | The unique ID the station wrote to the tag |

When you place a tag, the station looks for the spool that carries these values.
That is how it knows which spool is on the scale.
Both fields must exist in Spoolman before you write the first tag:
[Required extra fields](../inventory/custom-fields.md).

A tag with valid OpenPrintTag data but no matching spool shows
`Not linked to a Spoolman spool yet`. Use **LINK TO A SPOOL** (browser: **Link to a spool**).

## Reading and clearing

- The station reads a tag by itself when you place it. Reading never changes the tag.
- It writes only after you confirm on the screen.
  After writing, it reads the whole tag again and compares it before it reports success.
- Clearing sets the data area to zeros and removes the two values from the spool in Spoolman.
  The spool itself stays in Spoolman. The tag's permanent ID does not change.
  See [Clear and reuse a tag](../daily-use/clear-reuse.md).

## Related

- [Which tags work](supported-tags.md)
- [OpenPrintTag format](../reference/openprinttag.md) — field numbers and limits for developers

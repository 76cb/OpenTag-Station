# What is stored on a tag

OpenPrintTag is an open format for describing a filament spool on an NFC tag.
The station writes a spool's details from Spoolman to the tag,
so the spool carries its own description and the station recognises it when you put it down.

## What the station writes

When you write a tag, the station builds the tag content from the Spoolman spool you chose.

| On the tag | Taken from Spoolman | Note |
|---|---|---|
| Unique ID of this tag's content | Spool extra field `opentag_instance_uuid`, if it exists | The station creates one if the spool has none. Used to link tag and spool. |
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
| Colour | Filament **colour** | 6 or 8 hex digits. A filament with several colours gets no colour on the tag. |
| Filament used so far | Spool **used weight** | Required |

Text that is too long is left out, not shortened. Before you write, the review page lists every
detail that will be left out ("Not written: …" on the touchscreen, "Not written to the tag:" in the
browser). See [Details that are not written to the tag](../daily-use/manage-tags.md#details-that-are-not-written-to-the-tag).
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

After the tag is written and checked, the station saves the link on the spool in Spoolman, in one
or both of these places:

| Where in Spoolman | Contains | When |
|---|---|---|
| The spool's tag list | The tag's permanent ID | Spoolman 0.27 or later |
| Spool extra field `nfc_uid` | The tag's permanent ID | When the extra fields exist |
| Spool extra field `opentag_instance_uuid` | The unique ID the station wrote to the tag | When the extra fields exist |

When you place a tag, the station looks for the spool that holds these values, starting with
Spoolman's tag list. That is how it knows which spool is on the scale.
On Spoolman older than 0.27 both extra fields must exist before you write the first tag; on 0.27
or later they are optional. See [Required extra fields](../inventory/custom-fields.md).
Without the extra fields, the unique ID is kept only on the tag and on the station.

A tag with valid OpenPrintTag data but no matching spool shows
`Not linked to a Spoolman spool yet`. Use **LINK TO A SPOOL** (browser: **Link to a spool**).

## Reading and clearing

- The station reads a tag by itself when you place it. Reading never changes the tag.
- It writes only after you confirm on the screen.
  After writing, it reads the whole tag again and compares it before it reports success.
- Clearing sets the data area to zeros and removes the link from the spool in Spoolman.
  The spool itself stays in Spoolman. The tag's permanent ID does not change.
  See [Clear and reuse a tag](../daily-use/clear-reuse.md).

## Related

- [Which tags work](supported-tags.md)
- [OpenPrintTag format](../reference/openprinttag.md) — field numbers and limits for developers

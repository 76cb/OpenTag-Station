# Which tags work

The station works with one kind of tag: **NXP ICODE SLIX2**, blank.
Buy the right kind and every other page of this manual applies.
The reader looks only for NFC-V (ISO 15693) tags: other NFC-V tags are read but cannot be written,
and NTAG or MIFARE tags are not seen at all.

## The supported tag

| Property | Required |
|---|---|
| Chip | NXP ICODE SLIX2 |
| Technology | NFC-V, also called ISO 15693 |
| Memory | 80 blocks of 4 bytes (320 bytes) |
| Tag ID (UID) | Starts with `E0:04`, the NXP manufacturer code |
| Content | Blank, or already an OpenPrintTag |
| Protection | No locked blocks, no write protection |

The station uses the first 312 bytes (blocks 0–77) and never touches the last two blocks.
The tag's ID is permanent; writing or clearing does not change it.

## What "blank" means

For the station, blank means **every byte of the data area is zero**.
It then shows `BLANK TAG` / `Ready to use` on the touchscreen (browser: **Blank tag**, **Ready to assign**).

Many NFC stickers are sold for phones and arrive "formatted" or with a web link stored on them.
Those are not blank. The station shows `This tag can't be used as it is`,
and it cannot erase them: **CLEAR / REUSE** only works on tags that hold OpenPrintTag data.

!!! warning "Phone apps usually do not make a tag blank"
    The "erase" or "format" function of a phone NFC app normally writes an empty record to the tag
    rather than zeros. To the station such a tag is still not blank.
    Only writing zeros to every block would help, and the project has not tested any app for that.
    Buy tags that are sold as blank or unformatted.

## Other tag types

| Tag | What happens |
|---|---|
| NTAG213 / 215 / 216, MIFARE and other NFC-A, NFC-B or NFC-F tags | The reader does not see them at all. The screen keeps showing `PLACE A TAG`. |
| ISO 15693 tags that are not SLIX2 (other maker, other memory size) | The station reads them, so OpenPrintTag data on them is shown. A blank one shows `This tag can't be used as it is`. Writing and clearing are refused with `Incompatible tag: this release approves NXP 80 x 4-byte SLIX2 only`. |
| SLIX2 with phone formatting, a web link or other data | `This tag can't be used as it is` |
| SLIX2 with locked blocks | `Tag has protected blocks` |

## Buying

- Check that the listing names the chip **ICODE SLIX2**.
  A listing that names NTAG or MIFARE is the wrong type.
- Prefer listings that say blank or unformatted.
- Buy a few first and test them before you order many.
- The project names no particular sellers, sizes or shapes.

## Test a new tag

1. Place **one** tag on the reader.
2. Touchscreen: tap **MANAGE TAG**. Browser: the Dashboard shows the tag.
3. A usable new tag shows `BLANK TAG` / `Ready to use` and the button **ASSIGN TAG**
   (browser: **Blank tag** and **Assign tag**).

In the browser, **Manage tag → Tag metadata & advanced details** shows the tag ID and memory size.

Next: [Write or update a tag](../daily-use/manage-tags.md).
If the tag is not found: [Tag is not detected](../troubleshooting/tag.md).

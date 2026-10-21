# Which tags work

The station works with one kind of tag: **NXP ICODE SLIX2**, blank.
Buy the right kind and every other page of this manual applies.
The reader reads only NFC-V (ISO 15693) tags: other NFC-V tags are read but cannot be written.
NTAG and MIFARE tags (NFC-A, ISO 14443A) are found and named, but nothing on them is read yet.

## The supported tag

| Property | Required |
|---|---|
| Chip | NXP ICODE SLIX2 |
| Technology | NFC-V, also called ISO 15693 |
| Memory | 80 blocks of 4 bytes (320 bytes) |
| Tag ID (UID) | Starts with `E0:04`, the NXP manufacturer code |
| Content | Blank, or already an OpenPrintTag |
| Protection | No locked blocks in the first 78 blocks (0–77), no write protection |

The station uses the first 312 bytes (blocks 0–77) and never touches the last two blocks.
Those two blocks may be locked; the station reads their lock state but does not act on it.
The tag's ID is permanent; writing or clearing does not change it.

## What "blank" means

For the station, blank means **every byte of the data area (blocks 0–77) is zero**.

A blank tag is shown like this:

| Where | What you see |
|---|---|
| Touchscreen, Home | `COMPATIBLE BLANK TAG`, `Ready to write`, `Open Manage tag to choose a spool` |
| Touchscreen, **Manage tag** | `BLANK TAG`, `Ready to use` and the tag's ID, with the button **WRITE TAG** |
| Browser, Dashboard | `Blank tag`, `Ready to write`, with the buttons **Write tag** and **Write tag for a spool** |
| Browser, **Manage tag** | `Compatible blank tag`, `Ready to write`, with the badge `Blank` |

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
| NTAG213 / 215 / 216, MIFARE Classic and other NFC-A (ISO 14443A) tags | Found and named, not read: the touchscreen says `Tag type not supported yet` with the tag's kind and ID, the browser `Not supported yet`. Reading them is planned (NTAG: issue #95, MIFARE Classic: issue #94). |
| NFC-B or NFC-F tags | The reader does not see them at all. Home keeps showing `Place a spool`, and **Manage tag** keeps showing `PLACE A TAG`. |
| ISO 15693 tags that are not SLIX2 (other maker, other memory size) | The station reads them, so OpenPrintTag data on them is shown. A blank one shows `This tag can't be used as it is`. Writing and clearing are refused with `Incompatible tag: this release approves NXP 80 x 4-byte SLIX2 only`. |
| ISO 15693 tags with more than 4096 bytes of memory, or blocks larger than 32 bytes | The station does not read them. Home shows `Place a spool`, **Manage tag** shows `PLACE A TAG`, and the NFC READER setup step says `NFC reader problem. Check the NFC wiring.` The browser's **Manage tag** shows `NFC reader error` with `NFC-V tag geometry is invalid or too large` (too much memory) or `Unsupported NFC-V geometry` (blocks too large). |
| SLIX2 with phone formatting, a web link or other data | `This tag can't be used as it is` |
| SLIX2 with locked blocks among blocks 0–77 | The tag is read and shown as usual. Writing and clearing are refused with `Tag has protected blocks`. |

A tag that can't be used is shown like this:

| Where | What you see |
|---|---|
| Touchscreen, Home | `Place a spool`, as if no tag were there |
| Touchscreen, **Manage tag** | `This tag can't be used as it is`, the reader's reason, and `Use a blank NXP ICODE SLIX2 tag.` |
| Browser, Dashboard | `Reading spool…` and `Tag needs attention` |
| Browser, **Manage tag** | The badge `Unsupported` and `OpenPrintTag decode failed / unsupported` |

## Buying

- Check that the listing names the chip **ICODE SLIX2**.
  A listing that names NTAG or MIFARE is the wrong type.
- Prefer listings that say blank or unformatted.
- Buy a few first and test them before you order many.
- The project names no particular sellers, sizes or shapes.

## Test a new tag

1. Place **one** tag on the reader. With two tags, **Manage tag** says `Two tags on the reader. Remove one.`
2. Touchscreen: Home shows `COMPATIBLE BLANK TAG`; tap **MANAGE TAG**. Browser: the Dashboard shows the tag.
3. A usable new tag shows `BLANK TAG` / `Ready to use` and the button **WRITE TAG**
   (browser: **Blank tag** and **Write tag**).

In the browser, **Manage tag → Tag metadata & advanced details** shows the tag ID and memory size.

Next: [Write or update a tag](../daily-use/manage-tags.md).
If the tag is not found: [Tag is not detected](../troubleshooting/tag.md).

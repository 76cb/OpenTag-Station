# Tag write or clear fails

When a write or clear cannot be done, the touchscreen shows **Needs attention** with a message
and a **BACK** button. The browser shows the same message in the tag dialog.
Find the message below.

If the screen says **Finish writing this tag**, **Finish clearing this tag**,
**Link not saved yet** or **Unlink not finished**, go to
[Finish an interrupted tag operation](journal.md) instead.

## The tag is refused

Nothing has been written when you see one of these.

| Message | What it means | What to do |
|---|---|---|
| `Incompatible tag: this release approves NXP 80 x 4-byte SLIX2 only` | The tag is not an ICODE SLIX2. | Use a [supported tag](../openprinttag/supported-tags.md). |
| `Unsupported or incomplete OpenPrintTag; refusing unknown data overwrite` | The tag holds data that is neither blank nor OpenPrintTag, for example a phone format or a web link. The station will not overwrite or clear it. | Use a blank tag. |
| `Tag has protected blocks` | Part of the tag is locked. | The tag cannot be used. Take another one. |
| `OpenPrintTag declares write protection` | The tag's data says it must not be changed. | The tag cannot be rewritten by the station. Take another one. |
| `Place exactly one stable NFC-V tag` | No tag, or more than one, is on the reader. | Place one tag and keep it still. |

## The tag changed between review and writing

The station compares the tag with what it showed you on the review screen.
If anything differs before writing starts, it stops and nothing is written.

| Message | What to do |
|---|---|
| `Unstable tag content; preview again` | Reposition the tag so it reads steadily, then start again. |
| `Tag generation changed; read and preview again` | The tag was lifted or swapped. Start again from **Manage tag**. If this appears while the tag is being written, part of the tag may already be changed: put the **same** tag back and use **FINISH WRITING** / **FINISH CLEARING**. |
| `Tag bytes changed after preview` | Start again from **Manage tag**. |
| `Spoolman settings changed; preview again before writing` | The Spoolman settings were changed meanwhile. Start again. |
| `Spoolman changed since this editor was opened. Refresh and review again.` | Someone else edited the spool. Reload and review again. |

## The spool data cannot be put on a tag

Fix the record in Spoolman, then write again.

| Message | What it means | What to do |
|---|---|---|
| `Canonical metadata does not fit this tag; no bytes written` | The spool's details do not fit, or a weight, density or other number in Spoolman is negative or not a number. | Check the spool's and filament's numbers in Spoolman; shorten long names. |
| `Spoolman diameter is not representable in integer micrometres` | The filament diameter has too many decimals or is out of range. | Set a normal value such as 1.75 or 2.85 in Spoolman. |
| `Canonical filament color is malformed` | The filament colour is not 6 or 8 hex digits. | Correct the colour in Spoolman. |
| `Canonical used_weight is missing or invalid` | The spool has no valid used weight. | Set the used weight in Spoolman (0 for a new spool). |
| `Canonical active Spoolman spool required` | The spool is archived. | Un-archive it in Spoolman or choose another spool. |
| `Spoolman is missing the Spool extra field…` | The two extra fields do not exist in Spoolman. | [Required extra fields](../inventory/custom-fields.md) |
| `Multiple NFC UID owners; clean up Spoolman before continuing` | Two or more spools in Spoolman carry this tag's `nfc_uid`. | In Spoolman, empty `nfc_uid` and `opentag_instance_uuid` on the spool that should not have the tag. |
| `Instance UUID belongs to another Spoolman spool` | The unique ID for this tag is already saved on a different spool. | The same. |

## Writing or clearing stopped part-way

Part of the tag may already be changed.
Whenever the station offers **FINISH WRITING** or **FINISH CLEARING**, put the **same** tag back
and use it. See [Finish an interrupted tag operation](journal.md).

| Message | What it means | What to do |
|---|---|---|
| `Block readback mismatch; write stopped` | The station checked a block after writing it, and it did not match. Usually the tag moved. | Put the **same** tag back and finish. |
| `Complete physical readback mismatch` | The station read the whole tag after writing, and it did not match. | The same. |
| `Final decoded semantic verification failed` | The written tag does not hold the expected spool details. | The same. |
| `Final blank verification failed` | After clearing, the tag is not blank. | The same. |
| `Tag removed, replaced, or multiple tags present` | The tag moved or was swapped, or a second tag came into range. | Put the **same** tag back, alone. If the station offers to finish, do so; otherwise start again. |
| `Physical NFC operation exceeded its bounded deadline` | The operation took longer than two minutes. | Reposition the tag. If the station offers to finish, do so; otherwise try again. |
| `NFC bus errors must be zero` | The connection to the reader module is unreliable. | Check the NFC [wiring](../hardware/wiring.md) and [power](../hardware/power.md). |
| `Torn/unknown block differs from both journal images; refusing rewrite` | Part of the tag matches neither the old nor the new data. | The tag cannot be finished. Use **SKIP RECOVERY** and discard the tag. See [Finish an interrupted tag operation](journal.md#skip-recovery). |

## The station is busy

| Message | What to do |
|---|---|
| `Station is busy. Please try again.` | Wait a few seconds and try again. |
| `Station response unavailable; try again` | The same. |
| `This is taking longer than usual.` with **BACK TO HOME** | The station is waiting for Spoolman. Tap **BACK TO HOME**, check [Spoolman](spoolman.md), then open **Manage tag** again. |
| `Tag workspace unavailable. Please restart.` | Restart the station. |
| `Another tag (…) has an unfinished write or clear…` | [Finish an interrupted tag operation](journal.md) |

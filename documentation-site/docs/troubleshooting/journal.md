# Finish an interrupted tag operation

If a tag write or clear is interrupted — the tag moved, power was lost, Spoolman was offline —
the station remembers where it stopped and offers to finish, even after a restart.
Until then it will not start a write or clear on a different tag.

## What you see and what to do

Open **MANAGE TAG** on the touchscreen (browser: **Manage tag**).

| Touchscreen title | What it means | What to do |
|---|---|---|
| **Finish writing this tag** | A write stopped part-way. The tag may be half-written. | Put the **same** tag back on the reader and tap **FINISH WRITING**. |
| **Finish clearing this tag** | A clear stopped part-way. | Put the **same** tag back and tap **FINISH CLEARING**. |
| **Link not saved yet** | The tag is written and checked. Saving the link in Spoolman failed; the reason is shown. | Fix the reason, for example start Spoolman. Then tap **TRY SAVING LINK AGAIN**. The tag is not written again. |
| **Unlink not finished** | The tag is erased. Removing its link in Spoolman failed. | Fix the reason, then tap **TRY UNLINKING AGAIN**. The tag does not need to be on the reader. |

In the browser the same situations look like this:

| Situation | Browser |
|---|---|
| Write interrupted | **Manage tag** shows the button **Resume tag workflow**. |
| Link not saved | **Retry Spoolman link** or **Skip linking** in the tag dialog. |
| Clear interrupted | The **Reuse this NFC tag?** dialog opens again. |
| Unlink not finished | `Tag cleared · cleanup pending` with **Retry Spoolman cleanup** or **Skip cleanup**. In **Manage tag** the button reads **Retry cleanup**. |

## Messages that point here

| Message | What to do |
|---|---|
| `Another tag (…) has an unfinished write or clear. Place that tag on the reader to finish it first. If it is lost, choose Skip recovery on the Tag screen.` | Finish the tag named in the message first, or skip its recovery. |
| `Retry pending association first` | Finish **Link not saved yet** first. |
| `Retry pending unlink first` | Finish **Unlink not finished** first. |
| `Finish Clear / Reuse recovery before another operation` | Finish **Finish clearing this tag** first. |
| `Clearing this tag was interrupted and the Spoolman settings have changed since. Restore the earlier Spoolman settings to finish, or choose Skip recovery on the station's Tag screen.` | Set the Spoolman settings (address, extra field names) back to what they were when you started the clear, then finish. Or skip recovery. |

## Skip recovery

Use this only when you cannot finish: the tag is lost or damaged, or the spool no longer exists
in Spoolman.

Touchscreen: tap the red **SKIP RECOVERY**, read the **Skip recovery?** screen,
then tap **SKIP RECOVERY** again. **BACK** returns without skipping.
Browser: **Skip linking** or **Skip cleanup**. An interrupted write or clear can only be skipped
on the touchscreen.

The station then stops trying and shows **Recovery skipped**.
It changes neither the tag nor Spoolman, so what is left depends on where it stopped:

| You skipped | What is left behind | What to do afterwards |
|---|---|---|
| **Finish writing this tag** or **Finish clearing this tag** | The tag may be half-written. The station cannot finish it, and may not be able to write or clear it either. | Skip only if the tag is lost or damaged. If you still have it, place it again; discard it if the station says it can't be used. |
| **Link not saved yet** | The tag is written, but Spoolman does not know it belongs to the spool. | Link it later: **Manage tag → LINK TO A SPOOL** (browser: **Link to a spool**). |
| **Unlink not finished** | The tag is erased, but Spoolman may still list it on the old spool. | In Spoolman, open the old spool and empty its `nfc_uid` and `opentag_instance_uuid` fields. |

## Good to know

- An unfinished operation survives a restart.
- A [factory reset or USB reinstall](recovery.md) forgets it. A half-written tag can then no longer
  be finished.
- To avoid interruptions, keep the tag on the reader until the screen says done.
  While the tag is being written the screen shows `Keep the tag on the reader.` and a block counter.

## Related

- [Tag write or clear fails](write.md)
- [Clear and reuse a tag](../daily-use/clear-reuse.md)
- [Spoolman problems](spoolman.md)

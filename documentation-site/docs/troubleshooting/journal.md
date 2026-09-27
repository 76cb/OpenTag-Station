# Unfinished tag operations

If a tag write or clear is interrupted (tag moved, power lost, Spoolman offline),
the station remembers where it stopped and offers to finish it, even after a
restart.

## What you may see

| Screen | Meaning | What to do |
|---|---|---|
| **Finish writing this tag** | A write stopped part-way. | Put the same tag back and tap **FINISH WRITING**. |
| **Finish clearing this tag** | A clear stopped part-way. | Put the same tag back and tap **FINISH CLEARING**. |
| **Link not saved yet** | The tag is written and checked; saving the link in Spoolman failed. The reason is shown. | Fix the reason (for example start Spoolman), then tap **TRY SAVING LINK AGAIN**. The tag is not written again. |
| **Unlink not finished** | The tag is erased; removing its link in Spoolman failed. | Fix the reason, then tap **TRY UNLINKING AGAIN**. The tag does not need to be on the reader. |
| "Another tag (…) has an unfinished write or clear" | You tried to write a different tag while one is unfinished. | Finish the first tag, or skip its recovery. |

## Skip recovery

If you can't finish (the tag is lost, or the spool was deleted in Spoolman), use
**SKIP RECOVERY** on the touchscreen, or **Skip linking** / **Skip cleanup** in the
browser. The station stops trying. It does not change the tag or Spoolman, so:

- skip an unfinished **write or clear** only if that tag is lost or damaged: a
  half-written tag can't be read, written or cleared by the station afterwards;
- after skipping a link, link the tag again later from Manage tag;
- after skipping an unlink, check the old spool in Spoolman and empty its
  `nfc_uid` and `opentag_instance_uuid` fields yourself.

A factory reset also forgets unfinished tag operations.

See the [recovery model](../openprinttag/recovery.md) for how interruption is
detected safely.

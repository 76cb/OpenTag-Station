# Clear and reuse a tag

Erase a tag when its spool is used up, so the tag can go onto a new spool.
Clearing does two things: it erases the filament data on the tag, and it removes the tag's link from
the old spool in Spoolman.

## What is erased and what stays

| | Erased or removed | Stays |
|---|---|---|
| On the tag | All filament data the station wrote | The tag's permanent UID |
| In Spoolman | The tag's link on the old spool: the values in the two extra fields `nfc_uid` and `opentag_instance_uuid` | The spool itself, its filament, its weights and its usage |

After clearing, the tag is blank and can be written again.
Only tags that hold OpenPrintTag data can be cleared.
A blank tag needs no clearing and has no **CLEAR / REUSE** button.
A tag with other data ("This tag can't be used as it is") cannot be erased by the station.

## Steps

### Touchscreen

1. Put the tag on the reader and tap **MANAGE TAG**.
2. Tap the red **CLEAR / REUSE** button. The station reads the tag.
3. Read "Erase this tag?": "Erases the filament data on the tag and removes its link in Spoolman."
   and "The spool itself stays in Spoolman."
   Tap the red **ERASE TAG** to go on, or **BACK** to leave the tag as it is.
4. Keep the tag on the reader. The page shows "Clearing tag" with a block count, then
   "Removing link in Spoolman" with "The tag is already erased."
5. Done: "TAG READY TO REUSE", "The old filament information is removed." and
   "What should this tag become?" Choose one:
    - **CHOOSE EXISTING SPOOL** to write the tag for a spool that is already in Spoolman.
    - **NEW SPOOL FROM A FILAMENT** to create a spool from a filament and write the tag for it.
    - **DONE** to leave the tag blank.

The first two continue with the steps in [Write or update a tag](manage-tags.md).

### Browser

1. Put the tag on the reader. On the Dashboard click **Manage tag**, then **Clear / Reuse**.
2. The dialog "Reuse this NFC tag?" explains:
   "This removes the filament information and unlinks the tag from Spoolman. Its permanent NFC
   identifier will not change." It lists the tag, its current material and the Spoolman spool, and
   says "Review this exact tag before clearing".
3. Click the red **Clear tag**, or **Cancel**.
4. Keep the tag on the reader and the station powered while the progress line counts the blocks.
5. Done: the title reads "Tag ready to reuse · verified".
   Click **Assign tag to a spool** to write it again straight away, or **Done**.

## If Spoolman cannot be reached in the middle

The tag is erased first and Spoolman is updated second.
If Spoolman does not answer at that moment, the tag is already blank but the old spool in Spoolman
still lists it. The station remembers this, even across a restart, and asks you to finish.

| | Touchscreen | Browser |
|---|---|---|
| What you see | Page title "Unlink not finished" with "The tag is cleared. Removing its link in Spoolman is still pending." | "Tag cleared · cleanup pending" and "Tag blank and verified. Spoolman cleanup is still pending. Retry cleanup without rewriting the tag." |
| Finish it | **TRY UNLINKING AGAIN** | **Retry Spoolman cleanup** in the dialog. In **Manage tag** the button reads **Retry cleanup**. |
| Give up | Red **SKIP RECOVERY**, then confirm on "Skip recovery?" | Red **Skip cleanup**, then confirm |

Finishing needs Spoolman to be reachable again. It does not need the tag, and it does not write to
the tag.

Skipping leaves Spoolman as it is. The touchscreen says so before you confirm:
"The tag is erased, but Spoolman may still list it on the old spool."
If you skip, empty the `nfc_uid` and `opentag_instance_uuid` fields on the old spool in Spoolman
yourself.

Until this is finished or skipped, the station does not start another tag write or clear.
Full details for every unfinished state: [Finish an interrupted tag operation](../troubleshooting/journal.md).

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "Finish clearing this tag" with "Clearing this tag was interrupted. Place the same tag on the reader to finish clearing it." | The tag was removed, or power was lost, while it was being erased. | Put the same tag back and tap **FINISH CLEARING**. See [Finish an interrupted tag operation](../troubleshooting/journal.md). |
| "Unlink not finished" | The tag is blank; Spoolman was not updated. | See the section above. |
| "Spoolman is missing the Spool extra field …" | The two extra fields are missing in Spoolman. The tag is already erased; only the link is left. | Add the fields ([Required extra fields](../inventory/custom-fields.md)), then finish as described in the section above. |
| "This tag can't be used as it is" | The tag holds data the station does not recognise, so it will not erase it. | Use another tag. See [Which tags work](../openprinttag/supported-tags.md). |
| Any other error while erasing | The tag or reader refused the operation. | See [Tag write or clear fails](../troubleshooting/write.md). |

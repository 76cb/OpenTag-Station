# Update the firmware

Update a working station from its web page. Your settings and the scale calibration are kept, and
the station goes back to the previous version by itself if the new one does not start properly.

Updating is only possible in the browser; the touchscreen has no update function.

## Before you start

- Download `opentag-station-<version>-application.bin` from the project's
  [GitHub Releases page](https://github.com/76cb/OpenTag-Station/releases).
  Do **not** use `opentag-station-factory.bin`; that file is only for the
  [USB installer](web-flasher.md).
- Finish any tag write or clear that is in progress.
- Optional: [download your settings](../configuration/backup.md).

To see which version is running: touchscreen **Settings** → **About / Advanced**, or in the browser
the **Current build** line on the update card.

## Steps

1. Open the station's web page and go to **Settings** → **Advanced**.
2. Scroll to **Updates and device controls** and find the **Update Existing Station** card.
3. Under **WT32-SC01 Plus firmware image (.bin)**, choose the file you downloaded.
   The line below changes to "… · calculating SHA-256…" and then "… · ready to upload".
4. Click **Upload and check**. Confirm the question
   "Upload this image to the inactive slot? Upload completion does not activate or confirm the
   firmware." If an access token is set, the browser asks for it.
5. Keep the page open while **Transfer progress** fills. Other actions are paused during the
   upload. The list on the card then reads:
    - "Upload completed"
    - "File checked"
    - "Update installed, waiting for restart"
6. Click **Restart to finish update** and confirm:
   "Restart now to finish the update? The station checks itself for 30 seconds after restarting
   and returns to the previous firmware if something is wrong."
7. Wait for the station to restart, then reload the page.

Up to step 6 the old version keeps running. To back out, click **Cancel update** and confirm
"Cancel this update and keep the current firmware?".

## What you should see

About half a minute after the restart the list on the card reads "Restarted into new firmware" and
"Update confirmed", and **Current build** shows the new version.

## The self-check after restart

For 30 seconds after the restart the station checks that its own parts have started: storage,
settings, the touchscreen, the scale and network tasks, the web page and the updater.
If all are running after 30 seconds, the update is kept. If not — or if the station crashes or
loses power during that time — it starts the previous version again.

The self-check does **not** test your Wi-Fi connection, Spoolman, the tag reader or the load cell.
After an update, read a tag and weigh a spool once to be sure.

## What is kept and what is checked

- Kept: Wi-Fi, Spoolman and printer settings, the access token and the scale calibration.
- The file may be at most 5 MiB.
- The station checks that the file arrived intact, is a firmware image for this board, and is
  OpenTag Station firmware. The file name does not matter.
- Firmware files are not signed. Only upload files you downloaded from the project's releases
  page; see [Access token and network safety](../configuration/security.md).

## If something goes wrong

Messages appear on the update card or as a pop-up message.

| What you see | What it means | What to do |
|---|---|---|
| "The image must be between 1 byte and …" or "Firmware image size is zero or exceeds the inactive OTA slot" | The file is too large. You probably chose the factory image. | Choose `opentag-station-<version>-application.bin`. |
| "Firmware image structure validation failed" | The file is not a firmware image, or the download is damaged. | Download the file again. |
| "Firmware image target is incompatible with this station" | The file was built for a different board. | Use the file from this project's releases. |
| "Firmware image project identity is incompatible" | The file is not OpenTag Station firmware. | Use the file from this project's releases. |
| "Firmware SHA-256 does not match the declared digest" | The file changed on the way to the station. | Upload it again. |
| "Firmware upload ended before the declared byte count" | The connection dropped during the upload. | Upload it again. |
| "The firmware upload connection was interrupted. The station may have received the image; inspect update status before retrying." | The browser lost contact; the station may have the file anyway. | Reload the page and read the list on the update card before uploading again. |
| "A candidate image must be confirmed or rolled back before another upload" | An earlier update is still waiting. | Click **Restart to finish update**, or **Cancel update**. |
| "The previous update did not complete; upload it again." / "The update was selected but never started; upload it again." / "The update record does not match the running firmware; upload it again." | An earlier attempt was left unfinished. | Upload the file again. |
| "Device lifecycle is owned by …" | The station is busy with another restart, reset or update. | Wait a moment and try again. |
| "New firmware failed; previous firmware restored" and "Update was not kept" | The new version did not pass the self-check. The station is running the previous version. | Check that you used the right file and try once more. If it happens again, stay on the current version and report it. |
| The station does not start at all after an update | The automatic return did not help. | [Reinstall over USB](../troubleshooting/recovery.md). |

## Related

- [Firmware update internals](../reference/ota.md)
- [Factory reset and USB recovery](../troubleshooting/recovery.md)

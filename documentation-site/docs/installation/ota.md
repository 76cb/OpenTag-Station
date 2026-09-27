# Update the firmware (keeps your settings)

**Update Existing Station** installs new firmware over Wi-Fi. It keeps Wi-Fi,
Spoolman, FilaBridge and printer settings and the scale calibration. If the new
firmware fails its self-check after restarting, the station goes back to the
previous firmware by itself.

## Before you start

- Download `opentag-station-<version>-application.bin` from the
  [GitHub releases page](https://github.com/76cb/OpenTag-Station/releases).
  Release candidates are listed as pre-releases.
- Do **not** use `opentag-station-factory.bin` for this; that file is only for
  USB install and it erases settings.
- Finish any tag write or weight update first. The station refuses to restart
  while one is running.
- Optional: export a configuration backup (Settings → Advanced).

## Steps

1. In the station's browser page open **Settings → Advanced → Updates and device
   controls**.
2. Under **WT32-SC01 Plus firmware image**, choose the `application.bin` file.
   The browser calculates its checksum.
3. Tap **Upload and check**. Wait until the file is checked and installed; the
   current firmware keeps running.
4. Tap **Restart to finish update** and confirm.
5. Wait about a minute, reload the page, and check that the version under
   **Current build** is the new one and the update says **Update confirmed**.

## If it fails

- A wrong or damaged file is refused before anything is installed.
- If the new firmware fails its self-check, the station restarts into the previous
  firmware and the update shows **Update was not kept**.
- Firmware files are checked for corruption but are not signed. Only install files
  from the official releases page.
- If the station does not start at all, use [USB recovery](web-flasher.md).

Details of every update state and power-loss case are in the
[OTA reference](../reference/ota.md).

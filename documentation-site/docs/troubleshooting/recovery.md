# Factory reset and USB recovery

Three ways to recover a station, from mild to drastic: restart it, reset its settings, or install
the firmware again over USB. None of them changes anything in Spoolman or on your tags.

| | Restart | Factory reset | USB install |
|---|---|---|---|
| Where | Browser | Browser | Computer with Chrome or Edge, USB cable |
| Settings and calibration | Kept | **Erased** | **Erased** |
| Firmware | Kept | Kept | Replaced |
| Use it when | Something is stuck | You want a clean start, or are giving the station away | The station does not start, or you cannot reach it at all |

Try the milder option first. A forgotten access token does not need a reset; see
[Access token and network safety](../configuration/security.md).

## Restart the station

1. Open the station's web page and go to **Settings** → **Advanced** → **Device controls**.
2. Click **Reboot device** and confirm "Reboot OpenTag Station now?".

Unplugging and reconnecting the power does the same.

## Factory reset

A factory reset is only possible in the browser. The touchscreen has no reset.

!!! warning "This cannot be undone"
    A factory reset erases all settings and the scale calibration.
    [Download your settings](../configuration/backup.md) first if you want a head start afterwards.

1. Open the station's web page and go to **Settings** → **Advanced** → **Device controls**.
2. Type `FACTORY RESET` (in capitals) into the field **Type FACTORY RESET to enable reset**.
3. Click **Factory reset**.
4. Confirm "Factory reset erases local configuration and calibration, then reboots. This cannot be
   undone. Continue?" If an access token is set, the browser asks for it.

The station restarts and the touchscreen shows **First-run setup**.

### What a factory reset erases

| Erased | Kept |
|---|---|
| Wi-Fi name and password | The firmware version that is installed |
| Hostname, brightness and other station settings | Everything in Spoolman, including which tag belongs to which spool |
| Spoolman and FilaBridge addresses and tokens, the selected printer | The data on your tags |
| The access token | |
| Load-cell profile, toolhead profiles, weight-update settings | |
| Scale calibration | |
| Any unfinished tag write or clear the station was waiting to finish | |

If a tag was in the middle of being written or cleared when you reset, the station no longer
knows how to finish it. That tag may show "This tag can't be used as it is" and cannot be used
again. Finish such operations before you reset; see
[Finish an interrupted tag operation](journal.md).

## Reinstall over USB

Use this as the last resort: the station does not start, the screen stays dark, or you can reach
neither the web page nor the touchscreen.

Follow [Install the firmware over USB](../installation/web-flasher.md). It erases the same things
as a factory reset and also replaces the firmware.

If the station still starts and you only want a newer version, use
[Update the firmware](../installation/ota.md) instead; that keeps everything.

## After a factory reset or USB install

Set the station up again in this order:

1. [First boot and Wi-Fi](../getting-started/first-boot.md) — Wi-Fi name and password.
2. Optional: [restore your settings file](../configuration/backup.md). It brings back addresses,
   the selected printer, the load-cell profile and the toolhead profiles, but no passwords, no
   tokens and no calibration.
3. [Connect Spoolman and FilaBridge](../getting-started/initial-setup.md) — check the addresses,
   re-enter tokens if you use them, click **Test**.
4. [Tare and calibrate](../scale/calibration.md) — the calibration is always lost.
5. [Set the access token](../configuration/security.md) again.

You do not need to rewrite your tags or add the extra fields in Spoolman again. Put a tagged spool
on the station and it is recognised as before.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| The **Factory reset** button stays grey | The text in the field is not exactly `FACTORY RESET`. | Type it in capitals with one space. |
| You cannot open the web page to reset | The station is not on your network. | Fix the Wi-Fi from the touchscreen first; see [Wi-Fi and reaching the station](wifi.md). |
| "Device lifecycle is owned by …" | The station is busy with an update or another restart. | Wait a moment and try again. |
| After the reset the **NFC READER** setup step shows "NFC reader starting..." | The reader waits for Wi-Fi. | Finish the Wi-Fi setup and wait about half a minute. |

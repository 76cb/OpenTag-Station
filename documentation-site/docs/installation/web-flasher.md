# Install the firmware over USB

Use the USB installer to put the firmware on a new board, or to rescue a station that no longer
starts. It runs in your browser; there is nothing to install on your computer.

!!! warning "The USB install erases the station"
    It replaces the firmware **and** erases every setting and the scale calibration.
    For a station that already works, use [Update the firmware](ota.md) instead — that keeps
    everything.

## Before you start

- A desktop computer with **Chrome** or **Edge**. Other browsers and phones cannot talk to the
  board over USB.
- A USB cable that carries data, not only power.
- For a station that is already set up: [download your settings](../configuration/backup.md) first,
  if the station still lets you.

## Steps

1. Connect the WT32-SC01 Plus to the computer with the USB cable.
2. Open the installer: <https://76cb.github.io/OpenTag-Station/>.
   The page is titled **Station Firmware**.
3. In the **Factory Install / Recovery** section, click the **Factory Install / Recovery** button.
4. Your browser asks which serial port to use. Choose the station's port and connect.
5. Follow the installer's prompts and let it finish. Leave the cable connected and the page open
   until it says it is done. If it asks whether to erase the device, your answer does not change
   the outcome for settings: they are replaced either way.
6. The station restarts and the touchscreen shows **First-run setup**.

Continue with [First boot and Wi-Fi](../getting-started/first-boot.md).

## What is erased

| Erased | Not touched |
|---|---|
| Wi-Fi name and password | Your spools and filaments in Spoolman |
| Spoolman and FilaBridge addresses and tokens | The data on your tags |
| The access token | The links between tags and spools stored in Spoolman |
| Scale calibration and load-cell choice | |
| Toolhead profiles and other settings | |
| Any unfinished tag write or clear the station was waiting to finish | |

After a USB install you set the station up again from the start; see
[what to redo](../troubleshooting/recovery.md#after-a-factory-reset-or-usb-install).

## USB install or update?

| Situation | Use |
|---|---|
| New board, never had the firmware | USB install (this page) |
| Station works and you want a newer version | [Update the firmware](ota.md) |
| Station does not start, or its web page and touchscreen are both unusable | USB install (this page) |
| You want to wipe the settings but the station works | [Factory reset](../troubleshooting/recovery.md) |

The installer uses the file `opentag-station-factory.bin`. Never upload that file through the
station's web page; the update there needs `opentag-station-<version>-application.bin`.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "This installer requires desktop Chrome or Edge with Web Serial support." | Your browser cannot use USB serial ports. | Open the installer in Chrome or Edge on a desktop computer. |
| "Open this installer from its HTTPS GitHub Pages address." | The page was opened from a copy or over plain HTTP. | Open <https://76cb.github.io/OpenTag-Station/> directly. |
| The station's port is not in the list | The computer does not see the board. | Try another USB cable (many are charge-only) and another USB port, then reload the page. |
| The port is there but the connection fails | The board did not switch to its download mode by itself. | Put the board into download mode by hand and try again. The button procedure is described in the board maker's documentation for the WT32-SC01 Plus; this manual does not cover it. |
| The install stops part-way | The connection dropped. | Reconnect the cable and start again from step 2. |

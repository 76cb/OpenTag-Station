# Back up and restore settings

You can download the station's settings as a file and load them back later, for example after a
factory reset. The file leaves out everything secret and it does not include the scale calibration,
so a restore is a head start, not a complete copy.

Both actions are in the browser only.

## What the file contains

| In the file | Not in the file |
|---|---|
| Hostname and screen brightness, dim and sleep times | Wi-Fi password |
| Wi-Fi network name | Spoolman and FilaBridge tokens and certificates |
| Spoolman address and the names of the two extra fields | The access token |
| FilaBridge address and the selected printer | **The scale calibration** |
| Load-cell profile (model, capacity, overload limit) | The station's own list of which tag belongs to which spool |
| Toolhead profiles | |
| Weight-update settings (automatic saving, tolerances) | |

The file only notes *whether* a password, a token or a calibration exists, not their values.

The links between tags and spools are not lost with the station: they are stored on the spools in
Spoolman, and the station finds them there again.

## Download the settings

1. Open the station's web page and go to **Settings** → **Advanced**.
2. Find **Redacted configuration transfer**.
3. Click **Download redacted JSON**.

Your browser saves `opentag-station-redacted.json`.

## Restore the settings

1. If the station was reset, first get it [back onto your Wi-Fi](../getting-started/first-boot.md).
2. Go to **Settings** → **Advanced** → **Redacted configuration transfer**.
3. Click **Choose JSON to import** and pick the file.
4. Confirm "Validate and apply this redacted configuration? Existing hidden credentials will be
   preserved."

Passwords and tokens already saved on the station stay as they are. The Wi-Fi network name from the
file is applied, so restore a file that names the network the station should be on.

## Re-enter afterwards

After a factory reset or USB install, the restore does not bring these back:

- **Wi-Fi password** — entered during [first boot](../getting-started/first-boot.md).
- **Spoolman and FilaBridge tokens**, if you use them — **Settings** → **Integrations**.
- **Access token** — see [Access token and network safety](security.md).
- **Scale calibration** — see [Tare and calibrate](../scale/calibration.md).

!!! warning
    Restoring a file with a different load-cell profile (5 kg instead of 2 kg, or the reverse)
    deletes the current scale calibration.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "Configuration import must be between 1 byte and 16 KiB." | The file is empty or too large to be a settings file. | Choose the file the station produced. |
| An error about the contents of the file | The file is damaged, was edited by hand, or is not a settings file. | Use an unedited download. |
| "Configuration must be ready before export." / "… before import." | The settings have not finished loading in the browser. | Reload the page and try again. |
| The scale asks for calibration after a restore | The calibration is never part of the file, or the load-cell profile changed. | [Calibrate the scale](../scale/calibration.md). |

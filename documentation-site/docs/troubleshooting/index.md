# Find your problem

Look for what you see in the left column and open the page on the right.

| What you see | Go to |
|---|---|
| A tag is on the reader but the screen still says `PLACE A TAG` | [Tag is not detected](tag.md) |
| In the **NFC READER** setup step, `NFC reader starting...` does not go away | [Tag is not detected](tag.md) |
| In the **NFC READER** setup step, `NFC reader problem. Check the NFC wiring.` | [Tag is not detected](tag.md), then [Wiring](../hardware/wiring.md) |
| `This tag can't be used as it is` | [Which tags work](../openprinttag/supported-tags.md) |
| **Needs attention** after trying to write or clear a tag | [Tag write or clear fails](write.md) |
| **Finish writing this tag**, **Finish clearing this tag**, **Link not saved yet**, **Unlink not finished** | [Finish an interrupted tag operation](journal.md) |
| `Another tag (…) has an unfinished write or clear` | [Finish an interrupted tag operation](journal.md) |
| `Spoolman is missing the Spool extra field…` (Spoolman older than 0.27) | [Required extra fields](../inventory/custom-fields.md) |
| A message that names Spoolman's tag list, a filament number, or a spool "which was not part of this change" | [Spoolman problems](spoolman.md#tag-links-in-spoolmans-tag-list-spoolman-027-or-later) |
| Spoolman is offline, or a spool does not show up in search | [Spoolman problems](spoolman.md) |
| `Saving weights is turned off…` | [Spoolman problems](spoolman.md#saving-weights-is-turned-off) |
| The weight is wrong, jumps, or does not settle | [Weight looks wrong](weight.md) |
| A weight cannot be saved to Spoolman | [Weight looks wrong](weight.md#the-weight-cannot-be-saved) |
| The toolhead buttons are greyed out, or assigning fails | [Printer assignment fails](assignment.md) |
| You cannot open the station's web page, or Wi-Fi does not connect | [Wi-Fi and reaching the station](wifi.md) |
| A firmware update fails or `Update was not kept` | [Update the firmware](../installation/ota.md) |
| You forgot the access token | [Access token and network safety](../configuration/security.md) |
| The station does not start, or you want to start over | [Factory reset and USB recovery](recovery.md) |
| The scale or reader does not work right after building | [Wiring](../hardware/wiring.md) |

## Station check report

When you report a problem, a station check report helps. It only reads from the station: it never
writes a tag, changes a setting or saves a weight.

1. Install [Python 3](https://www.python.org/downloads/) on a computer on the same network as the
   station, if it is not there already.
2. Download [`station_check.py`](https://github.com/76cb/OpenTag-Station/blob/main/tools/station_check.py)
   (on that page: **Download raw file**).
3. Close the station's web page in your browser, then run in a terminal, in the download folder:

    ```sh
    python station_check.py
    ```

    If the station is not found at `opentag-station.local`, add `--host` and the IP address from
    the touchscreen **Settings** page, for example `--host 192.168.1.50`. If you set an access
    token, add `--token` and the token.

The check prints the station's status, whether changes need the access token, whether browsing the
spool list many times in a row works, and whether live updates connect. It saves the same text as
`station-check.txt` next to the script.

# FAQ

Short answers to common questions. Each one links to the page with the details.

## Tags

**Which tags can I use?**
Blank NXP ICODE SLIX2 tags (NFC-V). Nothing else can be written.
See [Which tags work](../openprinttag/supported-tags.md).

**Why doesn't the tag I wrote with my phone work?**
The station only writes to a tag that is completely blank or already holds OpenPrintTag data.
A tag with a web link or other phone-written data shows `This tag can't be used as it is` and
`Use a blank NXP ICODE SLIX2 tag.` The station cannot erase it for you.

**Why does the station ignore my NTAG or MIFARE tag?**
The reader only talks to NFC-V tags. Other types are not seen at all, so the touchscreen keeps
showing `PLACE A TAG`.

**In the NFC READER setup step the reader says it is starting and never gets ready. Why?**
The reader starts only after the station has joined your Wi-Fi and the setup network has closed.
Finish Wi-Fi setup and wait about half a minute.
See [Tag is not detected](../troubleshooting/tag.md).

**What happens if the power fails while a tag is being written?**
Put the same tag back on the reader. The station offers to finish the write or the clear.
See [Finish an interrupted tag operation](../troubleshooting/journal.md).

**Does Clear / Reuse delete my spool in Spoolman?**
No. It erases the tag and removes the tag's link from the spool. The spool and its weights stay.
See [Clear and reuse a tag](../daily-use/clear-reuse.md).

## Weighing

**Does weighing change Spoolman?**
Not by itself. **Weigh** only measures. The weight is saved when you choose **Update Spoolman**.
You can turn on saving after every Weigh in Settings; it is off by default.
See [Weigh a spool and save the weight](../daily-use/weigh.md).

**Does leaving a spool on the scale keep updating its weight?**
No. Only a weight you asked for with **Weigh** can be saved, and each measurement is saved at
most once. Tare and calibration never change Spoolman.

**Does saving a weight rewrite the tag?**
No. The tag is written only when you write or update it yourself.

## Spoolman, FilaBridge and printers

**Can I use the station without Spoolman?**
No. Tags are written from Spoolman spools and weights are saved to Spoolman.
Without it the station can only read a tag and show the scale reading.

**Which Spoolman version do I need?**
Saving weights is enabled for Spoolman 0.26.1 only.
See [Supported versions](upstream.md).

**Can I use the station without FilaBridge?**
Yes. FilaBridge is only needed to assign a spool to a printer toolhead.

**Which printers work?**
Toolhead assignment goes through FilaBridge, and the station shows toolheads T1 to T5.
The manual covers the Prusa XL: [Set up FilaBridge for a Prusa XL](../printer/prusa-xl.md).

**Where is the Community filament catalog?**
It is not part of this version.

## Station

**Can I reach the station from the internet?**
Do not do that. The station uses plain HTTP and is made for your home network.
An access token is optional and does not encrypt anything.
See [Access token and network safety](../configuration/security.md).

**Does it work on 5 GHz Wi-Fi?**
No. The station joins 2.4 GHz networks only.

**Does a firmware update erase my settings?**
An update from the browser keeps Wi-Fi, Spoolman, printer settings and scale calibration.
The USB installer erases them.
See [Update the firmware](../installation/ota.md) and
[Install the firmware over USB](../installation/web-flasher.md).

**Where do I see the firmware version?**
Touchscreen: **Settings → About / Advanced**. Browser: **Settings → Advanced**, card **Device**.

**Is it open source?**
The source is public under the PolyForm Noncommercial License 1.0.0. You may build and use it
for noncommercial purposes. See
[LICENSE.md](https://github.com/76cb/OpenTag-Station/blob/main/LICENSE.md).

**Where are the release files?**
Releases are published on the [GitHub Releases page](https://github.com/76cb/OpenTag-Station/releases).
Whether a release is available yet is stated on the [home page](../index.md).

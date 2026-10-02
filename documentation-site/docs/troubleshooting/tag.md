# Tag is not detected

You put a tag on the reader and the station does not react, or it reports a reader problem.
Go through the table from the top.

## What you see

On **Manage tag**, a reader that is not working looks the same as no tag: `PLACE A TAG`.
Home shows `Place a spool` in both cases.
To see the reader's own status, open touchscreen **Settings → Wi-Fi & services** and tap **Next**
until the step **NFC READER**.
In the browser, **Manage tag** shows the reader's status at the top.

| What you see | What it means | What to do |
|---|---|---|
| NFC READER step: `NFC reader starting...` (browser: `NFC deferred: provisioning`) | The reader has not started yet. It starts only once the station is connected to your Wi-Fi. | See [The reader waits for Wi-Fi](#the-reader-waits-for-wi-fi). |
| NFC READER step: `NFC reader problem. Check the NFC wiring.` (browser: `NFC reader error`) | The reader module does not answer, or reading the tag on it failed. The station tries again every 5 seconds. | Remove the tag. If the message stays, see [The reader does not answer](#the-reader-does-not-answer). |
| `PLACE A TAG` although a tag is on the reader | The reader is not running, or it works but sees no tag: wrong tag type, tag too far away, or metal in between. | Check the NFC READER step first. If it says the reader is ready, see [The reader is ready but sees no tag](#the-reader-is-ready-but-sees-no-tag). |
| `Reading tag...` that never ends on **Manage tag** (NFC READER step: `More than one tag. Use one tag at a time.`; browser: `Multiple NFC-V tags detected`) | Two or more tags are in range. | Remove the others, including tagged spools lying next to the station. |
| `This tag can't be used as it is` | The tag is an NFC-V tag, but it is not blank SLIX2 and holds no OpenPrintTag data the station can read. | Use a blank SLIX2 tag. See [Which tags work](../openprinttag/supported-tags.md). |
| `Reading tag...` for a long time, or the tag appears and disappears | The tag is at the edge of the reader's range, or more than one tag is in range. | Remove other tags. Move the tag closer to the antenna and keep it still. |

## The reader waits for Wi-Fi

The NFC reader stays off until the station is connected to your Wi-Fi.

- Normal start: the reader starts as soon as Wi-Fi is connected.
- During first setup, or whenever the setup network `OpenTag-Setup-XXXX` is open:
  the reader starts about 30 seconds after the station has connected, when the setup network closes.
- Once started, the reader keeps running even if Wi-Fi drops later.

So if the **NFC READER** step keeps showing `NFC reader starting...`, fix the Wi-Fi connection:
[Wi-Fi and reaching the station](wifi.md).
A station that has never been connected to Wi-Fi cannot read tags.

## The reader does not answer

Unplug the station, then check the [wiring](../hardware/wiring.md):

1. The **I2C solder bridge** on the NFC module is closed.
2. Module pin 6 has 5 V and pin 7 is connected to GND.
3. Module pin 5 (SDA) goes to GPIO13, pin 3 (SCL) to GPIO14, pin 1 (IRQ) to GPIO12.
4. Module pins 2 and 4 are not connected to anything.

To test the reader on its own: touchscreen **Settings → Wi-Fi & services**, tap **Next** until the
step **NFC READER**. A working reader shows `NFC reader ready. Place a tag on it to test.`

## The reader is ready but sees no tag

1. **Check the tag type.** Only NFC-V (ISO 15693) tags are seen, and only NXP ICODE SLIX2 can be written.
   NTAG and MIFARE stickers, the common phone tags, are invisible to this reader:
   the screen does not change at all.
2. **Move the tag.** Hold it flat against the antenna on the NFC module, then try the position
   it has on the spool.
3. **Remove metal.** The load cell, screws or a metal plate near the antenna or the tag
   can block reading.
4. **Wait a moment.** The station needs to see the tag for about a second and a half
   before it shows it.
5. **Try another tag** to rule out a damaged one.

If the tag reads on the open bench but not in your enclosure, see
[Scale platform and enclosure](../hardware/mechanical.md#place-the-nfc-reader).

# Parts list

This is everything you need to build one station.
The firmware supports exactly one controller board and one NFC reader module, with fixed pins,
so buy these parts and not look-alikes.

## What to buy

| Part | Qty | Exactly what | Check before buying |
|---|---:|---|---|
| Controller with touchscreen | 1 | **WT32-SC01 Plus** (ESP32-S3, 16 MB flash, 480 × 320 touchscreen) | It must be the **Plus**. The older WT32-SC01 (without "Plus") is a different board and does not work. |
| Scale converter | 1 | **NAU7802** breakout board | See [Choosing a NAU7802 board](#choosing-a-nau7802-board) below. |
| Load cell | 1 | **YZC-133**, **5 kg** (default) or **2 kg** | A four-wire bar-type cell. Get the seller's wiring sheet and mounting drawing. Use one cell, not two. |
| NFC reader | 1 | **ELECHOUSE NFC_ST25R3916B** module (ST25R3916B chip, antenna on the board) | The module must be switched to I2C mode with its solder bridge (see [Wiring](wiring.md)). The pin order in this manual is for this module only. |
| NFC tags | one per spool | **NXP ICODE SLIX2** (NFC-V / ISO 15693), blank | NTAG and MIFARE tags are not seen by the reader at all. Other ISO 15693 tags are seen but cannot be written. See [Which tags work](../openprinttag/supported-tags.md). |
| USB cable | 1 | A data cable that fits the board's USB socket | Charge-only cables cannot install the firmware. |
| 5 V power supply | 1 | A regulated 5 V USB supply | See [Power](power.md). |
| Cable for the expansion connector | 1 | An 8-wire cable that fits the board's EXT connector | Match the connector on your board. |
| Cable for the NFC module | 1 | A 7-wire cable that fits the module's connector | Match the connector on your module. |
| Hook-up wire, pin headers | as needed | For the NAU7802 board and the load cell | Depends on your breakout board. |
| Base, platform, screws, spacers | 1 set | To mount the load cell | You design this yourself. See [Scale platform and enclosure](mechanical.md). |

### Choosing a NAU7802 board

- It has labelled **VIN**, **GND**, **SDA** and **SCL** pins.
- It has four load-cell terminals: **E+**, **E−**, **A+**, **A−**.
- Its documentation says which supply voltage it accepts.
- Its SDA and SCL lines work at 3.3 V. See [Power](power.md).

You also need two tools for setup: a **multimeter** to check the cables before first power,
and an **object of known weight** (no heavier than the load cell's capacity) to calibrate the scale.

## What the project does not recommend

The project has not tested or measured any of these, so it names none:

- **Sellers and prices.**
- **A specific NAU7802 board.** Any breakout that meets the checks above should work.
- **An enclosure.** There is no reference case, no printable model and no drawing with dimensions.
- **A current rating for the power supply.**

## For the curious: how the parts fit the firmware

You do not need this to buy or build. It shows what the firmware expects from each part.

| Part | What the firmware expects |
|---|---|
| WT32-SC01 Plus | ST7796 display and FT6336 touch, both built into the board. The expansion connector carries the scale and the NFC reader. |
| NAU7802 | Address `0x2A`. The station sets it to gain 128, 3.0 V internal regulator, 10 readings per second. |
| YZC-133 | Capacity 5000 g or 2000 g. You choose **5 kg** or **2 kg** in the browser under **Settings → Scale → Edit Load-cell profile → YZC-133 variant**. The default is 5 kg. Changing it deletes the scale calibration. |
| NFC_ST25R3916B | Address `0x50`, I2C mode, with the interrupt (IRQ) wire connected. |
| ICODE SLIX2 tag | 80 blocks of 4 bytes, tag ID starting `E0:04`. |

## Reference documents

These come from the manufacturers, not from this project.
The connector pin numbers in [Wiring](wiring.md) follow the first one.

- [WT32-SC01 Plus datasheet (PDF)](https://docs.makehub.tw/wt32-sc01plus/WT32-SC01%2BPLUS%2BDatasheet-V1.5%2BEN.pdf)
- [ELECHOUSE guide to I2C mode](https://www.elechouse.com/st25r3916-esp32-i2c-quick-start/) —
  use it for the solder bridge only.
  The GPIO numbers in its examples are for a generic ESP32 and do **not** apply to the station.

## Related

- [Wiring](wiring.md)
- [Power](power.md)
- [Scale platform and enclosure](mechanical.md)

# Power

The station runs from one regulated 5 V supply.
This page separates what the firmware fixes from what the part manufacturers state.

## What is certain

- The signal pins of the WT32-SC01 Plus (SDA, SCL, IRQ) work at **3.3 V**. Never put 5 V on them.
- The load cell is powered by the NAU7802 through its E+ and E− terminals.
  The station sets the NAU7802's internal regulator to 3.0 V for this.
  Do not connect the load cell to 5 V.
- All parts share one ground: EXT pin 2.

## What the manufacturers state

These figures come from the manufacturers' documents.
The project has not measured them, nor the current the complete station draws.

| Statement | Source |
|---|---|
| EXT pin 1 supplies 5 V (±5 %). | WT32-SC01 Plus datasheet |
| The board alone draws about 175 mA (170–190 mA) from USB with the backlight at maximum. | WT32-SC01 Plus datasheet |
| The 3.3 V pin on the board's debug connector is a reference, not a power input. | WT32-SC01 Plus datasheet |
| The NFC module is powered from 5 V. | ELECHOUSE guide |

Both documents are linked from the [parts list](bill-of-materials.md#reference-documents).

Wi-Fi, the NFC reader and the scale converter draw current on top of the board's own figure,
and there is no minimum rating for the supply: use one that can deliver more than the board's figure.

## Power your NAU7802 board correctly

Supply voltage limits of NAU7802 breakout boards differ between makers:
read the documentation of the board you bought.

| Your breakout's documentation says | Connect VIN to |
|---|---|
| VIN accepts 5 V, and SDA/SCL are pulled up to 3.3 V | EXT pin 1 (5 V) |
| VIN must be a lower voltage, or SDA/SCL are pulled up to VIN | The regulated voltage that documentation specifies, from a supply that shares ground with the station. Do not use 5 V. |

If the documentation does not say, measure: with the board powered and nothing else connected,
SDA and SCL must sit at about 3.3 V, not 5 V.

## Check the supply on the finished station

1. Power off. With a multimeter, check that 5 V and GND are not connected to each other.
2. Power on through USB. Measure about 5 V between EXT pin 1 and pin 2, and at the NFC module's
   +5V and GND pins.
3. Measure SDA and SCL of the scale and of the NFC reader against GND: about 3.3 V when idle.
   Unplug at once if you read 5 V.
4. Use the station normally for a few minutes: Wi-Fi connected, a tag on the reader.
   The screen must not restart or flicker.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| The station restarts on its own, mostly when Wi-Fi or the reader is active | Most often the supply or the USB cable cannot deliver enough current. | Try another 5 V supply and another USB cable. If it continues, unplug and re-check the [wiring](wiring.md). |
| A part gets hot | A wire is reversed or shorted. | Unplug and re-check the [wiring](wiring.md). |
| The scale or reader comes and goes | Loose power or ground wire. | Re-seat connectors; measure the voltages again. |

# Weight looks wrong

Two different things can go wrong: the scale itself shows a wrong or restless number,
or the number is fine but cannot be saved to Spoolman.

## The scale reading is wrong or does not settle

| What you see | What it means | What to do |
|---|---|---|
| `The weight did not settle. Keep the spool still and weigh again.` | The reading did not stay within 2 g for 1.5 seconds. The station gives up after 20 seconds. | Let the spool rest. Make sure no filament is pulled tight to the printer and nothing touches the platform. |
| `Scale is still moving. Leave the spool still and retry.` | The same. | The same. |
| `Measurement failed or timed out. No inventory update` | The same, or the scale stopped answering. Nothing was saved. | Weigh again. |
| `Scale zero has shifted. Empty the platform, then use Tare in Settings > Calibrate scale and retry.` | The scale reads more than 30 g below zero. | Empty the platform. Touchscreen: **Settings → Calibrate scale → TARE EMPTY PLATFORM**. Browser: **Weigh → Scale setup → Tare**. |
| `Scale overload detected. Remove weight and retry.` | The load is above the cell's capacity plus the overload margin (by default 5500 g for the 5 kg cell, 2200 g for the 2 kg cell). It also appears when the load-cell signal is out of range, for example with a loose or swapped cell wire. | Remove the load. If the platform is empty, check the load-cell wires, check that the right cell size is selected and calibrate again. |
| The Home button reads **CALIBRATE**, or `Calibration required` | The scale has no calibration. This also happens after you change the load-cell size or do a factory reset. | [Tare and calibrate](../scale/calibration.md) |
| `Scale hardware unavailable` | The scale converter does not answer. | Check the scale [wiring](../hardware/wiring.md). |
| `The scale is busy. Try again in a moment.` | Another measurement is running. | Wait and try again. |
| The weight is steady but wrong by the same factor every time | The calibration is off, or the wrong cell size is selected. | Check **Settings → Scale → Edit Load-cell profile** in the browser (5 kg or 2 kg), then calibrate again with a known weight. |
| The weight changes when you touch the case or move a cable | The platform is not free. | [Scale platform and enclosure](../hardware/mechanical.md) |

### Look at the raw numbers

Browser: **Settings → Scale → Scale diagnostics** shows **Raw**, **Filtered**, **Zero** and **Factor**.
Touchscreen: **Settings → About / Advanced**, block **HARDWARE**, line **ADC**.

- Press on the platform: **Raw** must change. If it never changes, a load-cell wire is loose or
  on the wrong terminal.
- With the platform empty and untouched, **Raw** should stay nearly constant.
  If it wanders, look for a cable pulling on the platform or a loose screw.

### Calibration messages

| What you see | What to do |
|---|---|
| `No reference weight detected. Place the reference weight on the platform and retry.` | Put the known weight on the platform after taring, wait until it is stable, then calibrate. |
| `calibration reference produced too few ADC counts` | The scale hardly noticed the weight. Use a heavier known weight; check that the platform rests on the load cell only. |
| `Enter a valid reference weight` | Enter the weight in grams: more than 0 and not more than the cell's capacity. |

## The filament weight does not make sense

The station shows three numbers after weighing. Touchscreen: **Empty spool**, **Filament now**,
**In Spoolman**. Browser: **Empty spool / tare**, **Measured filament**, **Spoolman currently**.

Filament now = scale reading − empty spool weight.
So a wrong filament weight with a correct scale reading means the **empty spool weight** is wrong.
Check it on the spool in Spoolman. How the station picks it:
[Weigh a spool and save the weight](../daily-use/weigh.md).

## The weight cannot be saved

### Nothing can be saved yet

| What you see | What it means | What to do |
|---|---|---|
| `This spool is not linked to Spoolman yet. Link the tag to a spool (Manage tag), then weigh again.` | The station does not know which spool this is. Also shown when no tag is on the reader or Spoolman is offline. | Place the tagged spool; link the tag in **Manage tag**; check [Spoolman](spoolman.md). |
| `The spool on the station changed. Weigh again.` | The tag was lifted or swapped, or settings changed, after the measurement. | Weigh again and keep the spool in place until it is saved. |
| `Spoolman already matches this weight. Nothing to update.` | The difference is within the tolerance (5 g by default). This is not an error. | Nothing. |
| `Saving weights is turned off: …` | Your Spoolman version is not a tested one, or Spoolman is offline. | [Spoolman problems](spoolman.md#saving-weights-is-turned-off) |
| `The weight tolerance setting is invalid. Check Settings.` | The tolerance under **Settings → Scale → Weight updates** is not usable. | Enter a valid number of grams there. |

### Spoolman is missing data

| What you see | What it means | What to do |
|---|---|---|
| `The empty spool weight is unknown. Set it on the spool in Spoolman, then weigh again.` | Neither the spool, the tag, the filament nor the vendor has an empty spool weight. | Set it on the spool in Spoolman. |
| `This spool has no initial weight in Spoolman. Set its initial weight in Spoolman, then weigh again.` | The spool has no initial weight. | Set the initial weight in Spoolman. |
| `Spoolman has no remaining weight for this spool. Set its initial weight in Spoolman, then weigh again.` | The same. | The same. |
| `Spoolman refused the new weight (HTTP 400). Check that the spool has an initial weight in Spoolman.` | The same. | The same. |

### The numbers disagree

| What you see | What it means | What to do |
|---|---|---|
| `The scale reads less than the empty spool weight. Check that the spool is on the scale and its empty weight is right.` | The reading is lower than the empty spool alone should be. | Put the spool on properly; correct the empty spool weight; tare the empty platform. |
| `The measured filament (N g) is more than this spool's initial weight in Spoolman (M g). Check the empty spool weight, or correct the initial weight in Spoolman.` | The spool holds more filament than Spoolman says it started with. Up to 10 g over is accepted as a full spool. | Correct the empty spool weight or the initial weight in Spoolman. |
| `Spoolman usage changed while this weight was being processed. No inventory value was overwritten. Weigh again` | Spoolman's numbers changed while you were weighing, for example because a print used filament. | Weigh again. |

Each measurement can be saved once. To save again, weigh again.

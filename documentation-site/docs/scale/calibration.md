# Tare and calibrate

Before the station can weigh anything, the scale needs two things: a zero point for the empty
platform (tare) and one known weight to learn from (calibration). You do both once after building
the station, and again only when something changes.

## Before you start

- The load cell and platform are mounted and the platform moves freely.
  See [Scale platform and enclosure](../hardware/mechanical.md).
- You have an object whose weight you know exactly, in grams. Anything from 1 g up to the load
  cell's capacity is accepted; a weight close to a full spool (around 1000 g) is a practical choice.
  Weigh it on a kitchen scale if you are not sure.
- The platform is empty: no spool, no reference weight.

## Choose the load-cell size

The station supports two sizes of the YZC-133 load cell: **5 kg** (the default) and **2 kg**.
If you built with the 5 kg cell, skip this section.

1. Browser: **Settings → Scale → Edit Load-cell profile**.
2. Set **YZC-133 variant** to **5 kg** or **2 kg**. The line below shows "Rated capacity: 5000 g"
   or 2000 g.
3. Click **Validate and save**.

!!! warning
    Changing the load-cell size deletes the calibration. Calibrate again afterwards.

There is no load-cell choice on the touchscreen.

**Overload threshold ratio** in the same form sets when the station reports an overload:
1.10 by default, which is 5500 g on the 5 kg cell and 2200 g on the 2 kg cell. Allowed: 1.01 to 2.
It is a warning only and does not protect the load cell from too much weight.

## Calibrate on the touchscreen

1. Tap **Settings**, then **Calibrate scale**.
   On a station that has never been calibrated, the first button on Home reads **CALIBRATE** and
   opens the same panel. During first-run setup it is **CALIBRATE NOW**.
2. With the platform empty, watch the yellow guidance line:
   "1. Waiting for stable empty platform" becomes "2. Ready to tare".
3. Tap **TARE EMPTY PLATFORM**. The line changes to "3. Tare complete - place reference weight".
4. Put the reference weight on the middle of the platform. The line reads
   "4. Waiting for stable reference weight".
5. Check the number in the field under "CALIBRATION". It starts at the weight you used last time,
   or 1000. Tap the field to change it ("Calibration weight") and enter your reference weight in
   grams.
6. When the line reads "5. Ready to calibrate", tap **RUN CALIBRATION**.
   If it reads "Enter the known reference mass" instead, the number is missing or larger than the
   load cell's capacity.
7. Wait a few seconds. When the calibration has worked, the panel closes by itself and the first
   button on Home reads **WEIGH**.

**RUN CALIBRATION** can be tapped only after the tare, with a steady reading and a valid reference
weight. **Close** leaves the panel without calibrating.

## Calibrate in the browser

1. On the Dashboard click **Weigh**, then open **Scale setup** in the dialog.
   **Settings → Scale → Recalibrate scale** opens the same dialog with the calibration panel open.
2. With the platform empty, wait for "Ready to tare." Click **Tare** and confirm
   "Tare the scale now? The platform must be empty and stable." You see "Tare complete."
3. Click **Calibrate**. The panel "Calibrate scale" opens with a checklist:
   Empty platform, Tare, Place reference, Stable signal, Calibrate.
4. Put the reference weight on the platform and type its weight into
   **Known reference weight (g)**.
5. When the status reads "Reference is stable. Ready to calibrate.", click the **Calibrate** button
   at the bottom of the panel and confirm "Calibrate using N g and the saved load-cell profile?".
6. You see "Calibration complete."

The status line walks you through it: "Waiting for stable empty platform.", "Ready to tare.",
"Tare complete — place the reference weight.", "Waiting for stable reference weight.",
"Reference is stable. Enter its known mass.", "Reference is stable. Ready to calibrate."

Check the result under **Settings → Scale**: the **Hardware** card shows **Scale** as "Calibrated".

## Check the result

1. Leave the reference weight on the platform and weigh it like a spool
   (touchscreen **WEIGH**, browser **Weigh**). The gross weight should match the weight you entered.
2. Take it off, put it back and weigh again. The result should repeat.

If the results do not repeat, look at the mechanics before calibrating again.
Weighing a real spool is described in [Weigh a spool and save the weight](../daily-use/weigh.md).

## Tare on its own

Tare sets the reading of the empty platform to zero. It is not the weight of the empty reel; that
comes from Spoolman (see
[Where the empty-spool weight comes from](../daily-use/weigh.md#where-the-empty-spool-weight-comes-from)).

- Touchscreen: **Settings → Calibrate scale → TARE EMPTY PLATFORM**, then **Close**.
  There is no tare button on the normal Weigh page.
- Browser: Dashboard **Weigh → Scale setup → Tare**.

Tare only with an empty platform. The station refuses a tare while the reading is still moving.
You do not need to tare before every weigh: when the empty platform has drifted a little, the
station corrects the zero by itself.

## When to do it again

| Situation | What to do |
|---|---|
| You changed the load-cell size | Calibrate. The old calibration was deleted. |
| After a factory reset or a USB install | Calibrate. Both erase the calibration, and a settings backup does not contain it. |
| You rebuilt the platform, moved the load cell or changed its wiring | Tare and calibrate. |
| A weigh fails with the empty platform reading below zero. The browser says "Scale zero has shifted. Empty the platform, then use Tare in Settings > Calibrate scale and retry."; the touchscreen says "Measurement failed or timed out. No inventory update". | Tare. Calibration is not needed. |
| A known weight no longer reads correctly | Tare first. If it is still off, calibrate. |

A firmware update over Wi-Fi keeps the calibration.

## Tips for stable readings

The station accepts a weight only when it is steady: the reading must stay within 2 g for
1.5 seconds. It waits up to 20 seconds for that.

- Put the station on a solid surface. Do not lean on the table while weighing.
- Let the whole spool rest on the platform. Keep the loose filament end and all cables off it.
- Put the spool in the middle of the platform.
- Wait for `STABLE` (touchscreen) or "✓ Stable" (browser) before you lift the spool.
- Stay below the load cell's capacity. Above the overload threshold the weigh fails: the browser
  answers "Scale overload detected. Remove weight and retry." and the touchscreen shows
  "Measurement failed or timed out. No inventory update".

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| Home button reads **CALIBRATE**, or the Weigh page shows `SETUP REQUIRED` | The scale has no calibration. | Calibrate as described above. |
| The line stays on "1. Waiting for stable empty platform" | The empty platform's reading keeps moving. | Check that nothing touches the platform and that the station stands firmly. |
| "Scale hardware unavailable" / "Scale hardware is unavailable." | The station gets no readings from the scale electronics. | Check the wiring: [Wiring](../hardware/wiring.md). |
| "Enter the known reference mass" or "Enter a reference weight within the selected load-cell capacity." | The reference weight is empty, zero or above the capacity. | Enter the weight in grams, up to the capacity. |
| "No reference weight detected. Place the reference weight on the platform and retry." | The reading did not change after the tare. | Put the reference weight on the platform, then calibrate. |
| After **RUN CALIBRATION** the panel stays open and goes back to an earlier numbered line (touchscreen), or a pop-up gives the reason (browser) | The scale refused the calibration. | Tare the empty platform again, place the weight, wait for a steady reading and retry. |
| "Tare request rejected or the scale queue is full" | Another scale action was still running. | Wait a moment and tap again. |

More: [Weight looks wrong](../troubleshooting/weight.md).
For the exact timings and limits see [Scale behaviour](../reference/scale.md).

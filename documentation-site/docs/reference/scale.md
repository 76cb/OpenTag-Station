# Scale behaviour

How the station turns load-cell readings into a weight: sampling, the stability rule, tare,
calibration and the warnings it raises. The processing values are fixed in the firmware; only the
load-cell profile, the calibration and the Spoolman tolerances are settings.

Steps for tare and calibration: [Tare and calibrate](../scale/calibration.md).

## Hardware

| Item | Value |
|---|---|
| ADC | NAU7802 at I2C address `0x2A`, SDA GPIO10, SCL GPIO11, 400 kHz |
| Sample rate | 10 samples per second |
| Gain | 128 |
| Load-cell excitation (LDO) | 3.0 V |
| I2C operation timeout | 1 second |
| ADC missing or silent | No sample for 1.5 seconds marks the scale disconnected; the station tries to start it again every 5 seconds. |

At start-up the station scans the scale connector once and prints the result on the serial port.
If it finds the ADC with the two lines swapped it prints `NAU7802: FOUND WITH SDA/SCL REVERSED`.
Wiring: [Wiring](../hardware/wiring.md).

## Load-cell profiles

| Profile `id` | Model | Capacity | Overload flagged above |
|---|---|---:|---:|
| `yzc-133-5kg` (default) | YZC-133 | 5,000 g | 5,500 g |
| `yzc-133-2kg` | YZC-133 | 2,000 g | 2,200 g |

The overload threshold is capacity × `scale_profile.overload_ratio` (default 1.10). It is a
warning only and does not protect the cell; never load a cell beyond its rated capacity. Changing
the profile discards the calibration.

## Sampling and stability

| Step | Rule |
|---|---|
| Filter | The last 10 samples are sorted, the 2 lowest and 2 highest are dropped, and the rest are averaged. |
| Weight | `(filtered counts − zero offset) ÷ counts per gram` |
| Stable | The kept samples span at most 2.0 g, the reading is neither negative nor overloaded, and this holds for 1.5 seconds. |
| Weigh result | Taken at the first stable moment and rounded to a whole gram. A steady reading within ±30 g of zero is reported as 0 g. |
| Timeout | A measurement that is not stable after 20 seconds ends with an error. |

Timeout messages:

| Cause | Message |
|---|---|
| Reading never settled | "Scale is still moving. Leave the spool still and retry." |
| Overload | "Scale overload detected. Remove weight and retry." |
| Steady reading below −30 g | "Scale zero has shifted. Empty the platform, then use Tare in Settings > Calibrate scale and retry." |
| Calibration without a weight on the platform | "No reference weight detected. Place the reference weight on the platform and retry." |

Weighing requires a calibration ("scale calibration is required before weighing"). Only one
measurement, tare or calibration runs at a time.

## Tare

Tare stores the current filtered reading as the zero offset.

- The raw reading must be steady: the kept samples of a full 10-sample window span at most 500
  ADC counts. Otherwise: "scale must be stable before tare".
- With a calibration present, the new zero is saved at once and survives a restart.
- Tare is for the empty platform. The weight of an empty spool is handled separately, below.

### Automatic zero tracking

Between measurements, when the platform reading is steady and within ±30 g of the stored zero for
3 seconds, the station moves a temporary zero correction toward the reading in steps of at most
5 g, up to 30 g in total. The correction is not saved; a tare, a calibration or a restart
discards it.

## Calibration

1. Tare the empty platform.
2. Place a known reference weight and start calibration with its weight in grams.
3. The station waits until the reading has been steady for 4 seconds, then computes
   `counts per gram = (filtered counts − zero offset) ÷ reference grams`.

| Rule | Value |
|---|---|
| Reference weight | More than 0 g, at most the profile capacity, at most 5,000 g over the API |
| Smallest accepted change | 100 ADC counts and 5 counts per gram; less is treated as no weight placed |
| Stored values | `zero_offset_counts`, `counts_per_gram`, `reference_grams`, `load_cell_capacity_grams` |
| Where | In the configuration file and as a CRC-protected copy in NVS (key `scaleCal`) |
| Discarded by | Changing the load-cell profile; factory reset |
| Kept by | Restart; firmware update |

## Filament weight

`measured filament = scale reading − empty-spool weight`, computed only from a stable,
non-negative reading.

The empty-spool weight is the first of these that is greater than 0:

1. The spool's own empty weight in Spoolman (`spool_weight`). If the spool has none, Spoolman's
   filament value, then its vendor value, stands in here.
2. The empty-container weight stored on the tag.
3. The filament's `spool_weight` in Spoolman.
4. The vendor's `empty_spool_weight` in Spoolman.

A value of exactly 0 is used only when no source holds a weight greater than 0. With no value at
all the measurement cannot be saved: "The empty spool weight is unknown. Set it on the spool in
Spoolman, then weigh again."

What happens to the measured value, including the 5 g tolerance and automatic saving:
[Spoolman integration](spoolman.md).

## Warnings

| Flag | Condition |
|---|---|
| Negative | Weight below −2.0 g |
| Overload | Weight above capacity × overload ratio, or the filtered ADC value at 98 % or more of its range |
| Drift | A stable reading has moved more than 5.0 g from where it first became stable |
| Disconnected | No sample for 1.5 seconds, or an I2C error |

A negative or overloaded reading is never stable.

## Status words on the touchscreen

| Word | Meaning |
|---|---|
| `ERROR` | The ADC is not responding. |
| `MEASURING` | A measurement started and fewer than 3 samples have arrived. |
| `SETTLING` | A measurement is running and the reading is not yet stable. |
| `STABLE` | The reading is stable, or the last measurement finished with a weight. |
| `ZERO` | The last measurement finished at 0 g. |
| `SETUP REQUIRED` | The scale has no calibration. |
| `READY` | Calibrated, no measurement taken yet. |

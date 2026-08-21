# Scale subsystem

## Phase 11 release status

The default/current physical target remains YZC-133 5 kg and the YZC-133 2 kg
profile remains supported. The release audit found no basis for an accuracy
claim; the required wiring, stability, tare, calibration, repeatability, drift,
position, noise, and overload procedure is in
[release-validation.md](release-validation.md).

## Hardware boundary

The NAU7802 is assigned to the WT32-SC01 Plus external I2C connector at address
`0x2A`, with SDA on GPIO 10 and SCL on GPIO 11. It uses ESP32 `Wire` (I2C
controller 0); the built-in FT6336-compatible touch controller uses `Wire1` (I2C
controller 1) on its separate display bus. Pins live only in the central board
profile and are injected into the driver.

At scale-task startup, firmware performs one bounded scan of valid 7-bit I2C
addresses on GPIO10/11. Only when that bus contains no devices does it scan once
with SDA/SCL reversed. Serial output reports every detected address (up to a
bounded display limit), whether NAU7802 address `0x2A` is present, and a specific
wiring hint when `0x2A` is found only in the reversed orientation. The driver
always restores GPIO10 SDA / GPIO11 SCL before normal initialization and never
adapts production pin assignments from the scan. One concise result is retained
in the bounded runtime log; the scan is not repeated by the five-second normal
disconnected-device retry loop.

The bounded hardware sequence resets the converter, enables its digital and
analog power domains, checks power-ready and revision ID, selects the internal
3.0 V LDO, gain 128, and 10 samples/second, applies the documented ADC/PGA
settings, and runs internal AFE calibration. Every I2C operation has a timeout,
and both startup and calibration waits have deadlines. A missing or disconnected
converter is reported and retried by the scale task without blocking LVGL.

The implementation was cross-checked against the pinned Adafruit NAU7802 1.0.8
register definitions. The station uses its own bounded adapter because the
upstream calibration polling loop does not provide the timeout contract required
by this appliance.

## Load-cell profiles

The actual station design uses the 5 kg variant of the YZC-133 load cell, so new
or uncalibrated configurations default to a `YZC-133` profile with a rated
capacity of 5,000 g and an overload ratio of 1.10. The YZC-133 2 kg variant
remains supported through the same driver and service.

| Profile | Model | Rated capacity | Default software overload flag |
|---|---|---:|---:|
| Default/actual design | YZC-133 | 5,000 g | above 5,500 g |
| Supported alternative | YZC-133 | 2,000 g | above 2,200 g |

The flag threshold is the absolute calibrated weight strictly above rated
capacity multiplied by the configured overload ratio. It is a diagnostic
threshold, not permission to exceed the load cell's rated capacity, a mechanical
protection mechanism, or a measured safe-overload limit.

## Calibration and sampling

`ScaleService` separates raw ADC access from calibration and filtering policy.
The workflow is:

1. collect a complete stable raw-sample window with the platform empty;
2. tare/zero, which records the averaged raw offset;
3. place a known reference weight and wait for another stable window;
4. calibrate with the reference mass and the configured profile's rated
   capacity;
5. persist the signed counts-per-gram factor, zero offset, reference mass,
   capacity, and schema version.

The calibration capacity must match the persistent hardware profile. Changing
the load-cell model or rated capacity therefore invalidates the old calibration
and requires a new tare and reference calibration. Changing only the overload
ratio does not change the weight conversion and does not discard calibration.

A signed factor supports either load-cell orientation. The default processing
profile uses a 10-sample moving mean, a 2 g peak-to-peak stability threshold,
1.5 seconds of continuous stability, and a 1.5 second sample timeout. Those
values are centralized and configurable in `ScaleProcessingConfig`; final
production values require measurements on the assembled station.

The service reports negative readings beyond tolerance, configured load-cell
overload, raw 24-bit ADC saturation risk, disconnects, and stable-baseline
creep. The overload flag uses the absolute calibrated weight and the profile's
rated-capacity-times-ratio threshold; raw ADC saturation remains an independent
fault. Tare and reference calibration refuse incomplete or noisy windows.

## Persistence and rollback

The central storage service owns one `scaleCal` NVS record. It is a fixed-size,
schema-1 record with a magic value and CRC-32; malformed, incompatible, or
corrupt data is rejected rather than used. NVS is outside both OTA application
slots, so compatible firmware updates and rollbacks retain calibration.

Phase 6 migrated this record into the complete versioned configuration document
and added export/import. The calibration remains mirrored to the schema-1 NVS
record for rollback compatibility; no scale code addresses NVS directly.

Calibration saves prevalidate the complete resulting configuration and write
the rollback NVS mirror before committing the authoritative JSON document and
live revision. A model or rated-capacity change clears the legacy calibration
before committing the new central profile, so stale calibration cannot be
re-imported or exposed to rollback firmware. The two stores are safety-ordered
but are not one power-atomic transaction; the central document remains

The versioned configuration stores hardware identity separately in a top-level
`scale_profile` object with `load_cell_model`, `rated_capacity_grams`, and
`overload_ratio` fields. This is an additive extension to configuration schema 3,
so the schema number did not change and older readers can continue to ignore the
unknown object. Calibration remains in the separate `scale` object.

For backward compatibility, a schema-3 document without `scale_profile` infers
the rated capacity from its stored scale calibration. This preserves existing
2 kg calibrations instead of silently treating them as 5 kg. The same inference
is applied when importing the older standalone NVS calibration. A configuration
with neither a profile nor calibration uses the new 5 kg default. Persisted
configuration is accepted only when profile and calibration capacities match.

## Weight policy

Gross scale weight, empty spool/container weight, and net filament remaining are
distinct values. Net is available only from a stable, finite, non-negative
physical result:

```text
net filament remaining = gross measured weight - empty spool/container weight
```

Empty weight resolves in this order: OpenPrintTag, spool-specific Spoolman data,
package/filament default, vendor default, then manual entry. The selected source
is retained with the value.

Reconciliation compares stable physical net weight with valid Spoolman and
OpenPrintTag values using separate normal and warning tolerances. Differences
above the warning threshold require explicit confirmation. Supported decisions
are update Spoolman, update OpenPrintTag, update both, or ignore; actual backend
writes arrive in their adapter phases and must verify after writing.

## Verification status

- Implemented: NAU7802 adapter, task isolation/retry, filtering, stability,
  tare, calibration, persistent 5 kg/2 kg hardware profiles, faults,
  diagnostics, weight-source priority, and three-band reconciliation policy.
- Compiled: complete WT32-SC01 Plus firmware with the pinned ESP32/Arduino
  toolchain.
- Unit-tested: calibration math, both load-cell orientations, 5 kg defaults,
  retained 2 kg profiles, profile persistence and mismatch rejection,
  moving/noisy samples, stable-duration gating, configured overload ratios,
  timeout recovery, store/ADC failures, creep, source priority, and
  reconciliation thresholds.
- Hardware-pending: the actual 5 kg cell has not been physically validated.
  Converter detection, raw values, tare, multiple known weights, accuracy,
  repeatability, final stability settings, overload behavior, power-cycle
  persistence, and interference testing remain to be performed on the assembled
  station.

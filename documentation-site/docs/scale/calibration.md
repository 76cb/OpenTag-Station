# Tare and reference calibration

Convert raw ADC counts to grams for the actual mounted load cell and platform.

## Before you start

Verified NAU7802 wiring, freely moving platform, known reference mass and selected matching 5 kg or 2 kg profile. Stay within rated capacity.

## Steps

1. Open **Settings → Scale** and confirm the ADC is ready and the profile matches the physical cell.
2. Remove the spool and reference mass. Leave the normal empty platform assembled and wait for a complete stable raw-sample window.
3. Choose **Tare**. Confirm the zero operation succeeds; do not continue from a noisy or incomplete sample window.
4. Place the known reference mass centrally, wait for stability, enter its mass in grams and request calibration.
5. Wait for persisted success. Remove and replace the reference to check zero return and repeatability.
6. Back up configuration and record reference mass, mounting arrangement and observed repeatability. Recalibrate after changing profile, wiring or mechanics.

## Expected result

The scale reports calibrated/tare-ready with a signed factor for the selected capacity. Known mass reads consistently and unloaded zero returns within the chosen acceptance tolerance.

## If it fails

Profile/capacity mismatch invalidates old calibration. Noise, stale samples, overload or saturation refuse a trustworthy result. A negative factor can represent cell orientation; it does not justify swapped power wiring. No accuracy specification is claimed until measured on the completed assembly.

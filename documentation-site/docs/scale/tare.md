# Tare during daily use

Set the unloaded platform baseline without confusing it with an empty spool’s mass.

## Before you start

Stable assembled platform and configured scale. The platform tare/zero is distinct from Spoolman’s empty-spool weight, which is subtracted later from gross spool mass.

## Steps

1. Remove every spool and loose object while retaining the platform hardware used during calibration.
2. Wait for a stable sample window and inspect for binding or cable tension.
3. Choose **Tare** in scale controls and wait for completion.
4. Place a known mass to verify calibration still makes sense, then remove it and check zero return.
5. Start a fresh explicit Weigh for the next spool. Tare itself is not an inventory-update event.

## Expected result

The empty platform reads near zero and subsequent measurements are relative to that baseline. No Spoolman mutation occurs because of tare.

## If it fails

Do not tare with a spool present to compensate for an incorrect empty-spool field. If zero drifts immediately, inspect mechanical contact, temperature settling and cable force. Recalibrate when the hardware profile or physical assembly changes.

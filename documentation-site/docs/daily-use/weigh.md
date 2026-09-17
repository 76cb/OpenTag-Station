# Weigh a spool

Measure gross mass and compare usable filament with Spoolman without changing inventory implicitly.

## Before you start

A calibrated stable scale, correct load-cell profile, resolved spool and known empty-spool tare. Keep the entire spool supported only by the platform.

## Steps

1. Select **Weigh** on Home or the WT32 Weigh view. This explicitly starts a measurement session.
2. Wait for stable completion; do not hold the spool or let its filament/cable pull against another surface.
3. Read gross mass, empty-spool weight, measured filament and Spoolman remaining weight. The calculation is gross minus empty-spool tare.
4. Check the active **Auto-update Spoolman after Weigh** policy. It is off by default. If enabled, an eligible explicit measurement can request an update.
5. With auto-update off, use **Update Spoolman** only after reviewing the receipt. For a changed setup or a failed/conflicted update, start a new explicit Weigh.

## Expected result

A completed receipt shows a stable measurement and its difference from canonical inventory. Identification-triggered measurements, tare, calibration and ordinary refreshes do not update Spoolman.

## If it fails

Unknown tare, unstable samples, negative net mass, stale identity or an offline backend prevent a safe update. Correct the cause and measure again. The 5 g default normal tolerance is a no-write deadband; a small difference can correctly produce no PATCH.

![Weigh receipt](../assets/images/browser/weigh.png)

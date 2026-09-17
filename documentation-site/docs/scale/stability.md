# Stability, accuracy and auto-update

The scale driver samples at 10 samples/second with gain 128. The default service
uses a 10-sample moving mean, 2 g peak-to-peak stability threshold and 1.5 seconds
continuous stability, with a 1.5-second stale-sample timeout. These processing
values are not an accuracy certification of the mechanical assembly.

To assess a build, tare an empty stable platform, calibrate with a known mass,
then repeat several loads and unloads. Test centered and off-center placement,
record zero drift over time, and repeat with Wi-Fi/NFC/display active. Record
actual error and spread rather than claiming a resolution from ADC bit count.

**Auto-update Spoolman after Weigh** is off by default. Enable it deliberately in
Settings → Scale only after verifying tare, identity and calibration. It applies
to completed explicit Weigh sessions, not routine tag identification, refresh,
tare, calibration or repeated stable samples. Unknown tare and ambiguous/offline
spools are ineligible. Each session is attempted at most once; failure/conflict
needs a fresh explicit Weigh.

The default 5 g normal reconciliation threshold is a no-write deadband. Review
the warning tolerance and displayed difference before trusting automatic updates.
Software overload flags and ADC saturation faults are separate; neither protects
the load cell mechanically. See [mechanical setup](../hardware/mechanical.md),
[calibration](calibration.md) and [weight troubleshooting](../troubleshooting/weight.md).

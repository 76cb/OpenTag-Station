# Power and logic levels

Use a regulated nominal **5 V** source and common ground. The WT32 EXT connector
specifies 5 V ±5%; design the assembled station to stay within that tighter range
at the connector under load. The board datasheet lists a wider operating range,
but that is not an endorsement for peripheral operation at its extremes.

The manufacturer lists about 175 mA typical (170–190 mA) for the display board
powered from USB with maximum backlight. This is **not total station current**:
NFC RF activity, Wi-Fi and the scale breakout add load. No complete-station peak
current or validated supply-headroom figure has been measured for this release.
Choose a regulated supply with documented spare capacity and verify voltage while
Wi-Fi, backlight and NFC are active; record current during physical acceptance.
[Source: WT32 datasheet, electrical parameters](https://docs.makehub.tw/wt32-sc01plus/WT32-SC01%2BPLUS%2BDatasheet-V1.5%2BEN.pdf).

## Distribution

| Rail | Destination | Constraint |
|---|---|---|
| 5 V | WT32 supply and ELECHOUSE module +5V | Correct polarity; do not inject it into GPIO |
| Breakout VIN | NAU7802 board | Use 5 V only if that exact breakout permits it; otherwise supply its specified regulated voltage |
| 3.3 V logic | SDA, SCL, IRQ | Breakout pull-ups/level shifting must keep WT32 GPIO at 3.3 V |
| E+/E− | Load-cell bridge | Comes from ADC excitation terminals, not raw station supply |
| Ground | Controller, reader, ADC, external regulator | One electrical reference for every signal |

The WT32 debug connector's 3.3 V pin is documented as a reference, **not a power
input**. Do not assume it has spare regulator capacity for an unspecified breakout.
Never tie two independently powered outputs together or back-feed a host USB port.
Use a single supply path unless your actual board's power-path circuit and external
supply arrangement explicitly support the combination.

## Verify the assembled supply

Prerequisites: finished wiring inspected, no spool on the platform, multimeter,
known supply polarity and the normal production firmware.

1. With power off, verify common ground and absence of a short from 5 V to ground.
2. Power the controller through the intended single path. Measure voltage at EXT,
   the NFC power pins and the scale supply terminals.
3. Confirm idle I²C pull-ups remain at 3.3 V logic levels, including on the selected
   scale breakout. Stop if a GPIO line is pulled to 5 V.
4. Enable normal display/Wi-Fi/NFC operation and repeat the voltage measurement.
5. Check for resets, reader errors and ADC disconnects during operation.

Success means supply stays within each component's documented range with stable
normal operation. Brownouts, heat or repeated resets require power-off inspection;
firmware retries cannot repair a reversed connector or an undersized supply path.

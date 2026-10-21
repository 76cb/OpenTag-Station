# Display

You can change the brightness and turn the screen upside down. The screen dims and switches off
by itself when nobody touches it.

## Change the brightness

You can change it in either place. Both change the same saved setting, which the station keeps
after a restart.

**Touchscreen**

1. Tap **Settings**.
2. Move the **Display brightness** slider (5 to 100 %).

The screen changes while you move the slider. The new brightness is saved when you lift your
finger.

**Browser**

1. Go to **Settings** → **Display**.
2. Set **Brightness (%)** between 5 and 100. The default is 80.
3. Click **Validate and save**.

The touchscreen changes to the new brightness straight away; you do not need to restart.

## Turn the screen upside down

Use this when the board sits the other way round in your case. The whole touchscreen, including
touch, turns by 180°; nothing else changes.

1. In the browser, go to **Settings** → **Display**.
2. Set **Screen orientation** to **Upside down (turned 180°)**. **Normal** is the default.
3. Click **Validate and save**.

The touchscreen turns straight away and keeps the orientation after a restart. There is no control
for this on the touchscreen itself. The setting key is `device.display_rotation`, `0` or `180`; see
[Configuration keys](../reference/configuration.md).

## Dimming and sleep

- After 2 minutes without a touch the screen dims to 20 %.
- After 5 minutes without a touch the screen switches off.
- Touch the screen to wake it. The first touch only wakes the screen; it does not press a button.
- To switch the screen off right away: **Settings** → **About / Advanced** → **Sleep**.

There is no control for the two times in either interface. They can only be changed through the
settings keys `device.dim_after_ms` and `device.sleep_after_ms`; see
[Configuration keys](../reference/configuration.md).

## The About screen

**Settings** → **About / Advanced** opens "About this station". It shows the firmware version, the
Wi-Fi state and IP address, and the state of the hardware. It has the buttons **Home**, **Setup**
and **Sleep**. It has no brightness slider; use the one on **Settings**.

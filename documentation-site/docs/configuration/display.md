# Display

The only display setting you can change is the brightness. The screen dims and switches off by
itself when nobody touches it.

## Change the brightness

**Browser** — this is the saved setting:

1. Go to **Settings** → **Display**.
2. Set **Brightness (%)** between 5 and 100. The default is 80.
3. Click **Validate and save**.

The saved brightness is used the next time the station starts.

**Touchscreen** — for a quick change:

1. Tap **Settings**.
2. Move the **Display brightness** slider (5 to 100 %).

The screen changes at once, but the slider does not save. After a restart the station uses the
brightness saved in the browser again.

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
Wi-Fi state and IP address, and the state of the hardware. It also has a second **Brightness**
slider and the buttons **Home**, **Setup** and **Sleep**.

# First boot and Wi-Fi

After the USB install the station starts a setup guide on the touchscreen and opens its own
temporary Wi-Fi network. This page gets the station onto your home network.

## Before you start

- The firmware is [installed](../installation/web-flasher.md) and the hardware is
  [wired](../hardware/wiring.md).
- You know the name and password of a **2.4 GHz** Wi-Fi network. WPA3-only networks are not
  supported.

## What you see first

The touchscreen shows **First-run setup**, "Step 1 of 8", with the buttons **Back**, **Home** and
**Next** along the bottom.

Tap **Next** once to reach the **WI-FI** step. There the touchscreen shows the station's setup
network:

```text
Wi-Fi: OpenTag-Setup-XXXX
Password: xxxxxxxxxx
Then open 192.168.4.1
```

The setup network is protected with WPA2. Its password is shown only on the touchscreen and is a new
one every time the station starts.

You can now enter your Wi-Fi in either of two ways.

## Option 1: on the touchscreen

1. On the **WI-FI** step, tap **Scan** and choose your network from the list, or type its name
   into **Wi-Fi network name**.
2. Type the password into **Wi-Fi password**.
3. Tap **Save**.
4. Wait until the status line shows the connection state and an address ("IP: …").
5. Tap **Next** and continue through the remaining steps:

| Step | What to do | Details |
|---|---|---|
| **SPOOLMAN** | Enter the **Spoolman URL**, tap **Save** | [Connect Spoolman and FilaBridge](initial-setup.md) |
| **FILABRIDGE** | Enter the **FilaBridge URL**, tap **Save** — or tap **Next** to skip | [Connect Spoolman and FilaBridge](initial-setup.md) |
| **PRINTER** | Choose the printer FilaBridge found, tap **Save** — or skip | [Connect Spoolman and FilaBridge](initial-setup.md) |
| **SCALE CALIBRATION** | Tap **CALIBRATE NOW** | [Tare and calibrate](../scale/calibration.md) |
| **NFC READER** | Place a tag on the reader to check that it works | See the note below |
| **READY** | Optionally type an access token, tap **Save**, then **Finish** | [Access token and network safety](../configuration/security.md) |

**Next** moves on even when a step is not filled in, and **Home** leaves the guide at any time.
You can come back later: **Settings** → **Wi-Fi & services**.

!!! note "The tag reader waits for Wi-Fi"
    The tag reader starts once the station has joined your Wi-Fi. Right after first setup it also
    waits until the setup network has closed, about 30 seconds. Until then the **NFC READER** step shows
    "NFC reader starting...". When it is ready it shows "NFC reader ready. Place a tag on it to
    test."

## Option 2: from a phone or computer

1. On your phone or computer, join the Wi-Fi network `OpenTag-Setup-XXXX` with the password from
   the touchscreen.
2. Open `http://192.168.4.1/`. The page shows **Connect this station**.
3. Under **1. Choose Wi-Fi**, click **Scan for networks** and pick your network from
   **Nearby networks**, or type it into **Wi-Fi name (SSID)**. Enter the **Wi-Fi password**.
4. Under **2. Name and secure the station**:
    - **Hostname** — leave `opentag-station` unless you run more than one station.
    - **Access token (recommended)** — optional; see
      [Access token and network safety](../configuration/security.md).
5. Click **Save and connect**.

The page then reports "Connected to … at …" with the station's address and name, and how many
seconds remain before the setup network closes. Reconnect your phone or computer to your normal
Wi-Fi.

Connecting this way also ends the guide on the touchscreen. Spoolman, FilaBridge and the scale
calibration are then **not** asked for: do them next in
[Connect Spoolman and FilaBridge](initial-setup.md).

## Find the station afterwards

Open the station's web page from any device on the same network:

- `http://opentag-station.local/` (or the hostname you chose, followed by `.local`), or
- the IP address shown on the touchscreen: **Settings** shows "Connected - " followed by the
  address.

The setup network closes about 30 seconds after the station has connected.

## If the Wi-Fi stops working later

After three failed connection attempts in a row the station starts the setup network again.
Its name and the new password are on the touchscreen **Settings** page ("Setup network … password
…"). Use either option above to enter working Wi-Fi details.

## Change the Wi-Fi later

- Touchscreen: **Settings** → **Wi-Fi & services**. The guide opens at the **WI-FI** step.
- Browser: **Settings** → **Network** → **Edit Wi-Fi**. Enter the **SSID** and a
  **New password**, then click **Validate and save**.

## If something goes wrong

The messages below appear on the **WI-FI** step of the touchscreen and on the setup page.

| What you see | What to do |
|---|---|
| "Wi-Fi password was rejected. Check the password (it is case-sensitive)." | Type the password again. |
| "Wi-Fi network not found. Use a 2.4 GHz network and check the name." | Check the spelling, and that the router offers this network on 2.4 GHz. |
| "Wi-Fi password or security mismatch. Check the password; WPA3-only networks are not supported." | Set the router to WPA2 or WPA2/WPA3 mixed mode, or use another network. |
| Your phone cannot join `OpenTag-Setup-XXXX` | The password changes at every start. Read the current one from the touchscreen. |

More cases: [Wi-Fi and reaching the station](../troubleshooting/wifi.md).

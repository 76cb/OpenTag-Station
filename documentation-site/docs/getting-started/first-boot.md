# First boot and Wi-Fi

Connect a newly flashed station to the local network without editing firmware source.

## Before you start

Finish the [wiring checks](../hardware/wiring.md), power the normal production firmware, and have the intended 2.4 GHz Wi-Fi credentials. Use a physically controlled place for setup.

## Steps

1. Join the station network named `OpenTag-Setup-XXXX` from a phone or computer. The suffix is device-derived; documentation does not publish a real identifier.
2. Open `http://192.168.4.1/` if the captive page does not appear. This is the firmware’s fixed setup address, not an example home-network address.
3. Choose **Scan for networks**, select your network or enter its SSID, and enter the Wi-Fi password and a valid hostname.
4. Optionally set a local API access token. A blank token on a fresh station enables normal trusted-LAN control; an existing token is preserved when a recovery setup field is blank.
5. Select **Save and connect**. Watch connection status and record the assigned address privately. After successful connection, the setup AP remains for a 30-second grace period.
6. Reconnect your client to the permanent network and open the station at its assigned address. Continue with [initial setup](initial-setup.md).

## Expected result

The station reports connected Wi-Fi and its browser interface loads from the permanent network. The same interface handles both provisioning and daily use.

## If it fails

If association fails, the setup AP remains available for corrected credentials. If captive detection fails, use the fixed setup address directly. If three saved-network attempts fail later, recovery setup starts again. The setup AP is unencrypted; avoid entering unrelated service secrets there.

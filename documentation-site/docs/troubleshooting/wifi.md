# Wi-Fi and reaching the station

Use this page when the station does not join your Wi-Fi, or when you cannot open its web page.

## Things to know first

- The station uses **2.4 GHz** Wi-Fi only. WPA3-only networks are not supported.
- When it has no working Wi-Fi it opens its own setup network, `OpenTag-Setup-XXXX`.
  The password is shown **only on the touchscreen** and is a new one every time the station starts.
- The setup network appears when no Wi-Fi is set up, and again after three failed connection
  attempts in a row. It closes about 30 seconds after the station has connected.
- The tag reader starts only once the station is on your Wi-Fi and the setup network has closed.
  Without Wi-Fi it does not read tags; the **NFC READER** setup step shows "NFC reader starting...",
  and **Manage tag** shows `PLACE A TAG` even with a tag on the reader.

## Where to look

| What you need | Where it is |
|---|---|
| Is the station connected, and its IP address | Touchscreen **Settings**: "Connected - " and the address, or "Wi-Fi not connected" |
| Setup network name and password | Touchscreen **Settings**: "Setup network … password …" |
| More detail (network name, signal, gateway) | Touchscreen **Settings** → **About / Advanced** |
| The reason the last attempt failed | Touchscreen **Settings** → **Wi-Fi & services** (the **WI-FI** step), or the setup page |

## The station does not join your Wi-Fi

| What you see | What it means | What to do |
|---|---|---|
| "Wi-Fi password was rejected. Check the password (it is case-sensitive)." | Wrong password. | Enter it again. |
| "Wi-Fi password or security mismatch. Check the password; WPA3-only networks are not supported." | Wrong password, or the network only allows WPA3. | Check the password. Set the router to WPA2 or WPA2/WPA3 mixed mode, or use another network. |
| "Wi-Fi network not found. Use a 2.4 GHz network and check the name." | The station cannot see a network with that name. | Check the spelling. Make sure the router offers this network on 2.4 GHz. |
| "Wi-Fi signal too weak or unstable. Move the station closer to the router." | The signal is poor at the station. | Move the station or the router. |
| "The router refused the connection. Check for MAC filtering, WPA3-only or client limits on this network." | The router does not let the station in. | Allow the station in the router's settings. |
| "Wi-Fi connection timed out" | No answer in time. | Try again; if it repeats, treat it like a weak signal. |

To enter the Wi-Fi details again:

- Touchscreen: **Settings** → **Wi-Fi & services**, then **Scan**, choose the network, type the
  password, **Save**.
- Phone or computer: join the setup network and open `http://192.168.4.1/`, then
  **Scan for networks** and **Save and connect**.

Both are described step by step in [First boot and Wi-Fi](../getting-started/first-boot.md).

## You cannot open the station's web page

| What you see | What it means | What to do |
|---|---|---|
| `http://opentag-station.local/` does not open | Your device cannot look up the name, or you chose another hostname. | Use the IP address from the touchscreen **Settings** page, for example `http://192.168.1.50/`. |
| The IP address does not open either | Your device is on another network, or the address changed. | Connect the device to the same network as the station and read the address from the touchscreen again. |
| The touchscreen shows "Wi-Fi not connected" | The station is offline. | See the table above. After three failed attempts the setup network starts. |
| `http://192.168.4.1/` does not open | Your device is not on the setup network, or the setup network has closed. | Join `OpenTag-Setup-XXXX` first. If it is not in the list, the station is connected to your Wi-Fi: use its normal address. |
| Your device cannot join `OpenTag-Setup-XXXX` | Old password. | Read the current password from the touchscreen; it changes at every start. |
| The page opens but changes ask for a token | An access token is set. | Enter it. If you forgot it, see [Access token and network safety](../configuration/security.md). |

## Open the setup network on purpose

If the station is connected but you want the setup network anyway — for example to move it to
another Wi-Fi from your phone:

1. Browser: **Settings** → **Advanced** → **Device controls** → **Start setup access point**.
2. Confirm "Start the temporary setup access point? Normal Wi-Fi remains active."
3. Read the name and password from the touchscreen **Settings** page.

## Tags are ignored after Wi-Fi trouble

The tag reader does not start until the station has joined your Wi-Fi once since it was switched
on. If the **NFC READER** setup step keeps showing "NFC reader starting...", fix the Wi-Fi first,
then wait about half a minute. See also [Tag is not detected](tag.md).

## For a closer look

Connected to a computer over USB, the station prints one line for every failed attempt on its
serial port (115200 baud), starting with `sta_failure`. It says whether the network was seen in
the scan and how strong the signal was.

## Related

- [First boot and Wi-Fi](../getting-started/first-boot.md)
- [Factory reset and USB recovery](recovery.md)

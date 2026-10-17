# Connect Spoolman

Spoolman holds your filaments and spools. The station looks spools up there, links tags to them and
saves weights to them. This page connects the two and shows how to check that the connection works.

## Before you start

- Spoolman is running and you can open it in a browser from the same network as the station.
- You know its address, including the port, for example `http://192.168.1.50:7912`.
  Use the address you type to open Spoolman itself.
- The station is on your Wi-Fi.

## Steps

### Browser

1. Open the station's web page and go to **Settings → Integrations**.
2. Open **Edit Spoolman**.
3. Type the address into **Base URL**. Start it with `http://` (see [HTTP and HTTPS](#http-and-https)).
4. Only if your Spoolman sits behind something that asks for a token: type it into
   **New token (blank keeps current)**.
5. Click **Validate and save**.
6. On the **Integrations** card, click **Test**.

### Touchscreen

1. Tap **Settings**, then **Wi-Fi & services**. The setup pages open at the Wi-Fi step.
2. Tap **Next** until you reach "SPOOLMAN" ("Enter and save the service base URL.").
3. Type the address into **Spoolman URL**. **Authentication token (optional)** is for the same case
   as above.
4. Tap **Save**. The status reads "URL saved".
5. Tap **Home** to leave the setup pages.

"URL saved" only means the address was stored. The touchscreen has no test button.
To see whether Spoolman answers, open the **Assign** page: its status line shows
"Spoolman ONLINE", "Spoolman OFFLINE" or "Spoolman WAITING".
The full check, including the extra fields, is the browser's **Test** button.

## What the Test button tells you

**Test** asks Spoolman and FilaBridge for their status and shows "Backend connection tests complete."
Then read the **Integrations** card:

| What you see | What it means | What to do |
|---|---|---|
| Spoolman: connected, no warning below | The station can read Spoolman, the extra fields exist and weights can be saved. | Nothing. |
| Spoolman: offline | The station got no valid answer from that address. | Check the address and port, that Spoolman is running, and that you used `http://`. See [Spoolman problems](../troubleshooting/spoolman.md). |
| "Spoolman is missing the Spool extra field …" | One or both required extra fields are missing or have the wrong type. | Create them: [Required extra fields](custom-fields.md). |
| "Spoolman 0.x.y has not been tested with this firmware, so saving weights to Spoolman is turned off." | Spoolman answers, but it is not the supported version. | See the next section. |

## Supported Spoolman version

The station saves weights only to Spoolman **0.26.1**. The version must match exactly.

On any other Spoolman version:

- **Not switched off:** identifying spools, browsing and searching, creating spools, editing
  records, writing, linking and clearing tags. The station does not check the version for these,
  but 0.26.1 is the only version it supports.
- **Stops working:** saving a measured weight. **UPDATE SPOOLMAN** / **Update Spoolman** and
  auto-update answer with "Saving weights is turned off: this Spoolman version has not been tested
  with this firmware, or Spoolman is offline. See Settings."

The versions the firmware was built against are listed in [Supported versions](../reference/upstream.md).

## HTTP and HTTPS

Use an `http://` address on your home network.

An `https://` address fails with "HTTPS requires a configured CA certificate".
The station needs the certificate authority that signed Spoolman's certificate, and neither the
touchscreen nor the browser page has a field for it. It can only be stored through the station's
[REST API](../reference/api.md) (key `spoolman.ca_certificate_pem`, see
[Configuration keys](../reference/configuration.md)).
The greyed example text in the **Base URL** box shows an `https://` address; do not copy it.

If the station reports "DNS resolution failed or is still pending for …", it cannot find Spoolman
by name. Use Spoolman's IP address in the URL instead.

## Tokens

Leave the token empty unless something in front of Spoolman requires one.
If the token does not start with `Bearer` or `Basic`, the station sends it as a `Bearer` token.

A saved token is never shown again. Leaving **New token** blank keeps it.
To remove it, tick **Explicitly clear saved token** and save.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "Spoolman URL is not configured" | No address is saved. | Enter the address as above. |
| "HTTPS requires a configured CA certificate" | The address starts with `https://`. | Use `http://`. |
| "Spoolman authentication was rejected" | Spoolman, or a proxy in front of it, refused the token. | Enter the right token. |
| "Spoolman endpoint or resource was not found" | The address reaches something, but not Spoolman's API. | Use Spoolman's base address without any extra path. |
| "DNS resolution failed or is still pending for …" | The host name cannot be found. | Use the IP address. |
| Home shows "Spoolman unavailable; spool not looked up" | Spoolman did not answer when the tag was read. | Fix the connection, then lift the spool and put it back. |

More: [Spoolman problems](../troubleshooting/spoolman.md).

## Related

- [Required extra fields](custom-fields.md)
- [Find and edit spools and filaments](filaments-spools.md)
- [Spoolman integration reference](../reference/spoolman.md)

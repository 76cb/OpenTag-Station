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
The full check, including the version and the extra fields, is the browser's **Test** button.

## What the Test button tells you

**Test** asks Spoolman and FilaBridge for their status and shows "Backend connection tests complete."
Then read the **Integrations** card:

| What you see | What it means | What to do |
|---|---|---|
| Spoolman: connected, no warning below | The station can read Spoolman, it can link tags and weights can be saved. | Nothing. |
| Spoolman: offline | The station got no valid answer from that address. | Check the address and port, that Spoolman is running, and that you used `http://`. See [Spoolman problems](../troubleshooting/spoolman.md). |
| "Spoolman is missing the Spool extra field …" | Your Spoolman is older than 0.27 and one or both extra fields are missing or have the wrong type. | Create them: [Required extra fields](custom-fields.md), or update Spoolman to 0.27. |
| "Spoolman 0.x.y has not been tested with this firmware, so saving weights to Spoolman is turned off." | Spoolman answers, but it is not a supported version. | See the next section. |

## Supported Spoolman versions

The station saves weights to Spoolman **0.26** and **0.27** (any release whose version starts with
`0.26.` or `0.27.`, for example 0.26.0, 0.26.1 or 0.27.0).

On any other Spoolman version, including test builds such as `0.27.0-dev`:

- **Not switched off:** identifying spools, browsing and searching, creating spools, editing
  records, writing, linking and clearing tags. The station does not check the version for these,
  but 0.26 and 0.27 are the only versions it supports.
- **Stops working:** saving a measured weight. **UPDATE SPOOLMAN** / **Update Spoolman** and
  auto-update answer with "Saving weights is turned off: this Spoolman version has not been tested
  with this firmware, or Spoolman is offline. See Settings."

The version rules are listed in [Supported versions](../reference/upstream.md).

## What Spoolman 0.27 adds

Spoolman 0.27 can link NFC tags to spools itself. When the station sees Spoolman 0.27 or later it
uses this automatically; there is no setting.

- **The extra fields become optional.** The station keeps the tag-to-spool link in Spoolman's tag
  list. If the two [extra fields](custom-fields.md) exist, the station keeps filling them too.
- **Written tags appear in Spoolman.** After writing a tag, the station adds it to the spool's tag
  list. Clearing a tag removes it again. Moving a tag to another spool moves it in the list.
- **Tags you linked in Spoolman are recognised.** A tag that Spoolman links to a spool is shown as
  that spool, even a blank tag or a tag holding other data. See
  [Identify a spool](../daily-use/identify.md).
- **Scans are reported to Spoolman.** Each time the station identifies a tag, it tells Spoolman
  which tag it saw, with the station's host name (`opentag-station` unless you changed it) as the
  reader. If you pair a browser with that reader in Spoolman's **Settings → Tag Scanning**, that
  browser jumps to the spool when you put it on the station. If the report fails, nothing on the
  station changes.

The station only adds and removes tag links on spools. It never removes a tag that Spoolman links
to a filament; it asks you to do that in Spoolman.

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

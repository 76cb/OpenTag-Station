# Access token and network safety

The station is built for a home network you trust. An access token adds a password to everything
that can be changed through the browser. This page explains what the token does, how to set it,
and what it cannot protect.

## What the token protects

With a token set, the browser must supply it before the station accepts any change:
saving settings, writing or clearing tags, weighing, assigning a toolhead, updating the firmware,
restarting, and factory reset.

Without a token, anyone on your network can do all of that from a browser.

## What the token does not protect

- **Looking.** The web page, the current spool and the station's status can be viewed without the
  token.
- **The touchscreen.** It never asks for the token. Anyone standing at the station can use it.
- **The connection itself.** The station speaks plain HTTP. The token and everything else travel
  unencrypted on your network.

## Token rules

- 16 to 128 characters.
- Only letters `A–Z` `a–z`, digits `0–9`, and `-` `.` `_` `~`. No spaces or other symbols.
- An empty token means no protection.

## Set or change the token

| Where | How |
|---|---|
| Setup page (`http://192.168.4.1/`) | Fill in **Access token (recommended)** before **Save and connect**. |
| Browser | **Settings** → **Advanced** → **Edit Local API security** → **New access token** → **Validate and save**. Leaving the field blank keeps the current token. |
| Touchscreen | **Settings** → **Wi-Fi & services** → **Next** until the **READY** step → type the token → **Save** → **Finish**. |

To remove the token: browser **Settings** → **Advanced** → **Edit Local API security** → tick
**Explicitly clear saved access token** → **Validate and save**. The touchscreen can replace a
token but not remove it.

Once a token is set, the browser asks for it the first time you change something:
"Local API authentication is enabled. Enter the configured 16–128 character token; it stays in
memory for this tab only." You enter it again in every new tab.

## If you forget the token

Set a new one on the touchscreen; it does not ask for the old one:

1. Tap **Settings** → **Wi-Fi & services**.
2. Tap **Next** until you reach the **READY** step.
3. Type a new token, tap **Save**, then **Finish**.

## The setup network

The temporary network `OpenTag-Setup-XXXX` is protected with WPA2. Its password is shown only on
the touchscreen and changes every time the station starts, so only someone who can see the station
can join it.

Over the setup network, scanning for Wi-Fi and saving new Wi-Fi details work without the access
token. Nothing else does, and an existing access token cannot be replaced that way.

## Firmware files are not signed

Before installing an update the station checks that the file arrived intact and is OpenTag Station
firmware for this board. It cannot check who made it. Only upload files you downloaded yourself
from the project's [GitHub Releases page](https://github.com/76cb/OpenTag-Station/releases).

## Keep it on your home network

!!! warning
    Do not make the station reachable from the internet: no port forwarding, no public reverse
    proxy. It has no encryption, and without a token it has no protection at all.

- Set a token if other people or devices you do not control share your network.
- The settings file you can download never contains passwords or tokens; see
  [Back up and restore settings](backup.md).

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "The local API token must contain 16–128 characters." | The token you typed is too short or too long. | Type the full token. |
| The token is refused after saving on the setup page | It contains a character that is not allowed. | Use only letters, digits and `-` `.` `_` `~`. |
| The browser keeps asking for the token | The token you typed is not the saved one. | Set a new one on the touchscreen as described above. |

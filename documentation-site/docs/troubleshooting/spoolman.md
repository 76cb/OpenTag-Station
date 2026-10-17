# Spoolman problems

The station needs Spoolman to look up spools, to link tags and to save weights.
The scale and the touchscreen keep working when Spoolman is unreachable.

## Check the connection first

Browser: **Settings → Integrations**, click **Test**. The card shows the state of Spoolman and any
warning below it.
Touchscreen: if FilaBridge is set up and working, the status line of the **Assign** page shows
`Spoolman ONLINE`, `OFFLINE` or `WAITING`.
Otherwise that line shows the FilaBridge message instead; use the browser to check Spoolman.
The touchscreen has no test button; in its setup steps `URL saved` only means the address was stored.

## Spoolman is offline

| What you see | What it means | What to do |
|---|---|---|
| `Spoolman unavailable; spool not looked up` (touchscreen Home) | The station cannot reach Spoolman. | Check that Spoolman is running and the address is right: browser **Settings → Integrations → Edit Spoolman → Base URL**. |
| `Spoolman URL is not configured` | No address is saved. | Enter it. See [Connect Spoolman](../inventory/spoolman.md). |
| `DNS resolution failed or is still pending for <host>` | The station cannot find that host name on your network. | Use Spoolman's IP address in the Base URL instead of a name. |
| `HTTP request requires Wi-Fi` | The station itself is offline. | [Wi-Fi and reaching the station](wifi.md) |
| `Spoolman endpoint or resource was not found` | The address reaches something, but not Spoolman. | Use the address you open Spoolman with in your browser, including the port. |
| `Spoolman returned HTTP <number>` | The same, or Spoolman answered with an error. | The same. |
| `Spoolman authentication was rejected` | Spoolman, or something in front of it that asks for a password, wants a token and the saved one is wrong. | Enter the token under **Edit Spoolman → New token**. |
| `Backend operation deadline exceeded` | Spoolman did not answer in time. | Check that Spoolman responds; try again. |
| `HTTP transport failed: …` | The connection to Spoolman failed or broke off. | The same. |
| `Backend deferred: insufficient internal memory; local controls remain available` | The station is short of memory for the moment. | Try again. If it stays, restart the station. |

Remember that the station connects to Spoolman from its own place on the network.
An address that works on your computer, such as `localhost`, does not work from the station.

## HTTPS addresses

| What you see | What it means | What to do |
|---|---|---|
| `HTTPS requires a configured CA certificate` | The Base URL starts with `https://`, and the station cannot check the server's security certificate. | Use an `http://` address on your home network. |

The settings pages have no field for a certificate.
Developers can store one: see [Configuration keys](../reference/configuration.md).

## Missing extra fields

This message appears only with Spoolman older than 0.27. On 0.27 or later the extra fields are
optional and a missing field is not reported.

| What you see | What to do |
|---|---|
| `Spoolman is missing the Spool extra field 'nfc_uid'. In Spoolman open Settings > Extra Fields > Spool and add it with type Text.` (the message names the field or fields that are missing) | Add both fields, `opentag_instance_uuid` and `nfc_uid`, under **Spool** with type **Text**. Steps: [Required extra fields](../inventory/custom-fields.md). Or update Spoolman to 0.27 or later. |

The message appears in the browser under the Integrations card after **Test**,
and on the touchscreen as **Needs attention** when you try to write, link or clear a tag.
A field created under **Filament** or **Vendor**, or with another type, does not count.

## Saving weights is turned off

| What you see | What it means | What to do |
|---|---|---|
| `Spoolman <version> has not been tested with this firmware, so saving weights to Spoolman is turned off.` | Your Spoolman version is not 0.26 or 0.27. Test builds such as `0.27.0-dev` count as untested too. | Run a Spoolman 0.26 or 0.27 release. See [Supported versions](../reference/upstream.md). |
| `Saving weights is turned off: this Spoolman version has not been tested with this firmware, or Spoolman is offline. See Settings.` | The same, shown after you tap **UPDATE SPOOLMAN**. Also appears when Spoolman is offline. | Click **Test** under **Settings → Integrations** to see which it is. |

With an untested version the station still reads your spools, identifies tags and writes tags.
Only saving a weighed result to Spoolman is switched off.

## A spool or filament does not show up in search

Search is simple, and that explains most "missing" spools:

- The text you type is compared with the **filament name**.
  Only if that finds nothing is it compared with the **vendor name**.
- It does not look at material, colour, spool number, location, lot or comment.
- Type one thing, not two. `Prusament` finds the vendor; `Galaxy Black` finds the filament;
  `Prusament Galaxy Black` finds nothing, although the list shows each row as vendor followed by name.
- **Archived spools are not listed:** the search does not ask Spoolman for them.
- A filament without any spool appears under **MY FILAMENTS** (browser: **Filaments** tab),
  not under **MY SPOOLS**.

| What you see | What to do |
|---|---|
| `No matches for "<text>".` | Search for a shorter part of the filament name, or for the vendor name alone. |
| `No spools in Spoolman yet.` or `No filaments in Spoolman yet.` | Spoolman has no such records, or all spools are archived. |
| `End of the list.` | You paged past the last entry. Tap **PREV**. |
| `This page is too large to show; narrow the search.` | Search for something more specific. |

In the browser you can also filter by material and click a vendor or filament to narrow the list.
The list is sorted by Spoolman ID, oldest first, so new spools are on the last page.

## The station cannot tell which spool a tag belongs to

| What you see | What it means | What to do |
|---|---|---|
| `Multiple NFC UID owners; clean up Spoolman before continuing` | Two or more spools carry this tag's `nfc_uid`. | In Spoolman, empty both fields, `nfc_uid` and `opentag_instance_uuid`, on the spool that should not have the tag. |
| `Instance UUID belongs to another Spoolman spool` | The tag's unique ID (`opentag_instance_uuid`) is already saved on a different spool than the one you chose. | The same. |
| `Too many matching spools; configure an exact OpenPrintTag UUID or NFC UID` | The tag is not linked to any spool, and too many spools match its brand and material for the station to suggest one. | Link the tag yourself: **Manage tag → LINK TO A SPOOL**. |

## Tag links in Spoolman's tag list (Spoolman 0.27 or later)

Spoolman 0.27 can link a tag to a spool or to a filament itself, and you can change those links in
Spoolman. The station checks them before and after every write and clear, and stops when they do
not fit. The station only changes links on spools; a link on a filament is always yours to remove.

Before writing (nothing has been written to the tag):

| What you see | What it means | What to do |
|---|---|---|
| `Spoolman links this tag to filament #N. Remove it from that filament in Spoolman first.` | In Spoolman the tag belongs to a filament, not a spool. | In Spoolman, remove the tag from filament #N, then start the write again. |
| `Spoolman links this tag to two different spools; remove one of the links in Spoolman first` | Two spools hold the tag (one with its UID in the usual byte order, one reversed, as some phones and readers report it). | In Spoolman, remove the tag from the spool it is not on. |
| `This tag is linked to spool #A by its extra field and to spool #B in Spoolman's tag list. Remove one of the links in Spoolman first.` | The spool's `nfc_uid` field and Spoolman's tag list disagree. | In Spoolman, empty the field on the wrong spool, or remove the tag from the wrong spool's tag list. |

After writing, while saving the link (the tag is already written; the station shows
`Link not saved yet`, see [Finish an interrupted tag operation](journal.md)):

| What you see | What it means | What to do |
|---|---|---|
| `Spoolman now links this tag to spool #N, which was not part of this change. Check it in Spoolman, then try again.` | Someone linked the tag to another spool in Spoolman while the station was writing. | Remove that link in Spoolman if it is wrong, then **TRY SAVING LINK AGAIN** (browser: **Retry Spoolman link**). |
| `Spoolman links this tag to filament #N. Remove it from that filament in Spoolman, then try again.` | Spoolman refused the link because a filament holds the tag. | Remove it from the filament in Spoolman, then try again. |
| `Spoolman already links this tag to spool #N. Remove it from that spool in Spoolman, then try again.` | Spoolman refused the link because another spool holds the tag. | Remove it from spool #N in Spoolman, then try again. |
| `Spool #N no longer exists in Spoolman` | The spool was deleted in Spoolman. | Skip linking, then write the tag again for an existing spool. |
| `Spoolman did not keep the tag link; try again` | Spoolman accepted the link but did not show it afterwards. | Try again. |

After clearing (the tag is already blank; the station shows `Unlink not finished`):

| What you see | What it means | What to do |
|---|---|---|
| `Spoolman now links this tag to another spool; unlink refused` | The tag is linked to a different spool than the one being unlinked. | Check the tag's links in Spoolman and remove the wrong one, then **TRY UNLINKING AGAIN** (browser: **Retry Spoolman cleanup**). |
| `This tag is linked to one spool by its extra field and to another in Spoolman's tag list; unlink refused` | The `nfc_uid` field and the tag list name different spools. | The same. |
| `Spoolman still links this tag after removing it; try again` | Spoolman did not keep the removal. | Try again. |

## Related

- [Connect Spoolman](../inventory/spoolman.md)
- [Find and edit spools and filaments](../inventory/filaments-spools.md)

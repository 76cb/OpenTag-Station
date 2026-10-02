# Required extra fields

The station remembers which tag belongs to which spool by storing two values on the spool in
Spoolman. Spoolman needs two extra fields to hold them. You create them once, before you write your
first tag. The station never creates them for you.

## The two fields

| Key | Belongs to (Spoolman calls this the entity) | Type | What the station stores in it |
|---|---|---|---|
| `nfc_uid` | Spool | Text | The permanent UID of the tag that is on this spool. |
| `opentag_instance_uuid` | Spool | Text | The unique ID the station wrote onto that tag. |

Together they identify one physical spool, even when you own several spools of the same filament.

## Steps

1. Open Spoolman in a browser.
2. Open Spoolman's settings and go to the extra fields for **Spool**
   (the station's own message calls the place "Settings > Extra Fields > Spool").
   Spoolman keeps separate extra fields for spools, filaments and vendors (it calls these
   entities). These two must be **Spool** fields. Fields created for Filament or Vendor are not
   seen by the station.
3. Add a field with the key `nfc_uid` and the type **Text**. The display name is up to you,
   for example "NFC tag".
4. Add a second field with the key `opentag_instance_uuid` and the type **Text**.
   The display name is again up to you, for example "OpenTag ID".
5. Check the result from the station's web page: **Settings → Integrations → Test**.
   No warning under the Spoolman line means both fields were found.

The keys must match exactly: lower case, with underscores.
Leave the fields empty on your spools. The station fills them in when it links a tag and empties
them when you clear the tag.

## What happens when they are missing

The station refuses to write or link a tag until both fields exist, and tells you which one is
missing:

> Spoolman is missing the Spool extra field 'nfc_uid'. In Spoolman open Settings > Extra Fields >
> Spool and add it with type Text.

When both are missing the message names both:

> Spoolman is missing the Spool extra fields 'opentag_instance_uuid', 'nfc_uid'. In Spoolman open
> Settings > Extra Fields > Spool and add them with type Text.

Where you see it:

- Browser: under the Spoolman line on the **Integrations** card in **Settings**, after **Test**.
- Touchscreen: on the "Needs attention" page when you try to write or link a tag.
  The touchscreen does not check the fields before that.

A clear is different. The station erases the tag first and checks the fields afterwards, so the tag
ends up blank but its link stays on the spool in Spoolman, and the station stops at
"Unlink not finished". Add the fields, then tap **TRY UNLINKING AGAIN**
(browser: **Retry Spoolman cleanup**). See
[Clear and reuse a tag](../daily-use/clear-reuse.md#if-spoolman-cannot-be-reached-in-the-middle).

The same message appears when a field exists but has a type other than Text, or was created for
Filament or Vendor instead of Spool. Delete the wrong field in Spoolman and create it again as
described above.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "Spoolman is missing the Spool extra field …" although you created the fields | The field was created for Filament or Vendor, has the wrong type, or the key has a typing error. | Compare the keys letter by letter. Check that both are **Spool** fields of type **Text**. Then click **Test** again. |
| "Multiple NFC UID owners; clean up Spoolman before continuing" | Two spools carry the same tag UID in `nfc_uid`. | In Spoolman, empty the field on the spool that does not have the tag. |
| "Instance UUID belongs to another Spoolman spool" | Another spool already carries this tag's ID. | In Spoolman, empty `opentag_instance_uuid` on the spool that does not have the tag. |

## Related

- [Connect Spoolman](spoolman.md)
- [Clear and reuse a tag](../daily-use/clear-reuse.md) — how the link is removed again
- [Configuration keys](../reference/configuration.md) — the station can be told to use other keys
  (`spoolman.identity_field`, `spoolman.nfc_uid_field`); there is no form field for this

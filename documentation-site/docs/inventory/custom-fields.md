# Identity fields and ownership

OpenTag Station remembers which physical spool a tag belongs to by storing two
values on the **spool** in Spoolman. Spoolman needs two extra fields for this.
You create them once, before writing your first tag.

## Create the two Spoolman extra fields

1. Open Spoolman in a browser.
2. Go to **Settings → Extra Fields → Spool**. (Use the **Spool** tab, not
   Filament or Vendor.)
3. Add a field with key `opentag_instance_uuid`, any name you like (for example
   "OpenTag ID"), and type **Text**.
4. Add a second field with key `nfc_uid`, a name such as "NFC tag", and type
   **Text**.
5. On the station's browser page, open **Settings → Integrations** and choose
   **Test**. If a field is missing or has the wrong type, a message under
   Spoolman names the field to fix.

The keys must match exactly (lowercase, underscore). If you changed the keys in
an imported configuration, create fields with those keys instead.

!!! warning "Why this matters"
    Spoolman ignores searches on extra fields that do not exist and returns
    unrelated spools. Without these fields the station would not be able to
    tell spools apart, so it refuses to write, link or look up tags until the
    fields exist, and says which field is missing.

## How the station uses them

The station stores the tag's OpenPrintTag instance UUID and NFC UID in these
fields, plus a bounded local cache of confirmed mappings. Together they identify
one physical spool. A vendor, material or product-barcode match is only a
suggestion: the station asks you to choose or link the spool before it saves a
weight or assigns it to a printer.

Clear / Reuse first verifies the tag is blank, then finds the spool that owns the
tag and sets only those two extra values to empty. It reads Spoolman back to
confirm the change. Usage and other extra fields are left alone. If a different
spool claims the tag, cleanup stops instead of erasing that spool's link.

If one tag appears to belong to several spools, fix the duplicates in Spoolman
and refresh the station. See the [Spoolman technical reference](../reference/spoolman.md)
for encoding and compatibility details.

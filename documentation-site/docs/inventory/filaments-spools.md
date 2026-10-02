# Find and edit spools and filaments

Browse your Spoolman inventory from the station's web page, find a spool or filament, and correct
its details without opening Spoolman. Editing is done in the browser; the touchscreen can pick
spools and create new ones but has no editor.

## Filaments, spools and vendors

Spoolman keeps three kinds of record, and the station uses the same words:

| Record | What it describes | Example |
|---|---|---|
| **Vendor** | The maker. | The company that made the filament. |
| **Filament** | A product: name, material, colour, nominal weight, diameter, density. Shared by all spools of that product. | A black PETG, 1000 g. |
| **Spool** | One physical reel of a filament: its number, initial weight, used weight, empty-spool weight, location. | Spool #28. |

A tag always belongs to a **spool**, never to a filament.
If Spoolman has the filament but not the reel in your hand, create a spool first; the station can do
that for you while writing a tag (see [Write or update a tag](../daily-use/manage-tags.md)).

## Find a spool or filament

1. Open the station's web page and click **Inventory** in the left rail.
2. Choose a tab: **Spools**, **Filaments** or **Vendors**.
3. Narrow the list:
    - **Search**: type part of the filament name, then click **Search**.
      If no filament name matches, the station tries the same text as a vendor name.
    - **Material**: type a material such as `PLA` or `PETG`, then click **Search**.
    - Click a vendor to see only that vendor's filaments. Click a filament to see only its spools.
      The active filters appear as "Vendor: … ×" and "Filament: … ×"; click one to remove it.
    - **Clear** removes the search.
4. Page through the results with **Previous** and **Next**. A page holds eight entries.
   Spool rows show "N g remaining · N g initial".
5. Click a spool to select it.

**Refine this page** offers **Color** and **Status** ("All statuses", "Filament remaining", "Empty").
These only hide rows on the page you are looking at; they do not search the whole inventory.

Search looks at the filament name first and the vendor name second, and at nothing else.
Archived spools are not listed.
[How search works](../daily-use/manage-tags.md#how-search-works) has the details and a checklist for
a filament that does not show up.

On the touchscreen the same lists appear while you write a tag: **MANAGE TAG**, then **ASSIGN TAG**,
**LINK TO A SPOOL** or **REASSIGN**, then **MY SPOOLS** or **MY FILAMENTS**.
A tag must be on the reader. The lists show three rows per screen, with **SEARCH** but no material
filter.

## Edit a spool

1. Select the spool on the **Inventory** page and click **Continue**.
   For the spool that is on the station, **Edit spool** on the Dashboard is a shortcut.
2. On the Review step, click **Edit Spool**. The editor is titled "Edit this physical spool" and says
   "Changes apply only to Spool #N."
3. Change what you need:

    | Field | Meaning |
    |---|---|
    | **Initial filament (g)** | Filament on the reel when it was new. |
    | **Used weight (g)** | Filament used so far. |
    | **Empty spool (g)** | Weight of the empty reel. |
    | **Price (Spoolman currency)** | What you paid. |
    | **Location** | Where the spool is kept. |
    | **Lot / batch** | The maker's lot number. |
    | **Notes** | Free text. |

4. Click **Save Changes to Spoolman**. **Cancel edit** closes the editor without saving.
5. Wait for "✓ Saved and verified in Spoolman. Generate a new tag preview."

## Edit a filament

1. Select a spool of that filament on the **Inventory** page and click **Continue**.
   A filament that has no spool cannot be edited from the station.
2. On the Review step, click **Edit filament**. The editor is titled "Edit shared filament definition" and warns
   "Changes to filament #N affect every Spoolman spool using this filament."
3. Change what you need: **Product name**, **Material**, **Nominal filament weight (g)**,
   **Density (g/cm³)**, **Diameter (mm)**, **Default tare (g)** (the empty-reel weight for this
   product), **Color (6 or 8 hex digits)**, **Article number**, **Extruder setting (°C)**,
   **Bed setting (°C)**.
4. Click **Save Changes to Spoolman** and wait for
   "✓ Saved and verified in Spoolman. Generate a new tag preview."

## After an edit

The station saves the change, then reads the record again from Spoolman to make sure it was stored.

An edit changes Spoolman only. A tag that was written earlier still holds the old values.
The page reminds you: "Edits are verified in Spoolman. Every save requires a new tag preview."
To bring the tag up to date, put it on the reader and use **Update tag**
([Write or update a tag](../daily-use/manage-tags.md#update-a-tag-after-changing-the-spool-in-spoolman)).

## What the station changes in Spoolman

| The station does this | When |
|---|---|
| Sets a spool's remaining weight | When you save a weight, or with auto-update on. See [Weigh a spool and save the weight](../daily-use/weigh.md). |
| Fills or empties the two extra fields `nfc_uid` and `opentag_instance_uuid` on a spool | When a tag is written, moved or cleared. |
| Creates a spool for an existing filament | When you ask for it while writing a tag. |
| Changes the spool and filament fields listed above | When you save an edit in the browser. |

The station never:

- deletes or archives a spool, filament or vendor,
- creates the two extra fields themselves (you do that once, see [Required extra fields](custom-fields.md)),
- changes anything just because a spool sits on the station; every change starts with a tap or a
  click from you, or with a weigh when auto-update is on.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "No matching spools. Try changing your search or material filter." (or "filaments", "vendors") | Nothing matches the search and filters. | Search for one word of the filament name. Remove the material filter. |
| "This page is too large to show; narrow the search." | Spoolman's answer for this page is bigger than the station can hold. | Search for a more specific name. |
| "Inventory unavailable. … Open Inventory again to retry." | The station could not load the page. | Check Spoolman ([Connect Spoolman](spoolman.md)), then click **Inventory** again. |
| "No changed values to save." | You clicked save without changing a field. | Change a value or click **Cancel edit**. |
| "Spoolman changed. Current values: … Your draft is unchanged. Review these values, then Save Changes again." | Someone or something else changed the same fields in Spoolman while the editor was open, for example a print using filament. | Compare the current values with your draft, then save again if your draft is still right. |
| "Save was not verified. Refresh before retrying." | The station could not confirm the change in Spoolman. | Reload the page and look at the record before saving again. |

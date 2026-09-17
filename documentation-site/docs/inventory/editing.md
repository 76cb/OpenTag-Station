# Edit canonical inventory

Correct spool-specific or shared filament data in Spoolman before generating tag content.

## Before you start

A selected canonical record and network access. Know whether the intended field belongs to one spool or the shared filament definition.

## Steps

1. Open Inventory → My Spoolman and select the intended physical spool.
2. Choose **Edit Spool** for physical-spool fields or **Edit Filament Definition** for shared product fields. Read the displayed ID and shared-scope warning.
3. Enter the correction in the units shown. Review initial mass, used mass and empty-spool tare carefully; they mean different things.
4. Choose **Save Changes to Spoolman**. Wait for canonical readback and **Saved to Spoolman**.
5. If the tag must reflect the new canonical metadata, request a new tag preview and confirm that write separately.

## Expected result

The editor returns to reviewed canonical data. There is no hidden tag-only override and saving inventory does not silently write NFC.

## If it fails

A conflict retains the draft but shows current values. Decide whether the edit still applies and save explicitly again. IDs, associations, provenance and arbitrary extra fields are not writable through these editors; use the appropriate supported workflow instead.

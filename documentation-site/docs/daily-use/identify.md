# Identify a spool

Read an OpenPrintTag and resolve it to the canonical physical spool.

## Before you start

Reader ready, one supported tag and a reachable Spoolman instance for full inventory resolution. Home can show tag metadata even when canonical resolution is unavailable.

## Steps

1. Place one tagged spool near the reader, keeping other tags outside the field.
2. Wait for stable detection and the current-spool card. Check brand, material, spool number, tag status and link status.
3. If multiple candidate spools appear, compare them with your actual inventory, select the correct one and confirm. Do not choose solely because a name is similar.
4. If the tag is blank, use Inventory and the writer. If unsupported or malformed, inspect tag details before attempting any change.
5. Remove and reinsert the spool when checking a new physical item; wait for the previous identity to clear.

## Expected result

Home displays the expected spool ID and canonical material/weight. Actions requiring identity become available only after the station resolves the same current tag.

## If it fails

No detection can mean wrong tag technology, poor antenna placement or a reader fault. A tag with valid data but no inventory link requires identity resolution, not an automatic new spool. Stale identity must never be used to assign or update another spool.

![Current spool](../assets/images/browser/dashboard.png)

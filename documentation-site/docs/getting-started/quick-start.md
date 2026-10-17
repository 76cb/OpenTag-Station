# Your first spool

This is the shortest path from a blank tag and a spool in Spoolman to a written tag, a saved weight
and, if you use FilaBridge, a toolhead assignment. Each step links to the page with the details.

## Before you start

- The station is [on your Wi-Fi](first-boot.md), [connected to Spoolman](initial-setup.md) and the
  [scale is calibrated](../scale/calibration.md).
- Spoolman has the two [required extra fields](../inventory/custom-fields.md).
- The spool exists in Spoolman — or at least its filament does; the station can create the spool.
- You have a blank NXP ICODE SLIX2 tag. Use one tag at a time.

## 1. Write the tag

Stick the tag on the spool and put the spool on the station with the tag over the reader.

**Touchscreen**

1. Home shows "COMPATIBLE BLANK TAG — Ready to assign". Tap **ASSIGN TAG**.
2. Tap **MY SPOOLS**. Find the spool with **PREV** / **NEXT** or **SEARCH**, and tap it.
3. On "Use this spool?", tap **USE SPOOL**.
4. On "Ready to write", tap **WRITE TAG**. Keep the tag on the reader until it says done.
5. The screen shows **TAG READY** with the spool number and "verified".

No spool in Spoolman yet? Tap **MY FILAMENTS** instead, choose the filament, tap
**CREATE A SPOOL**, check the three weights, tap **CREATE SPOOL**, then **REVIEW TAG WRITE**.

**Browser**

1. The Dashboard shows "Blank tag — Ready to assign". Click **Assign tag**.
2. In the **Create OpenPrintTag** dialog, choose the spool on the **Spools** tab and click
   **Continue**.
3. Click **Preview tag**, then **Write this tag**, and confirm.
4. Wait for "✓ OpenPrintTag written and verified", then click **Done**.

The station has now written the tag, checked it, and stored the link on the spool in Spoolman.
Details: [Write or update a tag](../daily-use/manage-tags.md).

## 2. Weigh the spool and save the weight

Leave the spool on the station.

**Touchscreen**

1. On the **TAG READY** screen tap **WEIGH** (or tap **WEIGH** on Home).
2. Keep the spool still until the state reads **STABLE**. The result shows "Empty spool",
   "Filament now" and "In Spoolman".
3. Tap **UPDATE SPOOLMAN**. The message changes to "Saved to Spoolman."

**Browser**

1. On the Dashboard click **Weigh**.
2. When the measurement is stable, compare "Measured filament" with "Spoolman currently".
3. Click **Update Spoolman**.

Nothing is saved until you press the update button, unless you have turned on automatic saving.
If the weight looks wrong, tap **WEIGH AGAIN** first.
Details: [Weigh a spool and save the weight](../daily-use/weigh.md).

## 3. Assign the spool to a toolhead (optional)

This needs FilaBridge and a selected printer.

**Touchscreen**

1. Tap **ASSIGN TO PRINTER** on the **TAG READY** screen.
   Later you get to the same page with **ASSIGN** on Home or **Assign** in the bottom bar.
2. Tap the toolhead, **T1** to **T5**.
3. Confirm with **Assign**. If the toolhead already holds a spool the station asks
   "Replace spool?"; confirm with **Replace**.

**Browser**

1. On the Dashboard click **Assign**.
2. Click the toolhead tile, then the **Assign to T…** button.
3. Wait for "Assigned and verified on the printer."

Details: [Assign a spool to a toolhead](../daily-use/assign.md).

## What you should see

Take the spool off and put it back. The station recognises it by itself:

- Touchscreen Home: the maker, the filament name, "Spool #…" and "… g remaining".
- Browser Dashboard: "CURRENT SPOOL" with "Tag valid" and "Linked to Spoolman".

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| The screen stays on "Place a spool" | The tag is not seen. | Check that it is an ICODE SLIX2 tag and sits over the reader. See [Tag is not detected](../troubleshooting/tag.md). |
| "This tag can't be used as it is" | The tag is not blank and does not hold filament data the station understands. | Use a blank tag. See [Which tags work](../openprinttag/supported-tags.md). |
| "Needs attention" with a message about a missing Spool extra field | The extra fields are missing in Spoolman. | [Add the extra fields](../inventory/custom-fields.md). |
| The Home button reads **CALIBRATE** | The scale is not calibrated. | [Tare and calibrate](../scale/calibration.md). |
| "The empty spool weight is unknown. Set it on the spool in Spoolman, then weigh again." | Neither Spoolman (spool, filament, vendor) nor the tag holds an empty-spool weight. | Enter it in Spoolman. See [Weigh a spool and save the weight](../daily-use/weigh.md). |
| The toolhead buttons are greyed out | No linked spool on the station, FilaBridge offline, or no printer chosen. | See [Printer assignment fails](../troubleshooting/assignment.md). |
| The station asks you to finish writing a tag | A write was interrupted. | [Finish an interrupted tag operation](../troubleshooting/journal.md). |

Other problems: [Find your problem](../troubleshooting/index.md).

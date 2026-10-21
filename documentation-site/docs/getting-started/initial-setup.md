# Connect Spoolman and FilaBridge

The station needs to know where your Spoolman is. FilaBridge is optional and only needed to assign
spools to the toolheads of a Prusa XL. This page also points you to the two remaining one-time
jobs: choosing the load cell and calibrating the scale.

## Before you start

- The station is [on your Wi-Fi](first-boot.md).
- Spoolman is running and reachable from the same network.
- On Spoolman older than 0.27: you have added the two
  [extra fields](../inventory/custom-fields.md) in Spoolman. Without them the station cannot write
  or link a tag. On Spoolman 0.27 or later they are optional.

## Connect Spoolman

Use an address that starts with `http://`, for example `http://<spoolman-address>:7912`.
An `https://` address does not work unless a certificate has been stored through the API; there is
no field for it in either interface.

**Browser**

1. Open the station's web page and go to **Settings** → **Integrations**.
2. Open **Edit Spoolman**.
3. Enter the address in **Base URL**. If your Spoolman needs a token, enter it in **New token**.
4. Click **Validate and save**.
5. On the **Integrations** card, click **Test**.

The card shows the state of **Spoolman**, **FilaBridge** and **Printer**. Any problem appears as a
warning under the card.

**Touchscreen**

1. Tap **Settings** → **Wi-Fi & services**, then **Next** until you reach the **SPOOLMAN** step.
2. Enter the **Spoolman URL** (and the **Authentication token (optional)**), then tap **Save**.

The status "URL saved" only means the address was stored. The touchscreen has no connection test;
use **Test** in the browser to be sure.

### The two extra fields

On Spoolman 0.27 or later you can skip this: the station links each tag to its spool in Spoolman's
own tag list, and no warning appears when the fields are missing.

On older Spoolman, two extra fields for spools (Spoolman calls this the **Spool** entity) must
exist, both of type **Text**:
`opentag_instance_uuid` and `nfc_uid`. If they are missing, **Test** shows:

> Spoolman is missing the Spool extra fields 'opentag_instance_uuid', 'nfc_uid'. In Spoolman open
> Settings > Extra Fields > Spool and add them with type Text.

How to add them: [Required extra fields](../inventory/custom-fields.md).

More about versions and what the station reads and changes: [Connect Spoolman](../inventory/spoolman.md).

## Connect FilaBridge and choose the printer (optional)

Skip this section if you only want tags and weighing. With no FilaBridge address saved, the
touchscreen has no **Assign** tab or button and setup has no **PRINTER** step. Saving an address
later brings them back.

The touchscreen can assign to the first five toolheads. A printer that reports more can be
assigned from the browser.

**Touchscreen** — the easier way, because it lists the printers for you:

1. **Settings** → **Wi-Fi & services**, then **Next** to the **FILABRIDGE** step.
2. Enter the **FilaBridge URL**, tap **Save**, then **Next**.
3. On the **PRINTER** step, choose your printer from the list and tap **Save**.
   "No printers found yet" means FilaBridge has not answered; check the URL.
   The list starts at the printer that is already saved. If FilaBridge does not report that
   printer, it is listed as "<printer ID> (not found)"; choose your printer again.

**Browser**

1. **Settings** → **Integrations** → **Edit FilaBridge**.
2. Enter the **Base URL**, for example `http://filabridge.local:5000`.
3. Type the printer's ID into **Selected stable printer ID**. The ID is the key FilaBridge keeps
   the printer under, not the printer's name. The browser has no list to pick from, so choose the
   printer on the touchscreen if you do not know the ID.
4. Click **Validate and save**, then **Test**.

Details, supported FilaBridge versions and toolhead settings:
[Set up FilaBridge for a Prusa XL](../printer/prusa-xl.md).

## Choose the load cell and calibrate

1. If your load cell is the 2 kg type, change the profile first — browser only:
   **Settings** → **Scale** → **Edit Load-cell profile** → **YZC-133 variant** → **2 kg** →
   **Validate and save**. The default is **5 kg**.
2. Calibrate the scale with a known weight: [Tare and calibrate](../scale/calibration.md).

!!! warning
    Changing the load-cell profile deletes the calibration. Choose the profile before you
    calibrate.

## Set an access token (recommended)

Without a token, anyone on your network can change settings, write tags, update the firmware or
factory-reset the station from a browser. See
[Access token and network safety](../configuration/security.md).

## What you should see

- Browser **Settings** → **Integrations**: after **Test**, the **Spoolman** row does not say
  offline and there is no warning under the card.
- Touchscreen **Assign** page: a status line with "Spoolman ONLINE" and, if you use it,
  "FilaBridge ONLINE".
- The touchscreen Home button reads **WEIGH**, not **CALIBRATE**.

Next: [Your first spool](quick-start.md).

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "Spoolman is missing the Spool extra field …" | Your Spoolman is older than 0.27 and one or both extra fields are missing or not of type Text. | [Add the extra fields](../inventory/custom-fields.md), then click **Test** again. |
| "Spoolman … has not been tested with this firmware, so saving weights to Spoolman is turned off." | Your Spoolman version is not one the station saves weights to. | See [Connect Spoolman](../inventory/spoolman.md). |
| "HTTPS requires a configured CA certificate" | The address starts with `https://`. | Use the `http://` address. |
| "DNS resolution failed or is still pending for …" | The station cannot look up that name. | Use Spoolman's IP address instead of its name. |
| Touchscreen says "URL saved" but nothing loads | The address was stored but may be wrong. | Click **Test** in the browser. |

More cases: [Spoolman problems](../troubleshooting/spoolman.md) and
[Printer assignment fails](../troubleshooting/assignment.md).

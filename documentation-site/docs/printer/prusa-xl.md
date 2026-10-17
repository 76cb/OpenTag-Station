# Set up FilaBridge for a Prusa XL

FilaBridge is a separate program that watches your printer and books the filament each print uses
against a spool in Spoolman. For that it must know which spool is in which toolhead.
The station tells it: you put a spool on the station, pick a toolhead, and the station sends that
pairing to FilaBridge.

FilaBridge is optional. Without it the station still identifies, weighs and tags spools; only the
**Assign** page stays unused.

## Before you start

- FilaBridge is installed, reachable on your network and already shows your printer.
  Set the printer up in FilaBridge first.
- You know FilaBridge's address, including the port, for example `http://192.168.1.60:5000`.
- [Spoolman is connected](../inventory/spoolman.md) to the station.

## Steps

The touchscreen is the easier way, because it lists the printers FilaBridge knows.

### Touchscreen

1. Tap **Settings**, then **Wi-Fi & services**.
2. Tap **Next** until you reach "FILABRIDGE" ("Enter and save the service base URL.").
3. Type the address into **FilaBridge URL**. Fill **Authentication token (optional)** only if
   something in front of FilaBridge asks for one. Tap **Save**; the status reads "URL saved".
4. Tap **Next** to reach "PRINTER" ("Choose the printer FilaBridge found.").
5. Pick your printer from the list. This fills in **Printer ID**.
   If the list reads "No printers found yet", FilaBridge has not answered yet; check the address.
6. Tap **Save**; the status reads "Printer ID saved". Tap **Home**.

### Browser

1. Go to **Settings → Integrations** and open **Edit FilaBridge**.
2. Type the address into **Base URL**.
3. Type the printer's ID into **Selected stable printer ID**. The ID is the key FilaBridge keeps
   the printer under, not the printer's name. The browser has no list to pick from; if you do not
   know the ID, choose the printer on the touchscreen instead.
4. If needed, type a token into **New token (blank keeps current)**.
5. Click **Validate and save**, then **Test** on the **Integrations** card.

### Check it

- Browser: the **Integrations** card shows FilaBridge as connected and names the printer.
  The **Printer** page shows the printer with its toolheads.
- Touchscreen: the **Assign** page shows the printer's name with "Connected", and
  "FilaBridge ONLINE" in the status line.

Then assign your first spool: [Assign a spool to a toolhead](../daily-use/assign.md).

## Supported FilaBridge versions

The station assigns spools only when FilaBridge reports version **1.2.1** or **1.2.2**
(with or without a leading `v`), or `dev`.

With any other version the station still shows the printer and what each toolhead holds, but it does
not change anything. The toolhead buttons on the touchscreen stay greyed out, and an attempt answers
"FilaBridge is connected read-only; mapping capability is unavailable and no assignment was queued".

If you update FilaBridge while the station is running, click **Test** in the browser so the station
checks the new version.
The versions the firmware was built against are listed in [Supported versions](../reference/upstream.md).

## How toolheads are numbered

| On the station | In FilaBridge |
|---|---|
| T1 | Toolhead 0 |
| T2 | Toolhead 1 |
| T3 | Toolhead 2 |
| T4 | Toolhead 3 |
| T5 | Toolhead 4 |

The touchscreen always shows five buttons, T1 to T5.
The browser **Printer** page shows the toolheads FilaBridge reports and uses FilaBridge's own
toolhead names when it provides them.

Before you rely on it, load a spool into T1 on the printer, assign it to **T1** on the station and
check in FilaBridge that the first toolhead now holds that spool.

### Switch off toolheads you do not use

Browser: **Settings → Advanced → Edit Toolhead profiles**. Each toolhead has a **Name** and an
**Enabled** tick box, next to its nozzle details. Untick **Enabled** and click
**Validate and save**. A disabled toolhead cannot be assigned from the station.

## What the station sends to FilaBridge

- To show the Assign and Printer pages, the station reads FilaBridge's status, its printer list and
  the current spool of each toolhead.
- To assign, it sends three things: the printer's name, the toolhead number and the Spoolman spool
  number. Then it reads the toolheads again and checks that FilaBridge now reports that spool.
- To unassign (browser only), it sends the same request with spool number 0.
- A saved token goes along with every request. If it does not start with `Bearer` or `Basic`, the
  station sends it as a `Bearer` token.

The station does not send weights, tag data or print jobs to FilaBridge, and it does not talk to the
printer directly.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| "No printer configured" and "Choose a printer in Settings." (touchscreen Assign page) | No printer ID is saved, or FilaBridge does not list that ID. | Choose the printer again as described above. |
| "No printer configured" with **Open Settings** (browser Printer page) | The same. | The same. |
| "No printers found yet" in the printer list | FilaBridge has not answered. | Check the address and that FilaBridge is running. |
| "FilaBridge URL is not configured" | No address is saved. | Enter it. |
| "FilaBridge stable printer ID was not found" | The saved printer ID is not in FilaBridge's list. | Pick the printer from the touchscreen list. |
| "FilaBridge authentication was rejected" | The token is wrong or missing. | Enter the right token. |
| "FilaBridge version changed; use Test backends to verify capabilities" | FilaBridge reports a different version than at the last check. | Click **Test** in the browser. |
| "FilaBridge is connected read-only; mapping capability is unavailable and no assignment was queued" | Unsupported FilaBridge version. | Use 1.2.1 or 1.2.2. |

More: [Printer assignment fails](../troubleshooting/assignment.md).

## Related

- [Assign a spool to a toolhead](../daily-use/assign.md)
- [FilaBridge integration reference](../reference/filabridge.md)

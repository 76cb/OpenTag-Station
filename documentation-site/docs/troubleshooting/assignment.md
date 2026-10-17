# Printer assignment fails

Assigning tells FilaBridge which spool is loaded in which toolhead.
The station sends the request, then asks FilaBridge what is loaded and compares.
Nothing is stored for later: if FilaBridge is offline, the request is refused and you try again.

## The toolhead buttons are greyed out

On the touchscreen **Assign** page a toolhead button works only when all of this is true:

- A spool is on the station and its tag is linked to a Spoolman spool.
- FilaBridge is reachable and the printer is listed: the page shows the printer name.
- FilaBridge is a version the station can assign with (see below).
- The toolhead is enabled in the browser under **Settings → Advanced → Edit Toolhead profiles**.
- No other assignment is running.

| What you see | What it means | What to do |
|---|---|---|
| `No printer configured` / `Choose a printer in Settings.` | No printer is selected, or FilaBridge does not answer. The status line at the bottom of the page then shows the reason. | Check that FilaBridge runs and its address is right: browser **Settings → Integrations → Edit FilaBridge**, then **Test**. If no printer was ever chosen, see [Set up FilaBridge for a Prusa XL](../printer/prusa-xl.md). |
| Browser: `Printer unavailable. Check FilaBridge in Settings, then try again. Nothing has been assigned.` | The browser lists no printer, for the same reasons. | The same. |
| `Offline` next to the printer name | FilaBridge answers, but reports the printer itself as offline. | Switch the printer on and check its connection to FilaBridge. |
| `Checking connection` next to the printer name | FilaBridge reports a printer state the station does not know. | Wait. Check the printer in FilaBridge. |
| Buttons grey although the printer is `Connected` | No linked spool is on the station, or the FilaBridge version is not supported. | Place a linked spool. Check the version. |

## FilaBridge version

The station assigns spools only with FilaBridge versions it was tested with;
they are listed under [Supported versions](../reference/upstream.md).
With any other version the station shows the printer and its toolheads but cannot change them.
On the touchscreen the toolhead buttons stay grey. In the browser, trying to assign shows:

`FilaBridge is connected read-only; mapping capability is unavailable and no assignment was queued`

If you changed the FilaBridge version while the station was running, you may see
`FilaBridge version changed; use Test backends to verify capabilities`.
Click **Test** under **Settings → Integrations** in the browser.

## The confirmation dialogs

The station always asks before it changes a toolhead. An active print does **not** block the
assignment; it changes the question.

| Situation | Touchscreen dialog | Buttons |
|---|---|---|
| Toolhead empty, printer idle | **Assign spool?** | **Cancel** / **Assign** |
| Toolhead already has a spool | **Replace spool?** — `T2 currently contains spool #27. Replace it with spool #28?` | **Cancel** / **Replace** |
| Printer is printing, paused or waiting for attention | **ACTIVE PRINT** — `This printer is actively printing. Mapping T2 may corrupt consumption accounting.` | **Cancel** / **Assign anyway** or **Replace anyway** |
| The station cannot tell what the printer is doing | **PRINTER STATE UNVERIFIED** — `This printer state cannot be verified. Mapping T2 may corrupt consumption accounting.` | **Cancel** / **Assign anyway** or **Replace anyway** |

When the toolhead already has a spool, the two warning dialogs name it instead:
`This printer is actively printing and T2 contains spool #27. Replacing it may corrupt consumption accounting.`
or `This printer state cannot be verified and T2 contains spool #27. Replacing it may corrupt consumption accounting.`

"May corrupt consumption accounting" means: if you change the spool of a toolhead in the middle of
a print, the filament used by that print may be counted against the wrong spool.
Tap **Cancel** unless you really are swapping the spool on that toolhead now.

The browser asks the same questions as OK / Cancel prompts:
`This toolhead is occupied by spool #27. Replace it with spool #28?` and
`Printer state is “printing”. This advanced override can corrupt consumption accounting. Continue?`
(with the printer's actual state in the quotes).

## After you confirm

| What you see | What it means |
|---|---|
| `Assignment verified by FilaBridge readback` (browser: `Assigned and verified on the printer.`) | Done. FilaBridge reports the spool on that toolhead. |

After any failure the browser shows
`Assignment was not verified. Check the station message and refresh the printer before trying again.`
The touchscreen shows the station message itself on the status line of the **Assign** page.

### Sent, but not confirmed

The request may have changed the toolhead. Look before you try again.

| What you see | What it means | What to do |
|---|---|---|
| `Sent to FilaBridge but could not be verified (<reason>). Refresh printers before retrying.` | The request went out, but the station could not confirm the result. | Look at the toolhead on the Assign page (browser: **Printer** page) before you try again. |
| `FilaBridge assignment verification failed: mapped spool does not match` | FilaBridge answered, but reports a different spool on that toolhead. | Check the toolhead in FilaBridge itself, then assign again. |

### Refused or stopped

| What you see | What it means | What to do |
|---|---|---|
| `FilaBridge is offline; toolhead assignment was not queued` | FilaBridge could not be reached. Nothing was changed and nothing will be sent later. | Bring FilaBridge back, then assign again. |
| `Toolhead assignment precondition is stale; refresh before confirming` | The spool or the toolhead changed between your tap and your confirmation. | Open the Assign page again and repeat. |
| `Spool or printer snapshot changed while the assignment was pending; refresh and retry` | The same. | The same. |
| `A resolved Spoolman spool is required before assignment` | The spool on the station is not linked to Spoolman. | Link the tag: [Write or update a tag](../daily-use/manage-tags.md). |
| `Selected local toolhead profile is disabled` | That toolhead is switched off in the station's settings. | Browser: **Settings → Advanced → Edit Toolhead profiles**. |
| `Selected printer no longer exists in FilaBridge` | The saved printer ID does not match any printer in FilaBridge. | Choose the printer again: [Set up FilaBridge for a Prusa XL](../printer/prusa-xl.md). |
| `FilaBridge stable printer ID was not found` | The same. | The same. |
| `Selected toolhead no longer exists in FilaBridge` | FilaBridge reports fewer toolheads than before. | Check the printer in FilaBridge. |
| `FilaBridge rejected a conflicting assignment` | FilaBridge refused the change as a conflict. | Check the toolheads in FilaBridge itself. |
| `FilaBridge authentication was rejected` | The saved FilaBridge token is wrong. | Browser: **Edit FilaBridge → New token**. |
| `FilaBridge URL is not configured` | No FilaBridge address is saved. | Enter it under **Edit FilaBridge → Base URL**. |
| `Station is busy. Try the assignment again.` | The station was doing something else. | Try again. |

## Good to know

- T1 to T5 on the station are FilaBridge toolheads 0 to 4. The touchscreen shows five toolheads.
- Removing a spool from a toolhead is only possible in the browser:
  **Printer** page, **Unassign**. The touchscreen has no unassign.

## Related

- [Assign a spool to a toolhead](../daily-use/assign.md)
- [FilaBridge integration](../reference/filabridge.md)

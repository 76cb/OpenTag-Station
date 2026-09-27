# Weigh a spool

Measure the spool and, if you choose, save the result to Spoolman. Weighing on
its own never changes Spoolman.

## Before you start

The scale is calibrated, the tag is linked to a Spoolman spool, and that spool
has an empty-spool weight and an initial weight in Spoolman. Rest the whole spool
on the platform; don't let filament or a cable pull on anything else.

## Steps

1. Tap **WEIGH** on Home (touchscreen) or **Weigh** in the browser dashboard.
2. Keep the spool still until the reading settles.
3. Read the result:
    - **Empty spool** – the reel's own weight.
    - **Filament now** – what is on the scale minus the empty reel.
    - **In Spoolman** – the remaining weight Spoolman has now.
4. To save it, tap **UPDATE SPOOLMAN**. The message above the buttons tells you
   exactly what will be saved (for example "set its remaining weight to 712 g").
5. To measure again, tap **WEIGH AGAIN**.

**Auto-update Spoolman after Weigh** (Settings) saves automatically instead. It is
off by default.

## When UPDATE SPOOLMAN is not available

The message on the Weigh screen says why. The common cases:

| Message | What to do |
|---|---|
| This spool is not linked to Spoolman yet | Manage tag → **Link to a spool**, then weigh again. |
| The empty spool weight is unknown | Set the spool's empty weight in Spoolman, then weigh again. |
| Spoolman has no remaining weight for this spool | Set its initial weight in Spoolman, then weigh again. |
| The measured filament is more than this spool's initial weight | Check the empty spool weight, or correct the initial weight in Spoolman. |
| Saving weights is turned off… | Your Spoolman version has not been tested with this firmware (see Settings → Integrations), or Spoolman is offline. |
| Spoolman already matches this weight | Nothing to do. Differences up to 5 g (the default tolerance) are not saved. |

The empty-spool weight is taken from the Spoolman spool first, then from the tag,
then the filament, then the vendor.

![Weigh receipt](../assets/images/browser/weigh.png)

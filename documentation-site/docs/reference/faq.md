# FAQ and error interpretation

**Can any NFC sticker work?** No. The production writer requires the approved
NXP SLIX2 80 × 4-byte NFC-V profile, complete protection information and stable
single-tag inventory. NTAG/MIFARE and other geometries are not substitutes.

**Does leaving a spool on the station update its weight continuously?** No.
Only an eligible completed explicit Weigh can request an inventory update.
Auto-update is off by default and each measurement is attempted once.

**Does Clear / Reuse delete my Spoolman spool?** No. It clears supported tag
payload bytes, verifies blank memory, and removes only the matching configured
UID/instance extra values and local confirmed association. Usage and unrelated
spool fields remain.

**Why is a button disabled?** The operation may need a resolved current spool,
fresh preview, supported capability, stable measurement, idle lifecycle or recovery
completion. Inspect the adjacent state instead of bypassing the fence.

| Error class | Meaning | Next step |
|---|---|---|
| Conflict / stale revision | Data changed after the screen captured it | Reload and review a fresh confirmation |
| Unsupported / protected | Current hardware/tag/backend cannot safely perform the action | Check supported profile; do not force |
| Offline / timeout | Backend or transport result unavailable/uncertain | Inspect fresh state before any new mutation |
| Unstable / stale sample | Scale cannot establish a valid measurement | Settle platform and start a fresh Weigh |
| Recovery / unlink pending | Durable operation has unfinished work | Follow its specific recovery action |

**Are screenshots physical proof?** Browser images use deterministic offline
fixtures. WT32 images approximate production geometry/fonts. Neither demonstrates
physical NFC, scale accuracy, real printer writes or touch fidelity.

**Is the API token required?** No. Blank-token trusted-LAN control is supported.
A configured token protects mutations but does not add HTTP encryption.

**Where is final 1.0.0?** It is not published while acceptance is pending. Check
the footer and VERSION-derived build metadata for the actual candidate version.

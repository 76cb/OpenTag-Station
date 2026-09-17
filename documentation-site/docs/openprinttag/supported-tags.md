# Supported tags

The supported production writer profile is **NXP ICODE SLIX2**, NFC-V / ISO15693,
with manufacturer UID prefix `E0:04`, **80 blocks of 4 bytes** (320 bytes total).
The station modifies only bytes 0–311 (blocks 0–77); bytes 312–319 (blocks 78–79)
must remain unchanged. Protection/system information must be complete and safe.

| Tag characteristic | Production requirement |
|---|---|
| RF technology | NFC-V / ISO15693 |
| Approved family | NXP ICODE SLIX2 profile |
| Geometry | Exactly 80 × 4 bytes |
| Manufacturer bytes | E0:04 in normalized UID order |
| Security | Complete checked security information; no protected target block |
| Inventory | One stable tag, same UID and generation throughout the operation |
| Source image | Recognized supported content, blank compatible image, or matching recovery journal |

NTAG and MIFARE stickers use other technologies/profiles. Other ISO15693 vendors,
memory sizes, protected tags and malformed images can be detected or rejected
without being safe write targets. A reader's broad RF capability is not a
promise of application support. Purchase the approved geometry explicitly.

## Check a tag before use

1. Place only one tag near the reader. Wait for stable identification.
2. Open Manage tag and inspect type, UID and geometry. Documentation examples use
   the synthetic UID `E004000000000028`; do not use it as a physical identity.
3. For an empty approved tag, open the writer and request a preview for a chosen
   Spoolman spool. Preview refusal is a safety decision, not a reason to force raw writes.
4. Review the proposed data and warnings before confirming. Keep power and tag
   position stable through verification.

Success is a verified OpenPrintTag linked to the intended spool. If unsupported,
replace it with an approved tag; do not change geometry guards or unlock unknown
tags as a workaround. See [writing](writing.md) and [recovery](recovery.md).

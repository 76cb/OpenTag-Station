# Supported production hardware

The maintained [hardware manual](https://76cb.github.io/OpenTag-Station-Docs/hardware/bill-of-materials/)
contains the BOM, complete harness table, connector orientation, voltage checks,
load-cell mounting and post-assembly validation. Its reviewable source is in
[documentation-site](../documentation-site/docs/hardware/bill-of-materials.md).

The production controller is WT32-SC01 Plus. Scale uses NAU7802 at `0x2A` on
GPIO10/11, `Wire` controller 0, 400 kHz. NFC uses ELECHOUSE NFC_ST25R3916B at
`0x50` on GPIO13/14, IRQ12, `Wire1` controller 1, 100 kHz. Built-in touch uses
LovyanGFX software I²C port −1 on GPIO6/5; it does not own a hardware I²C controller.

YZC-133 5 kg is the default scale profile; 2 kg is a supported alternative.
Production tag writing is implemented for the approved NXP SLIX2 80 × 4-byte
profile with preview, confirmation, journaling and readback. Routine polling is
read-only. Full integrated physical acceptance remains in [releasing](releasing.md).

Install normal OpenTag Station for all build validation. No standalone test
firmware environment is shipped. Older hardware notes that put touch on controller
1 or describe tag mutation as future work are superseded by this production profile.

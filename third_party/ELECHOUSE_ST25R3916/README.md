# ELECHOUSE ST25R3916 diagnostic dependency

This directory contains an unchanged, pinned copy of the two Arduino libraries
used by the opt-in WT32-SC01 Plus I2C/RF diagnostic:

- upstream: `https://github.com/wilson-elechouse/ST25R3916`
- commit: `16eb6c7fb13e502d320924040d768a9e564209b2`
- commit date: 2026-07-09
- `ST25R3916_ELECHOUSE` version: 1.1.1
- `NFC-RFAL` version: 1.0.2

The library directories and their license files are copied without local
modification. OpenTag Station code binds the library to the diagnostic-only
`Wire1` bus; normal production firmware does not include or activate this NFC
path.

# ELECHOUSE ST25R3916 production dependency

This directory contains an unchanged, pinned copy of the two Arduino libraries
used by production NFC recognition and the guarded OpenPrintTag writer:

- upstream: `https://github.com/wilson-elechouse/ST25R3916`
- commit: `16eb6c7fb13e502d320924040d768a9e564209b2`
- commit date: 2026-07-09
- `ST25R3916_ELECHOUSE` version: 1.1.1
- `NFC-RFAL` version: 1.0.2

The library directories and their license files are copied without local
modification. Production binds these libraries to Wire1 on the shared backend task; routine polling is read-only, and the private write binding is accessible only through the approved guarded writer. See [production NFC](../../docs/production-nfc.md).

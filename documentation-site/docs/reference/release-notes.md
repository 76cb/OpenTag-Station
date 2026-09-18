# Release notes

# Changelog

## 1.0.0-rc.4

Community search now uses a versioned, block-compressed local catalog shared by
the WT32 and browser. Fresh Web Flasher installs include the catalog in LittleFS;
upgraded stations can download it from the pinned OpenTag origin. Downloads stage,
hash, fully validate, and atomically install the replacement while retaining the
previous catalog on every failure. Search and detail selection work offline and
do not call the Community network service. Hardware performance acceptance is pending.

## 1.0.0-rc.3

Fix WT32 Community searches that exhausted the backend deadline or starved
CPU0's idle task. Pages stop after eight results plus one lookahead match;
sparse/final scans cooperate every 4 KiB and have a Community-only 60-second
budget. Full gzip responses retain CRC validation; intentional page completion
closes the stream without requiring an unread trailer. Search errors provide
Community-specific timeout guidance and Retry. Hardware acceptance is pending.

## 1.0.0-rc.2

- Standalone WT32 selection from My Spools, My Filaments, or Community; physical
  spool creation, import/reuse, exact writer review, and reassignment without a prior clear.
- Shared tag lifecycle: compatible blanks offer Assign; successful cleanup offers
  immediate reuse. Existing remote ownership and NFC safety fences remain mandatory.
- Full-screen text, numeric, URL and password input with 44 px keys, explicit acceptance,
  cancel, and repeat backspace. Bounded inventory pages and streaming gzip Community search.
- Explicit production version changes enforced in PR CI. Physical touch comfort,
  real-network Community timing and complete standalone appliance acceptance remain pending.

# 1.0.0

**Unreleased; acceptance pending.** The current candidate is derived from `VERSION`.
No final 1.0.0 release or tag has been published by this change.

- Current-spool dashboard, responsive inventory, explicit Weigh, printer assignment,
  tag management, Community import, and organized settings in the browser.
- Five touch views for the WT32-SC01 Plus, with large actions and shared spool state.
- Guarded NXP ICODE SLIX2 OpenPrintTag writing, readback, recovery journaling,
  Clear / Reuse, and verified Spoolman identity cleanup.
- NAU7802/YZC-133 calibration, tare, stability, and explicit measurement receipts.
  Spoolman auto-update after explicit Weigh is off by default.
- Spoolman remains canonical; FilaBridge maps Prusa XL T1–T5 with confirmation
  and exact backend readback. Destructive requests are never silently replayed.
- Browser Wi-Fi setup, optional local API authentication, configuration backup,
  and local A/B OTA with recovery safeguards.
- Production-only USB web installer, reproducible version/provenance checks,
  tag-triggered release packaging, and a separate hardware/user/developer manual.
- Sanitized product fixtures and guarded screenshot publication.

Final visual, live integration, physical tag/scale/touch, stability, and recovery
acceptance remains required. See [the release checklist](../contributing/release.md).

## Development history

Earlier milestones are preserved in [the historical changelog](https://github.com/76cb/OpenTag-Station/blob/main/docs/history/changelog-before-1.0.md).

# Changelog

## 1.0.0-rc.10

Final WT32 core-workflow usability cleanup from physical acceptance:

- Spoolman searches now use Spoolman's bounded cross-entity search when no structured filters are active, so vendor, filament name, material and article/SKU terms can find My Filaments/My Spools without a station cache.
- WT32 spool/tag navigation distinguishes PREVIOUS PAGE / NEXT PAGE from BACK TO SOURCES, and primary, navigation, destructive and disabled actions use visibly different treatments.
- The duplicate Home-screen Settings action is removed; Settings remains in the persistent bottom navigation and Manage Tag spans the secondary action row.
- Tag read/write/verify/link phases use the available screen area with centered status, explicit keep-tag/power guidance and block progress when available.
- The WT32 scale page clearly exposes UPDATE SPOOLMAN after a valid manual Weigh and visually demotes WEIGH AGAIN while an update is pending.
- Raw LVGL tag/scale containers are explicitly styled to avoid stray default rectangles/scrollbar artifacts.

## 1.0.0-rc.9

Community import/search is disabled for 1.0 after physical ESP32-S3 testing demonstrated a miniz inflater-state memory overwrite. The implementation is retained for redesign in 1.1.

- My Spools and My Filaments remain available on WT32 and in the optional browser. Production rejects Community API/import actions and installs no catalog callbacks; no startup download, verify, inflate, or catalog access runs.
- Failed browser NFC preview retains the reviewed physical spool. Retry Read uses the same spool and mode; Back to Review preserves values; Back to Select reloads the current source and clears stale rows. Busy state ends on completion or failure.
- Update Existing Station uses application-only A/B OTA, preserving LittleFS configuration, NVS, scale calibration and service/printer settings. Factory Install / Recovery explicitly erases configuration and calibration. Disabled Community is not seeded into factory LittleFS.
- Final physical acceptance of the core workflow is still required. Community is postponed, not fixed.

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
  tag management and organized settings in the browser.
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
acceptance remains required. See [the release checklist](docs/releasing.md).

## Development history

Earlier milestones are preserved in [the historical changelog](docs/history/changelog-before-1.0.md).

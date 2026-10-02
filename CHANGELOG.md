# Changelog

## 1.0.0-rc.12

Fixes from the deep-refactor audit (report: issue #47). Tag writer and spool identity; no new features.

- Clearing a tag, retrying an unlink and retrying a link now check that the two Spool extra fields exist in Spoolman before looking up the owner. Without them Spoolman ignores the lookup filter, and the station could remove the identity of an unrelated spool or loop on "Multiple NFC UID owners".
- A write or clear that is refused before it starts (for example the tag was lifted after confirming) no longer leaves a recovery record. In rc.10/rc.11 it blocked other tags, and after a refused clear even browsing spools.
- A clear interrupted before the Spoolman settings were changed now shows the recovery prompt with the reason, so **Skip recovery** is reachable. It used to lock the writer in a failed state.
- Recovery prompts no longer carry the previous preview's data.
- A locally remembered tag-to-spool link is treated as a conflict when Spoolman now links that spool to a different tag, so the wrong spool cannot receive weight updates.
- The same tag is recognised when Spoolman stores its UID in lower case or with separators; it is no longer rewritten with a new identity.
- A spool list page that is too large to show fails with "This page is too large to show; narrow the search." instead of an endless "retry".

## 1.0.0-rc.11

- Wi-Fi: when joining the network fails, the station now says why instead of always "Wi-Fi connection timed out": password rejected, network not found (2.4 GHz only), signal too weak, or refused by the router. The reason comes from the Wi-Fi driver and is shown on the touchscreen setup screen and in the browser, and logged with the network's signal strength from the last scan.

## 1.0.0-rc.10

Fixes from the rc.9 release review. No new subsystems.

**Safety**
- WT32 printer assignment: the dialog's first button is now **Cancel** and only an explicit Assign/Replace confirms. In rc.9 the **Back** button performed the assignment.
- OTA: the firmware now keeps a new image pending until OpenTag's 30 s health check (Arduino's early auto-confirm is disabled), so a failing update rolls back. A station left in `reboot_pending` by rc.9 now normalizes to confirmed instead of refusing every later update.
- Spool identity: a vendor/material or product-barcode match is only a suggestion and must be chosen or linked before weights or assignments can target it. Lookups by `extra.<key>` are refused, with the field name to create, when the Spool extra fields do not exist (Spoolman silently ignores unknown filters).
- Reboot, factory reset and OTA restart are refused while a tag or Spoolman operation is running.
- The setup access point is WPA2-protected; its per-boot password is shown only on the touchscreen.

**Tag recovery**
- A write or clear that fails while the station is running returns to its recovery prompt instead of a dead-end "Tag needs attention".
- Writing a different tag no longer overwrites another tag's unfinished-write record.
- Link/unlink failures show the reason. **Skip recovery** (touchscreen) / **Skip linking** / **Skip cleanup** (browser) stop a recovery that can't be finished. Factory reset clears unfinished tag operations.

**Spoolman and weighing**
- Search falls back to the vendor name when no filament name matches, on the WT32 and in the browser.
- A remaining weight above the spool's initial weight is refused before any change; missing initial weight and untested-version cases have specific messages. The Spoolman spool's empty weight now takes priority over the copy on the tag.
- WT32 spool creation no longer defaults the empty-spool weight to 0 g: it uses the filament or vendor value, or asks for it.
- Settings → Integrations warns about missing identity fields and untested Spoolman versions.

**Touchscreen and browser**
- Weigh: WEIGH → WEIGH AGAIN, UPDATE SPOOLMAN highlighted when a weight can be saved, two-line status that says what will be saved or why it can't.
- Setup: real NFC reader status (rc.9 always said "NFC disabled by wiring guard"), Next saves typed values, printer chosen from FilaBridge's list, CALIBRATE NOW returns to setup.
- Tag: erase/skip actions are red, no "0 / 0 blocks" for Spoolman-only work, "1.75 mm" diameter, duplicate LINK/REASSIGN removed, paging back lands on the previous rows, no-change writes disabled, a way back from Spoolman requests that stall.
- Settings → Wi-Fi & services on the touchscreen; larger About/Advanced buttons; toolheads show which spool is loaded.
- Plain-language OTA wording and a link to the release files.

**Release**
- Release candidates can be tagged `v1.0.0-rc.N` and are published as GitHub pre-releases with the OTA application image. The installer is rebuilt from release tags only.
- The disabled Community catalog is no longer published on Pages or in releases (it also made the release file check fail).

Physical acceptance on hardware is still required; see docs/releasing.md.

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

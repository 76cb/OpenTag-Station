# Release notes

What changed in each version of OpenTag Station, newest first.

## 1.0.0

Not released yet. This is what the first release contains.

**Identify and weigh**
- Place a spool with an OpenPrintTag on the station and it reads the tag and finds the spool in
  Spoolman. Spoolman stays the record of what you own.
- **Weigh** measures the spool. **Update Spoolman** saves the remaining weight. Nothing is saved
  until you ask; saving automatically after each Weigh is a setting and is off by default.
- Scale support for the NAU7802 amplifier with a YZC-133 load cell (5 kg by default, 2 kg
  selectable), with tare and calibration.

**Tags**
- Write a blank NXP ICODE SLIX2 tag from a spool in Spoolman, update a tag after the spool
  changed, or move a tag to another spool. You can pick an existing spool or create one from a
  filament, on the touchscreen or in the browser.
- The station checks every block after writing it and reads the whole tag again at the end. If a
  write or clear is interrupted, it offers to finish it the next time that tag is on the reader,
  or to skip it if the tag is lost.
- **Clear / Reuse** erases a tag and removes its link in Spoolman. The spool stays in Spoolman.

**Printer**
- Assign the spool on the station to a toolhead (T1–T5) through FilaBridge. The station asks
  before it replaces a loaded spool or changes a printer that is printing, and checks afterwards
  that FilaBridge accepted the change.

**Station**
- A touchscreen with Home, Weigh, Assign, Tag and Settings views, and a web page with Dashboard,
  Inventory, Printer and Settings. Firmware updates, settings export and factory reset are in
  the browser only.
- First-time Wi-Fi setup through a password-protected setup network; the password is shown on
  the touchscreen.
- An optional access token for the web page, and settings export and import that never include
  passwords or tokens.
- Firmware updates from the browser keep your settings. After an update the station checks
  itself for 30 seconds and returns to the previous firmware if something is wrong.
- A USB installer for new stations and recovery at <https://76cb.github.io/OpenTag-Station/>.
  It erases settings and calibration.

**Good to know**
- The station works on your local network over plain HTTP. Do not expose it to the internet.
- Firmware files are not signed. Only install files from
  <https://github.com/76cb/OpenTag-Station/releases>.
- Supported hardware is the WT32-SC01 Plus with an ST25R3916B NFC reader. Only NXP ICODE SLIX2
  tags can be written.
- Saving weights needs Spoolman 0.26.1. Assigning toolheads needs FilaBridge 1.2.1 or 1.2.2.
- The Community filament catalog is not part of this release.

The manual is at <https://76cb.github.io/OpenTag-Station-Docs/>.

Each release has these files: `opentag-station-<version>-application.bin` (update from the
browser), `opentag-station-factory.bin` (USB installer image), `manifest.json`,
`build-metadata.json` and `SHA256SUMS`.

---

The release candidates below were development builds made for testing before 1.0.0.

## 1.0.0-rc.19

- The USB installer page now names the real update file, `opentag-station-<version>-application.bin`, instead of "firmware.bin".
- Removed touchscreen code that could never run or show: button positions that were always overwritten, an auto-update button on the Weigh page that was created hidden and never shown (the one in Settings is unchanged), and a gauge update for a widget that only exists on Home. The source checks that required this dead text now check the live code instead.

## 1.0.0-rc.18

Refactoring. No behaviour change.

- Touchscreen code is easier to read: the tag-flow model is formatted one statement per line, the per-page parts of the main refresh routine are separate functions, repeated colours have names, and declarations nothing used are gone.

## 1.0.0-rc.17

Refactoring. No behaviour change.

- Web request helpers (idempotency-key check, SHA-256 decoding, lower-casing) have one definition instead of two or three copies.
- The tag-writer snapshot limits have names instead of repeated numbers.
- The update start-up checks shared by the update worker and the application have one definition.
- The station no longer builds a full diagnostic snapshot for every web request, or re-evaluates start-up health fifty times a second, when the result is not used.

## 1.0.0-rc.16

Clean-up. No behaviour change.

- Removed code that no build compiled (an unused ESP32 RFAL platform adapter) and functions and constants nothing called.
- The sanitizer test environment now runs every native suite, not four of them.
- CI no longer runs twice for each pull-request commit and caches PlatformIO packages. A branch without an open pull request is no longer built until one is opened.

## 1.0.0-rc.15

Browser fixes; no new features.

- After a lost response, the same action (Weigh, Tare, the same search) is no longer blocked until the page is reloaded. The browser checks the earlier request and lets you repeat it once it has finished, or after 10 minutes if it cannot tell. The message now says what to do.
- The Clear / Reuse button keeps one label instead of switching between two wordings.
- Removed unused fallbacks for fields the station never sends.

## 1.0.0-rc.14

Touchscreen tag flow fixes; no new features.

- The recovery prompt for an unfinished write or clear stays available after DONE, after leaving the Tag page and after a failed retry. In rc.10/rc.11 it disappeared until restart.
- A tag reading error no longer replaces the recovery instructions.
- Taking one tag off the reader and placing a different one starts the tag screen fresh; the previous tag's choices no longer carry over.
- A progress screen started from the browser always offers BACK TO HOME, except while the tag is being written or linked. Edits made in the browser no longer move the touchscreen.
- A mistake in the new-spool weights keeps the form and its values. Paging back after a failed page no longer opens later lists on the wrong row.
- "Taking longer than usual" is timed per step, so linking after a long write is not reported as late.
- The weigh button reads WEIGH until there is a measurement (it always read WEIGH AGAIN).
- Printer override dialogs no longer show "TT1".
- Weight updates no longer stay blocked until restart after a single NFC bus error.
- Fixed an out-of-bounds read in the diagnostic trace of the update status request.

## 1.0.0-rc.13

Fixes to configuration, update start-up, scale and backends; no new features.

**Updates (OTA)**
- An update record that could never move on (for example the new image was selected but the bootloader fell back to the old one) is now ended at start-up, so a new update can be uploaded. Before, upload, cancel and restart were all refused until a USB reflash.
- A healthy new image is no longer rolled back when power was cut between selecting it and recording that selection.
- Firmware older than the stored configuration no longer runs on defaults and then overwrites that configuration; it leaves it untouched, refuses to save, and lets the update roll back.

**Configuration**
- The two Spoolman field keys must differ and use only a–z, 0–9 and `_` (Spoolman's own rule). Backend access tokens may not contain control characters. Stored values that break these rules are repaired when the station starts.
- Tagging more than 64 spools no longer fails at the last step of a verified write; the local list of confirmed links drops its oldest entry (Spoolman remains the record).
- An unusable calibration left by very old firmware no longer blocks start-up.

**Scale and weight**
- Calibration is refused when no reference weight is detected, instead of saving a meaningless factor; the message says so.
- Changing scale hardware settings during a measurement ends that measurement with a message instead of blocking the scale until restart.
- A Spoolman empty-spool weight of 0 no longer overrides a real value from the filament, vendor or tag.

**Backends and Wi-Fi**
- One lost Spoolman version request no longer turns weight saving off for minutes; a failed Spoolman read check stays visible.
- When a printer mapping was sent but could not be read back, the message says so and the printer list stays current.
- Wi-Fi failure reasons now match the driver's codes: a handshake mismatch is reported as a password/security problem, "too many clients" as a router refusal.

## 1.0.0-rc.12

Tag writer and spool identity fixes; no new features.

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

Safety and usability fixes. No new subsystems.

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

## 1.0.0-rc.9

Community import/search is disabled for 1.0 after physical ESP32-S3 testing demonstrated a miniz inflater-state memory overwrite. The implementation is retained for redesign in 1.1.

- My Spools and My Filaments remain available on WT32 and in the optional browser. Production rejects Community API/import actions and installs no catalog callbacks; no startup download, verify, inflate, or catalog access runs.
- Failed browser NFC preview retains the reviewed physical spool. Retry Read uses the same spool and mode; Back to Review preserves values; Back to Select reloads the current source and clears stale rows. Busy state ends on completion or failure.
- Update Existing Station uses application-only A/B OTA, preserving LittleFS configuration, NVS, scale calibration and service/printer settings. Factory Install / Recovery explicitly erases configuration and calibration. Disabled Community is not seeded into factory LittleFS.
- Community is postponed, not fixed.

## 1.0.0-rc.2

- Standalone WT32 selection from My Spools, My Filaments, or Community; physical
  spool creation, import/reuse, exact writer review, and reassignment without a prior clear.
- Shared tag lifecycle: compatible blanks offer Assign; successful cleanup offers
  immediate reuse. Existing remote ownership and NFC safety fences remain mandatory.
- Full-screen text, numeric, URL and password input with 44 px keys, explicit acceptance,
  cancel, and repeat backspace. Bounded inventory pages and streaming gzip Community search.
- Explicit production version changes enforced in PR CI.

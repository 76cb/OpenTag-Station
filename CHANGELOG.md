# Changelog

What changed in each version of OpenTag Station, newest first.

## 1.1.0-beta.1

First step of 1.1: the station no longer assumes a printer.

**Printers are optional**
- Without a FilaBridge address the station does tags, weighing and Spoolman only. The touchscreen
  hides the **Assign** tab and button, and the Home screen keeps just **Weigh**, **Manage tag**
  and **Settings**.
- First-run setup skips the **Printer** step when no FilaBridge address is set, and counts its
  steps to match ("Step 5 of 7" instead of "Step 5 of 8"). The FilaBridge step is marked optional.
- Nothing changes for a station that already uses FilaBridge.

**Known limits**
- The touchscreen has room for five toolheads. A printer that reports more (for example a Core
  One with INDX, up to eight) can be assigned from the browser only.
- The browser interface still shows its printer sections when FilaBridge is not configured.

## 1.0.0

The first release. Everything below is in it.

**Identify and weigh**
- Place a spool with an OpenPrintTag on the station and it reads the tag and finds the spool in
  Spoolman. Spoolman stays the record of what you own. If Spoolman was unreachable, the station
  looks the spool up again by itself when Spoolman is back.
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
- Writing is one confirmation: pick the spool, check what will be written, **Write tag**. The
  station asks again only when the tag belongs to another spool or holds other data. The preview
  lists anything that does not fit on the tag (a long name or material, several colours).
- Reading and writing show their progress, and the spool's colour is shown on the result.
- With Spoolman 0.27 or later the station also uses Spoolman's own tag list: writing a tag links
  it to the spool there, clearing removes it, and a tag Spoolman links to a spool is recognised
  even if it holds no OpenPrintTag data. The two Spoolman extra fields are then optional. Each
  tag placed on the station is reported to Spoolman, so a Spoolman page paired with the station
  jumps to the spool.

**Printer**
- Assign the spool on the station to a toolhead (T1–T5) through FilaBridge. The station asks
  before it replaces a loaded spool or changes a printer that is printing, and checks afterwards
  that FilaBridge accepted the change.

**Station**
- A touchscreen with Home, Weigh, Assign, Tag and Settings views, and a web page with Dashboard,
  Inventory, Printer and Settings. Firmware updates, settings export and factory reset are in
  the browser only.
- First-time Wi-Fi setup on the touchscreen, or through an open setup network
  `OpenTag-Setup-XXXX` and a browser. A setup network you start yourself closes after 10
  minutes; on a station with an access token, the token is still needed over it.
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
- Saving weights needs Spoolman 0.26 or 0.27. Assigning toolheads needs FilaBridge 1.2.1 or 1.2.2.
- The Community filament catalog is not part of this release.

The manual is at <https://76cb.github.io/OpenTag-Station-Docs/>.

Each release has these files: `opentag-station-<version>-application.bin` (update from the
browser), `opentag-station-factory.bin` (USB installer image), `manifest.json`,
`build-metadata.json` and `SHA256SUMS`.

---

The release candidates below were development builds made for testing before 1.0.0.

## 1.0.0-rc.23

The fixes for 1.0 found when the open issues were re-checked (#84). This candidate also contains
rc.22 (touchscreen button colours), which was not published separately.

**Safety**
- The setup network you start from the browser now closes by itself after 10 minutes, and
  **Stop setup network** closes it at once. The touchscreen Settings line shows "closes in N min".
  It can't be stopped while it is the only way to reach a station that has no working Wi-Fi.
- Tagging many spools in a row from the browser no longer ends in "Idempotency ledger is full":
  browsing and searching the spool list no longer use up request slots, and slots of finished
  requests are reused.

**Finding the spool**
- A tagged spool is looked up in Spoolman as soon as it is placed. Before, the lookup waited for a
  steady weight, so an uncalibrated scale (or one waiting for a tare) meant tags were never
  identified.
- If Spoolman or Wi-Fi was down when the spool was placed, the station looks the spool up again
  by itself when Spoolman is back (up to three times, spaced further apart each time). You no
  longer need to lift and replace the spool. If Spoolman answers but the lookup fails, both
  interfaces say "The Spoolman lookup failed: …" and still let you link the tag by hand.
- While Spoolman is unreachable or the lookup is running, the touchscreen Manage tag page and the
  browser dashboard say so ("Spoolman is not reachable…", "Looking up this spool in Spoolman…")
  instead of calling the tag "not linked".

**Writing tags**
- The preview lists anything that will not be written to the tag: a name, vendor, material or
  article number that is too long, a material type the tag can't store (such as "PLA Matte" or
  "PLA+"), or several colours. Browser: "Not written to the tag:"; touchscreen: "Not written: …".
  Before, these were left out without a word.
- A single tag-reader error no longer blocks every write and erase until a restart. A write fails
  only if the reader reports an error during that write: "The tag reader reported an error during
  this step. Try again."
- Spool lists with large entries no longer fail a whole page: the station fetches fewer rows at a
  time, and both interfaces page correctly through shorter pages.
- Tag actions are named after what they do. "Assign" is only used for printer toolheads:
  **WRITE TAG** / **Write tag** for a blank tag, **USE FOR ANOTHER SPOOL** for a linked tag,
  **Write tag for a spool** in the browser.

**Scale and weighing**
- Tare is refused when the platform is not empty: "The platform is not empty (reads X g). Remove
  everything, then tare. If it is empty, tare again within 15 seconds." A second tare within 15
  seconds is accepted, for the case where the zero itself has moved.
- After a power glitch to the scale, the station reconnects to it by itself instead of needing a
  restart.
- Weigh can't be started while a weight is still being saved to Spoolman; the button reads
  "Saving to Spoolman…" and a refused weigh says why.
- The touchscreen Weigh page only shows a result for the spool that is on the station. After a
  swap it no longer offers to save the previous spool's weight.
- Tare and calibration results, including failures, stay on the touchscreen until your next tap.

**Touchscreen**
- Main actions are mint, erasing is red and other choices are grey on every screen (rc.22),
  including the setup guide, About and the toolhead question.
- Setup: **Next** waits for the save and stays on the step with the reason if it failed. The printer
  list shows the printer that is saved.
- The brightness set in Settings is kept after a restart, and display settings saved in the
  browser apply at once. About no longer has its own brightness slider.
- About ten messages that described the software's internals are rewritten in plain words, and
  common errors on the tag failure page are explained.

## 1.0.0-rc.22

**Touchscreen button colours**
- The main action on each touchscreen screen is now drawn in the product's mint colour:
  **WRITE TAG**, **ASSIGN TAG**, **CONTINUE**, **WEIGH** on TAG READY, **CHOOSE EXISTING SPOOL**,
  **FINISH WRITING** and the like. Erasing stays red. **BACK**, **DONE** and other secondary
  choices stay grey. Before, every tag button except the red ones was grey.
- The setup guide, **About this station** and the toolhead confirmation no longer use LVGL's
  default blue. In setup, **Next**/**Finish**, **Save** and (until the scale is calibrated)
  **CALIBRATE NOW** are mint; **Back**, **Home** and **Scan** are grey. In the toolhead question,
  **Assign** is mint, and **Replace** or **Assign anyway** (during an active print) is red.

## 1.0.0-rc.21

**Writing and erasing tags: fewer steps, one reminder**
- Touchscreen: tapping a spool in **MY SPOOLS** reads the tag and opens the review page straight
  away; "Use this spool?" is gone. After **CREATE SPOOL** the review page opens by itself.
- Touchscreen: "Move this tag?" is now the review page itself, not an extra page before it. It also
  appears when the station's preview finds that the tag belongs to another spool, and a tag that
  holds another spool's data shows "This tag holds another spool's data. Writing replaces it."
  Before, the touchscreen showed the generic text in those cases.
- Browser: **Continue** reads the tag at once and shows the preview; the separate Review step and
  its **Preview tag** click are gone. The steps are **Select → Preview → Write → Done**; an **Edit**
  step appears only while editing. **Write tag** asks no further question for an ordinary write;
  the extra question (and a red button) remains when the tag belongs to another spool, holds other
  data, or has an interrupted write. "Create this physical spool in Spoolman?" is gone.
- "Keep the tag on the reader." appears once, and only while the tag is read, written, checked or
  erased. While only Spoolman is updated the browser now says "The tag is written. Saving the link
  in Spoolman…" (or "The tag is erased. Removing the link in Spoolman…"); before, it still asked to
  keep the tag on the reader.
- After a finished clear in the browser, **Done** closes the dialog instead of reopening Manage tag.

**Reading progress**
- The full read before a write or clear reports its progress (160 block reads, both passes).
  Touchscreen and browser show a progress bar; the background read shows a spinner with
  "Reading tag" / "Hold the spool still."
- Two tags on the reader now show "Two tags on the reader. Remove one." on the touchscreen and
  "Tag not read" in the browser, instead of "Reading tag…" that never ends.
- Progress messages from the station are plain: "Reading tag", "Checking tag", "Writing tag",
  "Erasing tag", "Verifying tag".

**Spool colour**
- The touchscreen review and **TAG READY** pages show a ring in the spool's colour; the browser
  preview and result show a spool picture in that colour.
- Home on both interfaces uses the Spoolman spool's colour when the tag itself has none.
  The spool API now includes `primary_color`.

**Setup and About**
- The setup network `OpenTag-Setup-XXXX` is open; there is no password to read off the screen.
  When it comes back on a station that already has Wi-Fi saved, a station with an access token
  still requires the token over it.
- The first setup page no longer shows "Configuration schema 3". It says "Tap Next to start", or,
  on a station that is set up, that you can review a step or go Home.
- **About this station** is one scrolling column in a larger font, one value per line, in the
  sections Network, Scale and screen, System and Memory.

**Scale**
- Every tare, including the zero step of a calibration, first re-runs the scale converter's own
  offset calibration, so the zero matches the converter's current temperature. If that tare then
  does not finish, weighing asks for a new tare instead of using a zero that no longer fits.
- Both calibration screens advise calibrating after the station has been on for about 15 minutes.
  Troubleshooting explains warm-up drift and load-cell creep.

**Releases**
- Every version merged to `main` is published automatically as a GitHub release (a pre-release
  for candidates) with the update file, and the USB installer is built from that same release.

## 1.0.0-rc.20

**Spoolman 0.27 tag support**
- Writing a tag now also links it to the spool in Spoolman's own tag list (Spoolman 0.27 or
  later). Moving a tag removes it from the previous spool first, and clearing a tag removes it.
  These steps are part of saving the link in Spoolman, so the existing retry and recovery
  prompts cover them.
- The station finds the spool through Spoolman's tag list first. A tag Spoolman links to a spool
  is recognised even if it holds no OpenPrintTag data, for example a blank tag you linked in
  Spoolman yourself: Home shows its spool, and Weigh and Assign work.
- Tag IDs are matched in both byte orders, because some phones report them reversed.
- On Spoolman 0.27 or later the two extra fields are optional. When they exist the station still
  fills them; when they don't, it uses Spoolman's tag list only and no longer warns about them.
- Each tag placed on the station is reported to Spoolman, so a Spoolman page paired with the
  station (Spoolman: Settings > Tag Scanning) jumps to its spool.
- A tag that Spoolman links to a filament, or to two spools, is refused before anything is written.

**Fixes**
- Saving weights works with every Spoolman 0.26 and 0.27 release, not only 0.26.1.
- After start-up or a Spoolman outage the station learns Spoolman's version again right away.
  Before, weight saving could stay off for up to five minutes.
- A tag at the edge of the reader's range no longer flickers: one missed check is tolerated, and
  a read that fails part-way is retried after half a second instead of showing an error and
  waiting five seconds.

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

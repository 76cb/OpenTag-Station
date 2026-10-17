# Tag writer, clear and weight saving

The rules for every operation that changes a tag or Spoolman. User-facing steps are in the manual
(<https://76cb.github.io/OpenTag-Station-Docs/daily-use/manage-tags/>); the field mapping is at
<https://76cb.github.io/OpenTag-Station-Docs/reference/openprinttag/>.

## Components

| Component | File | Role |
|---|---|---|
| `TagWriterService` | `src/services/tag_writer_service.cpp` | Catalog, preview, write, link, clear, unlink, recovery. One instance, on `opentag-backend`. |
| `OpenPrintTagWriter` | `src/nfc/openprinttag_writer.cpp` | Physical read, plan and block-by-block execution |
| `map_spoolman()` | `src/nfc/formats/openprinttag/spoolman_mapping.cpp` | Builds the target image from a Spoolman spool |
| `I2cReader::commit_openprinttag_block()` | `src/hardware/nfc/st25r3916b/openprinttag_write_binding.cpp` | The only caller of the RFAL write command |
| `WriterJournal` | `src/platform/storage/writer_journal.cpp`, `src/nfc/writer_journal_codec.hpp` | Recovery record |
| `WeighSync` | `src/services/weigh_sync.cpp` | Weigh sessions and weight saving |

Both front ends send the same JSON command: the touchscreen through
`BackendWorker::submit_writer()`, the browser through `POST /api/v1/tag-writer`. State is
published as a snapshot (`GET /api/v1/tag-writer`) with a `phase` field.

Actions: `catalog`, `preview`, `write`, `retry_association`, `update_spool`, `update_filament`,
`create_spool`, `clear_preview`, `clear`, `retry_unlink`, `discard_recovery`.
The `community_*`, `import_preview` and `import` actions are rejected while the Community
catalog is disabled.

Phases: `catalog`, `searching`, `spool_selected`, `loading_spool`, `reading`, `preview`,
`editing`, `updated`, `validating`, `writing`, `verifying`, `decoding`, `associating`,
`association_pending`, `complete`, `write_recovery`, `clear_preview`, `clearing`,
`clear_recovery`, `unlinking`, `unlink_pending`, `cleared`, `recovery_discarded`, `failed`.

## Preconditions for any physical operation

`OpenPrintTagWriter::read()` refuses unless all of these hold:

- Exactly one NFC-V tag is in the field.
- UID starts `E0 04`, block size 4, block count 80.
- No protection bit on blocks 0–77.
- Two complete reads of all 80 blocks are identical.
- The content is blank (bytes 0–311 all zero), or decodes as OpenPrintTag, or is a partly written
  tag that matches the recovery record block by block.
- The decoded tag does not declare write protection.

Before and during execution `fence()` rechecks, before every block: 120 s deadline, same tag
generation, reader healthy, zero bus errors, exactly the same UID, same geometry, same system
information.

`BackendWorker::process_writer()` additionally refuses `preview`, `write`, `clear_preview` and
`clear` until NFC is initialised, and drops a `write` or `clear` that waited more than 15 s in
the queue (`Write confirmation expired in queue`).

## Write

1. **Preview** (`preview`): read the tag, load the spool from Spoolman, check that both Spool
   extra fields exist, choose the instance UUID (the spool's existing one, else the tag's if the
   spool already names this UID, else a new random UUID), check that no other spool uses that
   UUID, find the current owner of the UID, build the target image, plan the changed blocks.
   `mode` is `initialize` (blank tag), `rewrite`, or `update` (consumed weight only).
2. **Confirm** (`write`): the request must repeat `uid`, `generation`, `spool_id`,
   `previous_spool_id` and `target_checksum` from the preview. A mismatch, changed Spoolman
   settings, or a recovery record for a different tag refuses the write.
3. The recovery record is saved and read back. If that fails, nothing is written
   (`Recovery journal could not be verified; no tag bytes written`).
4. **Execute**: read the whole tag and compare with the previewed bytes. For each planned block:
   read it, compare with the original, write it once, read it back, compare with the target.
   Only blocks that differ are written. Order: ordinary blocks, then the blocks holding the
   instance UUID, then block 0. Bytes 312–319 must be equal in original and target.
5. **Verify**: read all 80 blocks, compare with the target, decode, and check instance UUID and
   consumed weight against the plan.
6. **Link** (`associating`): if the UID belonged to another spool, clear `nfc_uid` there first.
   Then `GET` the target spool, `PATCH` both extra fields, `GET` again and compare, check that
   the UUID and the UID each have exactly one owner, store the mapping in the local settings,
   delete the recovery record. Phase `complete`.

If step 4 is refused before any block was written, the recovery record is deleted again.
If linking fails, the phase is `association_pending`; the tag is correct and only
`retry_association` (no NFC write) or `discard_recovery` is accepted.

## Clear

1. **Preview** (`clear_preview`): same physical read. The target is the original with bytes
   0–311 zeroed. Requires a configured Spoolman URL and two distinct field keys.
2. **Confirm** (`clear`): the request must repeat `uid`, `generation`, `current_checksum` and
   `target_checksum`.
3. Save the recovery record, then execute as for a write. Payload blocks first, block 0 last.
   Final check: all 312 bytes zero.
4. **Unlink** (`unlinking`), in this order: save the record with "cleanup pending"; check the
   Spool extra fields exist; find the spool that owns the UID (or, failing that, the cleared
   instance UUID); refuse if that spool now names another tag or another UUID; save the record
   with the owner bound; `PATCH` both extra fields to `null`; `GET` and compare; check that
   neither the UID nor the UUID has an owner; remove the local mapping; delete the record.
   Phase `cleared`.

The physical result is recorded before the first Spoolman request. If Spoolman fails, the phase
is `unlink_pending` and the message says the tag is already cleared
(`The tag is cleared. Removing its link in Spoolman is still pending.`). Only `retry_unlink`,
`clear_preview` or `discard_recovery` is accepted. The spool record, its weights and its other
fields are not touched.

## Recovery record

| Property | Value |
|---|---|
| Path | `/writer-recovery.bin` on LittleFS |
| Written as | `/writer-recovery.new`, read back byte by byte, then renamed |
| Size, format | 836 bytes, magic `OPTWR003`, checksum in the last 4 bytes |
| Content | UID, system information, 80 protection bytes, original 320 bytes, target 320 bytes, spool ID, Spoolman settings identity, previous owner spool ID, operation (write/clear), cleanup flags, cleared instance UUID |
| Also readable | 813-byte `OPTWR001` and 817-byte `OPTWR002` records, always treated as a write |
| Deleted by | Successful link or unlink, `discard_recovery`, a refusal before the first block, factory reset, USB install |

There is one record. A write for another tag is refused while it exists.
It holds no credentials.

On the next preview the tag is compared with the record block by block, before any decoding:

| Tag content | Result |
|---|---|
| Equals the original | Normal preview; the operation starts over |
| Equals the target (write) | `association_pending`; only the Spoolman link is retried |
| Equals the target (clear) | `unlink_pending`; only the unlink is retried |
| Every block equals original or target | Recovery: the remaining blocks are written |
| Any block matches neither | Refused: `Torn/unknown block differs from both journal images; refusing rewrite` |
| Geometry, system information or protection differ | Refused |

At start-up and after a failed operation `restore_cleanup()` publishes the matching prompt:
`write_recovery`, `clear_recovery` or `unlink_pending`. A clear whose Spoolman settings changed
since it started can only be skipped or finished after restoring the settings.

Recovery actions as shown to the user:

| State | Touchscreen | Browser |
|---|---|---|
| Write interrupted | **FINISH WRITING**, **SKIP RECOVERY** | **Resume tag workflow** (Manage tag dialog) |
| Clear interrupted | **FINISH CLEARING**, **SKIP RECOVERY** | Clear dialog |
| Link not saved | **TRY SAVING LINK AGAIN**, **SKIP RECOVERY** | **Retry Spoolman link**, **Skip linking** |
| Unlink not finished | **TRY UNLINKING AGAIN**, **SKIP RECOVERY** | **Retry Spoolman cleanup**, **Skip cleanup** |

The skip buttons send `discard_recovery` with `"confirm": "SKIP RECOVERY"`. The browser offers
a skip only for the two Spoolman-only states.
`discard_recovery` deletes the record and changes neither the tag nor Spoolman.

## Weigh and save

- A weight can be saved only from a session started by an explicit Weigh (the scale queue's
  `weigh_started` hook). Tare and calibration never create a session.
- `BackendWorker::begin_weigh()` captures spool ID, tag UID, spool generation, settings revision,
  Spoolman used and remaining weight, empty-spool weight and the tolerances.
- `WeighSync::capture()` sets phase `ready` only for a stable, valid reading of a linked spool
  with known empty weight and remaining weight; otherwise `unavailable` with the reason.
- `reconciliation.auto_update_after_weigh` (default `false`) decides whether the backend saves a
  `ready` session by itself. Otherwise the user presses **UPDATE SPOOLMAN** / **Update Spoolman**.
- `WeighSync::update()` marks the session consumed before the first request. A session is
  attempted once; after a failure or conflict a new Weigh is required.
- No write when the difference is within `normal_tolerance_grams` (default 5 g): phase
  `unchanged`.
- Before the `PATCH`, `BackendWorker::weight_fence()` checks that the settings revision, spool,
  UID and generation are unchanged and that exactly that tag is still on the reader (inventory
  only; no decode, no NFC write).
- `SpoolmanAdapter::set_remaining_weight()`: `GET`, refuse if `used_weight` moved by more than
  0.05 g since capture, `PATCH`, `GET` and compare within 0.25 g.
- Saving a weight never writes to the tag.

## NFC write allow-list

`tools/check_production_nfc.py` runs in CI before the build (source) and after it (`--elf`).

Source checks:

- The firmware environment defines `-DOPENTAG_ENABLE_ST25R3916B=1` and excludes the legacy NFC
  sources.
- Outside `src/diagnostics` and `src/platform/rfal`, only read, inventory, system-information
  and field on/off RFAL calls appear. `rfalNfcvPollerWriteSingleBlock` appears only in
  `src/hardware/nfc/st25r3916b/openprinttag_write_binding.cpp`.
- `commit_openprinttag_block(` appears only in the writer, the reader header and the binding.
  `writer_.execute(` appears only in `tag_writer_service.cpp`; `writer_->process(` only in
  `tag_writer_commands.cpp`.
- Application, UI, web, services and hardware code do not call the blank-image generator, the
  consumed-weight encoder or `Codec::decode` directly.
- The API route `/api/v1/tag-writer` requires `uid`, `generation` and `target_checksum` for a
  write and has no raw block command.
- `NfcWorker` creates no task; `BackendWorker::run()` is the only caller of `poll_nfc()` and of
  `process_writer()`.

Binary checks (`--elf`):

- No lock, protect, password, privacy, AFI, DSFID, EAS, write-multiple or extended-write RFAL
  symbol is linked.
- The only object file that references `rfalNfcvPollerWriteSingleBlock` is
  `openprinttag_write_binding.cpp.o`.

Related CI checks: `tools/verify_openprinttag_initializer_reference.py` and
`tools/verify_openprinttag_writer_reference.py` compare the station's images with the pinned
upstream specification tools; `pio test --environment native-writer-sanitized` runs the host
suites, including `test_tag_writer` and `test_weigh_sync`, under AddressSanitizer and
UndefinedBehaviorSanitizer.

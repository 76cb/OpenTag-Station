# Architecture

How the firmware is put together. One board is supported: WT32-SC01 Plus (ESP32-S3), Arduino
framework, C++17. Stack sizes and memory limits are in [runtime-memory.md](runtime-memory.md);
the tag write engine is in [tag-writer.md](tag-writer.md).

## Layers

```text
Touchscreen (src/ui)            Browser page + REST API (src/web)
          \                          /
   Application: tasks, queues, operation registry (src/application)
                        |
   Services: workflow, identity, writer, weigh, scale (src/services)
      /                 |                    \
 Integrations       NFC (src/nfc)          OTA (src/ota)
 Spoolman, FilaBridge   |                      |
      |             Hardware drivers       Platform ports
 Network (HTTP)     (src/hardware)         (src/platform)
```

Rules that hold across the tree:

- Only the UI task calls LVGL. Only the backend task talks to the NFC reader and makes HTTP
  requests to Spoolman and FilaBridge.
- The touchscreen and the web server never call a service that blocks. They put a command on a
  queue and render snapshots.
- The OpenPrintTag codec does not depend on the NFC driver. Spoolman and FilaBridge JSON is
  handled only inside their adapters.
- Portable code (services, codec, API router, update manager) builds on the host for tests.
  Files that need the ESP32 are guarded and excluded from the `native` environments.

## Source tree

| Directory | Content |
|---|---|
| `src/main.cpp` | Arduino `setup()`/`loop()`; sets the loop task stack |
| `src/application/` | `Application`, the workers (`BackendWorker`, `ConfigurationWorker`, `DeviceControlWorker`, `OtaWorker`, `NfcWorker`), `ScaleCommandQueue`, `OperationRegistry`, `BootHealthPolicy`, `DeviceLifecycleGate` |
| `src/boards/` | Pins, buses and addresses of the WT32-SC01 Plus (`wt32_sc01_plus_rev_a.hpp`) |
| `src/config/` | `ConfigurationService` (settings document, validation, migration), `product_features.hpp` |
| `src/core/` | `Result`, `Error`, `ByteView` |
| `src/diagnostics/`, `src/logging/` | Health snapshot, build information, in-memory log |
| `src/domain/`, `src/events/` | Plain data types: spool, printer, weight, identity |
| `src/hardware/display/` | LovyanGFX display and touch device |
| `src/hardware/nfc/st25r3916b/` | `I2cReader` (ST25R3916B over `Wire1`) and the single block-write binding |
| `src/hardware/scale/` | `Nau7802Device` |
| `src/integrations/spoolman/`, `src/integrations/filabridge/` | `SpoolmanAdapter`, `FilaBridgeAdapter` |
| `src/network/` | `WifiService`, `HttpTransport`, DNS and connect with deadlines, PSRAM response bodies and JSON |
| `src/nfc/` | `ReadOnlyService` (polling and reading), `OpenPrintTagWriter`, recovery record codec |
| `src/nfc/formats/openprinttag/` | Codec, CBOR reader, blank-image generator, Spoolman-to-tag mapping |
| `src/nfc/protocols/nfcv/` | NFC-V tag model and read protocol |
| `src/ota/` | `UpdateManager` (portable update state machine) |
| `src/platform/ota/`, `src/platform/storage/` | ESP32 OTA port, firmware manifest, update record store, LittleFS/NVS storage, recovery file |
| `src/services/` | `StationWorkflow`, `SpoolIdentityResolver`, `ToolheadAssignmentService`, `TagWriterService`, `WeighSync`, `WeightReconciler`, `ScaleService`, `FirstRunSetup`, `MaterialCompatibilityService` |
| `src/ui/` | `UiService` (LVGL pages), `TagFlow` (tag screens model, `tag_flow.hpp`), `TouchInputScreen` (keyboard) |
| `src/web/` | `LocalWebServer` (ESP-IDF httpd), `api::Router`, `ApplicationApiContext`, `IdempotencyLedger`, embedded browser assets |

Outside `src/`: `boards/` (PlatformIO board definition), `partitions.csv`, `test/` (host
suites), `tools/` (build, check and release scripts), `third_party/ELECHOUSE_ST25R3916/` (the NFC
libraries, linked as `lib_deps`), `web-flasher/` (installer page), `community/`
(see [community-catalog.md](community-catalog.md)).

## Tasks

| Task name | Created in | Owns | Core |
|---|---|---|---|
| `opentag-ui` | `application.cpp` | LVGL, touch input, all drawing | 1 |
| `opentag-config` | `configuration_worker.cpp` | Writing the settings document | 0 |
| `opentag-scale` | `application.cpp` | NAU7802 sampling, tare, calibration, weigh | 0 |
| `opentag-network` | `application.cpp` | Wi-Fi, setup access point, mDNS, starting the web server | 0 |
| `opentag-backend` | `backend_worker.cpp` | NFC reader, Spoolman and FilaBridge HTTP, tag writer, weight saving, assignment | 0 |
| `opentag-control` | `device_control_worker.cpp` | Reboot and factory reset | 0 |
| `opentag-ota` | `ota_worker.cpp` | Writing and validating an update, restart, rollback decision | 0 |
| httpd | ESP-IDF, configured in `local_web_server.cpp` | HTTP and WebSocket requests | not pinned by this code |
| Arduino loop | `main.cpp` | Boot health check, periodic diagnostics on serial | Arduino default |

`NfcWorker` is not a task. `BackendWorker::run()` calls it between commands, so NFC access and
backend HTTP never overlap. While a slow HTTP request runs, tag polling waits.

The NFC reader is not started until Wi-Fi is configured and connected and the setup access point
and its grace period have ended (`src/nfc/worker_startup.hpp`).

## How work crosses tasks

- A caller (touchscreen callback or HTTP handler) submits a command to the owning worker's
  FreeRTOS queue and gets an operation ID from `OperationRegistry`.
- The worker marks the operation running, then succeeded or failed. The browser polls
  `GET /api/v1/operations/{id}`.
- State flows back as snapshots that the UI task and httpd copy or serialize to JSON. `/api/v1/events` (WebSocket) pushes scale and update events.
- A queued write, clear or assignment that waited longer than 15 s is dropped
  (`BackendWorker::destructive_command_expiry_ms`).
- `DeviceLifecycleGate` lets only one of reboot, factory reset, firmware update and new-firmware
  validation run at a time. The separate rule that reboot, factory reset and update restart are
  refused while a tag or Spoolman operation is queued or running is enforced in
  `ApplicationApiContext::submit_fresh()`.

## Data flow

**Read a tag.** `BackendWorker::poll_nfc()` → `NfcWorker::poll()` → `ReadOnlyService`: inventory,
two full reads that must match, `Codec::decode`. The result is handed to `StationWorkflow`, which
uses `SpoolIdentityResolver` and `SpoolmanAdapter` to find the spool. `StationWorkflow` stages:
`awaiting_spool`, `waiting_for_stable_weight`, `resolving_spool`, `spool_resolution_unavailable`,
`spool_not_found`, `spool_selection_required`, `spool_ready`, `assignment_complete`.

**Weigh.** Touchscreen or `POST` to the API → `ScaleCommandQueue` → scale task (`ScaleService`
waits for a stable reading). The queue's `weigh_started`/`weigh_finished` hooks call
`BackendWorker::begin_weigh()`/`complete_weigh()`, which fill a `WeighSync` session. Saving is a
separate command, `submit_weight_update()`, handled on the backend task by
`WeighSync::update()` → `SpoolmanAdapter::set_remaining_weight()`.

**Write or clear a tag.** `BackendWorker::submit_writer(json)` from `UiService` or from
`POST /api/v1/tag-writer` → `process_writer()` → `TagWriterService::process()` →
`OpenPrintTagWriter` → `I2cReader::commit_openprinttag_block()`. The Spoolman link is saved or
removed by `TagWriterService` over the same `SpoolmanAdapter`. The recovery record is stored by
`src/platform/storage/writer_journal.cpp`.

**Assign a toolhead.** `BackendWorker::submit_assignment_operation()` →
`StationWorkflow::assign()` → `ToolheadAssignmentService` → `FilaBridgeAdapter`
(`POST /api/map_toolhead`), followed by a fresh read of the printer state that must show the
requested spool.

**Update firmware.** httpd streams the upload to `OtaWorker`, which drives `UpdateManager`
against `Esp32OtaPlatform`. After the restart `BootHealthPolicy` confirms the new image after
30 s or rolls back. Details: <https://76cb.github.io/OpenTag-Station-Docs/reference/ota/>.

## Browser assets

The browser page is embedded in the firmware; nothing is loaded from the internet.

| File | Role |
|---|---|
| `src/web/ui/product.css`, `src/web/ui/product.js` | Hand-edited presentation sources |
| `src/web/web_assets.cpp` | HTML, CSS and JavaScript as embedded strings; the product CSS/JS blocks are generated by `python tools/generate_product_ui.py` |
| `src/web/writer_assets.cpp`, `src/web/writer_layout.inc` | Tag writer dialog script and layout |
| `tools/precompress_web_assets.py` | Pre-build step that gzips the CSS and JavaScript |

CI runs `python tools/generate_product_ui.py --check` and `python tools/check_web_assets.py`.

## Build environments

| Environment | Purpose |
|---|---|
| `wt32-sc01-plus` | The firmware. Default environment. `-DOPENTAG_ENABLE_ST25R3916B=1`, `-fstack-usage` |
| `native` | Host tests of the portable sources listed in its `build_src_filter` |
| `native-writer-sanitized` | `native` with `-fsanitize=address,undefined` |
| `native-community` | `native` with `-DOPENTAG_ENABLE_COMMUNITY=1` and a `test_filter` |

Extra scripts of the firmware environment: `tools/build_metadata.py` (version and commit
defines, `build-metadata.json`), `tools/precompress_web_assets.py`, `tools/web_flasher_target.py`
(the `web-flasher` target). The firmware build excludes the legacy files
`hardware/nfc/st25r3916b/frontend_backend.cpp`, `hardware/nfc/st25r3916b/service.cpp` and
`diagnostics/shared_i2c_diagnostic.cpp`; they are compiled only for host tests.

Flash layout (`partitions.csv`): NVS, OTA data, two application slots of 0x500000 bytes each, a
LittleFS partition of 0x5D0000 bytes and a core-dump partition.

## Where to read more

- REST API: <https://76cb.github.io/OpenTag-Station-Docs/reference/api/>
- Configuration keys: <https://76cb.github.io/OpenTag-Station-Docs/reference/configuration/>
- Tag format and field mapping: <https://76cb.github.io/OpenTag-Station-Docs/reference/openprinttag/>
- Wiring and pins: <https://76cb.github.io/OpenTag-Station-Docs/hardware/wiring/>

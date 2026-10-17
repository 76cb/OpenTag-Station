# Tasks, stacks and memory limits

The limits the firmware enforces at run time, and the CI tools that keep them from regressing.
Every number below is a constant in the named file. For what each task does, see
[architecture.md](architecture.md).

## Tasks

| Task | Stack (bytes) | Priority | Core | Command queue | Defined in |
|---|---:|---:|---:|---|---|
| `opentag-ui` | 12,288 | 2 | 1 | – | `src/application/application.cpp` |
| `opentag-config` | 8,192 | 1 | 0 | 8 | `src/application/configuration_worker.cpp` |
| `opentag-scale` | 6,144 | 1 | 0 | 4 | `src/application/application.cpp`, `scale_command_queue.hpp` |
| `opentag-network` | 16,384 | 1 | 0 | – | `src/application/application.cpp` |
| `opentag-backend` | 16,384 | 1 | 0 | 12 | `src/application/backend_worker.hpp`, `.cpp` |
| `opentag-control` | 4,096 | 1 | 0 | 1 | `src/application/device_control_worker.cpp` |
| `opentag-ota` | 24,576 | 1 | 0 | 4 | `src/application/ota_worker.hpp`, `.cpp` |
| httpd (ESP-IDF) | 12,288 | – | – | – | `src/web/local_web_server.hpp` |
| Arduino loop | 16,384 | – | – | – | `src/main.cpp` |

There is no NFC task. `NfcWorker` runs on `opentag-backend`
(`src/application/nfc_worker.hpp`); CI fails if `nfc_worker.cpp` creates a task.

## Backend task scheduling

`BackendWorker::run()` is one loop: wait up to 250 ms for a command, poll NFC, run the command,
poll NFC, run a due health check. NFC and HTTP never run at the same time, so a slow request
delays tag polling.

| Item | Value | Source |
|---|---|---|
| NFC poll interval | 500 ms; 5,000 ms while the reader is in error | `src/nfc/read_only_service.cpp` |
| Full tag read deadline | 15 s | `src/nfc/read_only_service.cpp` |
| Physical write or clear deadline | 120 s | `src/nfc/openprinttag_writer.cpp` |
| Spoolman / FilaBridge health check | every 30 s | `BackendWorker::probe_interval_ms` |
| Full capability discovery | every 300 s, and after a settings change | `backend_worker.cpp` |
| One backend operation, all requests together | 20 s | `src/network/operation_budget.hpp` |
| DNS wait | at most 15 s | `src/network/bounded_dns.hpp` |
| Connect / read timeout, Spoolman and FilaBridge | 5 s / 7 s | the two adapters |
| Queued write, clear or assignment expires | 15 s | `BackendWorker::destructive_command_expiry_ms` |

No request is retried automatically.

## Heap and PSRAM

| Rule | Value | Source |
|---|---|---|
| Backend request refused unless free internal heap / largest block is at least | 18,000 / 6,144 bytes (plain HTTP); 48,000 / 20,000 bytes (HTTPS) | `backend_admitted()` in `src/network/backend_memory.hpp` |
| Backend response bodies | PSRAM only, no fallback to internal heap; start at 512 bytes, default limit 65,536 | `src/network/backend_memory.hpp` |
| Backend JSON documents | PSRAM allocator, at most 196,608 bytes | `src/network/backend_json.hpp` |
| LVGL memory pool | 64 KiB, allocated from PSRAM by `opentag_lvgl_pool` | `src/ui/lv_conf.h`, `src/ui/lvgl_memory.cpp` |
| Writer plan, recovery record buffer, tag images | PSRAM (`network::make_external`) | `src/services/tag_writer_service.cpp`, `src/platform/storage/writer_journal.cpp` |

A refused request reports `Backend deferred: insufficient internal memory; local controls remain
available`. The touchscreen, scale and web server keep working.

Response limits per request:

| Request | Limit (bytes) |
|---|---:|
| Spoolman `/health` | 1,024 |
| Spoolman `/info` | 4,096 |
| Spoolman spool lookups (`find_spools`) | 65,536 |
| Spoolman catalog pages, edits and every other tag-writer request | 24,576 |
| FilaBridge `/api/printers` | 32,768 |
| FilaBridge `/api/map_toolhead` | 4,096 |

Spoolman parsing limits: page size 8, at most 64 extra-field definitions, extra-field key at most
64 characters, value at most 1,024 bytes, 2,048 bytes of extra fields per spool
(`src/integrations/spoolman/spoolman_adapter.cpp`).

## Web server and API

| Limit | Value | Source |
|---|---|---|
| Open sockets / WebSocket clients | 5 / 2 | `src/web/local_web_server.hpp` |
| WebSocket message | 4,096 bytes | `local_web_server.hpp` |
| WebSocket events | scale and update at most every 500 ms; heartbeat every 15 s | `local_web_server.hpp` |
| Request body | 16,384 bytes | `src/web/api_router.hpp` |
| Snapshot JSON / response body | 24,576 / 32,768 bytes | `api_router.hpp` |
| Request path | 256 bytes | `api_router.hpp` |
| Request headers | 16 headers, 1,024 bytes | `api_router.hpp` |
| Firmware upload | 0x500000 bytes (5 MiB), written in 4,096-byte chunks | `api_router.hpp`, `src/ota/update_manager.hpp` |
| Tag writer snapshot / view | 24,576 / 24,000 bytes | `src/services/tag_writer_service.hpp` |
| Idempotency keys remembered | 32, for 10 minutes | `src/web/idempotency_ledger.hpp` |
| Operation history | 24 records, message at most 192 bytes | `src/application/operation_registry.hpp` |
| Log history | 32 entries, message at most 192 bytes | `src/logging/bounded_log.hpp` |

## Browser asset sizes

Checked at compile time by `static_assert`.

| Asset | Limit | Source |
|---|---|---|
| HTML | 36 KiB | `src/web/web_assets.hpp` |
| CSS | 28 KiB | `web_assets.hpp` |
| JavaScript | 160 KiB | `web_assets.hpp` |
| HTML + CSS + JavaScript | 224 KiB | `web_assets.hpp` |
| Tag writer JavaScript | 36 KiB | `src/web/writer_assets.cpp` |

## CI checks

All run in the `firmware` job after `pio run --environment wt32-sc01-plus`. The firmware is built
with `-fstack-usage`; the stack tools read the compiler's `.su` files.

| Tool | Fails when |
|---|---|
| `tools/check_production_memory.py` | `.dram0.data` + `.dram0.bss` + `.noinit` exceed 120,000 bytes; the LVGL internal pool symbol `work_mem_int` is linked; `opentag_lvgl_pool` is missing; Community catalog code is linked while the feature is off |
| `tools/analyze_stack_usage.py` | No `.su` files exist. Otherwise it only prints the largest frames. |
| `tools/check_production_nfc_stack_usage.py` | The deepest modelled call chain on `opentag-backend` (tag read and decode, writer, clear, Spoolman and FilaBridge requests, settings and recovery-file writes, weight saving) leaves less than `BackendWorker::safety_bytes` (4,096) |
| `tools/check_production_http_stack_usage.py` | An ordinary API route leaves less than 4,096 bytes on httpd after a 2,048-byte allowance, or the upload route less than 2,048 |
| `tools/check_touch_stack_usage.py` | The tag screens or the keyboard leave less than 4,096 bytes on `opentag-ui` |
| `tools/check_production_nfc.py` | See [tag-writer.md](tag-writer.md) |

`local_web_server.hpp` also asserts at compile time that the httpd stack minus a worst-case use
of 9,344 bytes leaves at least 2,048.

These checks add up compiler frame sizes along call chains written into the tools. They are
regression guards. They do not measure a running station; new call paths must be added to the
tool that covers their task.

## Reading memory on a running station

Serial output at 115200 baud:

| Line starts with | Printed | Content |
|---|---|---|
| `health uptime=` | every 30 s | free and minimum heap, largest block, PSRAM, scale and Wi-Fi state |
| `stack_margin phase=` | after start-up | free stack per task |
| `NFC owner=opentag-backend` | every 5 s | reader state, free backend stack, bus error count |
| `BACKEND phase=` | around each backend request | internal heap and PSRAM (free, minimum, largest), response and parser bytes |
| `MEMORY` | at trace points in `src/network/memory_trace.hpp` | heap, PSRAM, open sockets, WebSocket clients |

The browser shows free heap and free PSRAM under **Settings → Advanced**, card **Device**.

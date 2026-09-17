# Shared backend / NFC owner

## Why

PR #24 passed physical first-run provisioning, but late NFC task creation failed
with 30,492 internal bytes free and only 13,812 contiguous bytes available for a
16,384-byte stack. Reserving that NFC stack earlier or placing it in PSRAM would
not satisfy the provisioning/runtime constraints.

`NfcWorker` is now a cooperative logical owner, **not an RTOS worker**. It has no
task entry, task handle, task allocation or stack. `BackendWorker::start(nfc)`
binds it before creating the existing backend task. Only that task calls its
private poll method. The network task only enables an atomic provisioning latch;
UI/httpd continue to consume small thread-safe snapshots. NFC/Wire1/RFAL remain
exclusive to one owner. Backend commands queued by NFC use nonblocking enqueue,
not a synchronous callback into the currently executing command.

Unconfigured Wi-Fi, connection attempts, setup AP and grace continue to expose
`enabled=true,state=deferred,reason=provisioning`. Station connection plus AP/grace
completion enables polling without allocation. Subsequent Wi-Fi loss does not
create, destroy or change owners.

## Scheduling and latency

The backend receives one command with a 250 ms timeout, polls NFC, processes the
command, polls NFC again, and polls once more after any due periodic probe cycle.
Polling before commands prevents a continuously populated queue from suppressing
removal checks. NFC retains its 500 ms normal / 5 s error cadence and three-poll
debounce. Polls do not nest beneath HTTP, probe, workflow or command frames, and
no backend/workflow lock is held while polling.

The fresh scale request remains asynchronous on the scale owner. If a backend
operation outlives a successful sample's existing five-second freshness window,
NFC requests a fresh sample on returning; stale weight is never accepted and
failed scale measurements are not automatically retried.

Normal idle removal latency is about 500–750 ms plus the RF exchange. A complete
two-image NFC read retains its 15 s deadline, checked between bounded exchanges;
it can delay the next backend command by that interval plus an in-flight RF
exchange. It does not block UI, httpd, network or scale tasks.

Each backend command or whole periodic probe cycle now has a **20 s cooperative
HTTP operation budget**, shared across all its individual HTTP requests. Once
expired, remaining requests fail without network I/O. A deadline-aware client
also closes continuously trickling headers/chunked bodies, not just idle sockets.
NFC is serviced after that operation before another command/probe. No nested NFC
callback occurs in the HTTP client.

This is not a preemptive hard-real-time deadline: bundled Arduino 2.0.17 DNS can
wait up to 15 s per lookup; a connect may include another lookup, 5 s TCP connect
and 5 s TLS handshake. Socket/Stream inactivity limits remain 7 s (rounded socket
timeout), and writes are checked between 128-byte portions. A deadline reached
inside such a call takes effect when it returns. Allow up to roughly **60 s** for
NFC servicing under ordinary failing DNS/connect/TLS/HTTP conditions, versus
subsecond polling on responsive LAN backends. An abnormally slow progressing
socket write can exceed this practical allowance, though each write is bounded
in bytes; firmware scheduling is not a hard-time guarantee. Test slow/unreachable
backends as well as the normal LAN. Header/body/flush trickles cannot indefinitely
restart an inactivity timeout; native tests exercise those deadline boundaries.

## Stack and memory

The existing backend stack increases from 12,288 to 16,384 bytes at its original
early-boot creation point, with explicit allocation PASS/FAILED logs and internal
free/minimum/largest measurements. This is **4 KiB extra during provisioning**,
not an early 16 KiB NFC reservation. Configured operation uses 12 KiB less stack
than the previous two-task design. No task stack is in PSRAM. Physical evidence
from previous builds (NFC observed use ~6,284; backend ~3,464 bytes) supports, but
does not replace, the conservative compiler audit.

CI audits the actual backend task entry/run/poll/NFC decode chain including
adversarial CBOR nesting, separately the RFAL transport path, and the backend
probe/identification/assignment HTTP paths. It takes their maximum, not their
sum: these operations are sequential. The existing >=4,096-byte safety reserve
is unchanged. Missing frames fail the check. Both read images and the shared
IdentifiedTag retain PR #24's PSRAM-preferred allocation and lifetime semantics.

Serial health output retains internal 8-bit heap free/minimum/largest and all
loop/UI/network/httpd/scale/config/backend/control/OTA stack margins. NFC logs
`owner=opentag-backend` with that task's high-water value; `/nfc` reports
`owner_task=opentag-backend`. There is no misleading separate NFC task metric.

## Physical evidence

Production read-only NFC and stationary soak passed. Observed backend/NFC free stack was 9776–9872 bytes after decode, RFAL=0, bus_errors=0. UID [physical tag UID omitted] and initialized checksum 9E639911 were retained. Further physical work uses the single [integrated acceptance](release-validation.md), including ten health cycles under backend load.

## Production OpenPrintTag writer

See [writer workflow and contract](openprinttag-writer.md). GET /api/v1/tag-writer returns its bounded snapshot; authenticated POST enqueues only approved high-level catalog/import/spool/preview/write/association operations. Periodic reads never write.

# WT32 Community search regression acceptance (1.0.0-rc.3)

The rc.2 production failure was an IDLE0 task-watchdog reboot followed by a
repeatable 19,971 ms Community deadline failure. The ninth match previously set
`has_more` but continued scanning the entire catalog.

## Stream and scheduling contract

`StreamDisposition::next`, `complete`, and `error` are distinct. After skipping
the requested offset and retaining eight matches, the ninth match sets
`has_more`, `next_offset`, and page completion. The transport closes the client
immediately from its output sink. HTTPClient's resulting short-write status is
accepted only for explicit completion; consumer errors remain failures.
Bytes already buffered by HTTP/TLS or inflated in the current bounded chunk can
exist, but no subsequent body read is requested and no later record is parsed.
Intentional completion does not require an unread gzip trailer. Full scans still
require the JSON closing bracket and valid gzip CRC/ISIZE, and reject trailing
garbage or concatenated gzip members.

The parser calls its injectable cooperation hook every 4,096 expanded bytes,
including whitespace and string contents. Firmware uses `vTaskDelay(1)`, allowing
CPU0's idle task to run, without resetting its watchdog. Native callers can use
the default no-op or supply a counting hook. Inflater work is bounded by its
32 KiB dictionary output window before reaching the parser; no hook is called
per byte or per record. Parser state persists across yields and input chunks.

Community uses a request-local 60,000 ms deadline covering DNS, connect, headers,
and body. The normal backend operation limit remains 20,000 ms. Limits remain:
64 MiB compressed and expanded input, 8 KiB per object, depth 12, 100,000 records,
eight retained results, 2 KiB per retained record, and 64 search characters.
Parser objects, JSON storage, and the gzip workspace remain in PSRAM.
The bounded record buffer is reused across objects, avoiding per-record heap
churn and serial release logging during sparse/full scans.

## Cancellation limitation

Search shares the backend-owned writer command pipeline, which currently blocks
Back while a command is active. This change does not introduce a cross-task
socket close, task deletion, or writer abort. An active search completes early or
waits for the bounded deadline; Retry and Back are available after an error.
Leaving the overall UI through existing navigation does not cancel that scan.
Operation-scoped cooperative cancellation needs a separate command protocol so
it cannot cancel another queued writer operation. NFC/tag state is untouched.

## Physical test — pending hardware execution

1. Boot WT32 and confirm firmware reports `1.0.0-rc.3` and the tested commit.
2. Confirm a blank-compatible tag.
3. Choose Assign Tag.
4. Choose Community.
5. Search a broad term, such as `PLA`.
6. Confirm results return without reboot.
7. Choose Next until the second backend page is requested.
8. Confirm the second page returns.
9. Search an intentionally obscure/no-match term.
10. Confirm a bounded no-results response.
11. Leave the station running for at least two minutes; exercise NFC, local UI,
    and a Spoolman request afterward.

PASS requires no task watchdog, no reboot, no generic backend-deadline message,
healthy NFC, responsive UI after search, working subsequent Spoolman requests,
and memory substantially returning after the stream closes. Also test poor
Wi-Fi: a real deadline must show “Community search timed out. Check Wi-Fi and try
again.” with Retry and Back.

Record these measurements for broad and no-match searches:

| Measurement | Broad search | No-match search |
| --- | --- | --- |
| Elapsed milliseconds | Pending | Pending |
| Internal heap minimum | Pending | Pending |
| Largest internal block minimum | Pending | Pending |
| PSRAM minimum | Pending | Pending |
| Backend stack minimum | Pending | Pending |
| UI stack minimum | Pending | Pending |
| httpd stack minimum | Pending | Pending |

`BACKEND community_begin` and `community_released` report before/after memory
and elapsed time. `COMMUNITY sampled_*` records scan-local free internal heap,
largest internal block, and PSRAM minima sampled at input chunks and cooperation
points while TLS/parser/inflater overlap; backend stack is the task's runtime
high-water mark. Use existing runtime task diagnostics for UI/httpd high-water
marks. Sampled heap minima are not an exhaustive allocation trace. Compare
release memory to the baseline; a temporary TLS trough alone is not a leak.
Static compiler stack estimates and native tests do not establish physical PASS.

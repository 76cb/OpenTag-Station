# WT32 local Community catalog acceptance (1.0.0-rc.4)

> **1.0 status: disabled.** Community import/search is disabled for 1.0 after physical ESP32-S3 testing demonstrated a miniz inflater-state memory overwrite. The implementation is retained for redesign in 1.1. The following is retained development history, not an rc.9 operating procedure.

The rc.3 streaming search avoided a watchdog reset but still reached its
Community-specific 60-second deadline while downloading, inflating, and scanning
the remote JSON catalog. That physical result prompted the rc.4 local catalog
architecture. The streaming search, its timeout, and its Wi-Fi-oriented search
error are no longer production paths.

## Acceptance procedure

1. Install the rc.4 Web Flasher factory image and confirm the Community catalog
   reports version `2026-09-18`, 53,424 records, and Ready.
2. Disable the station's internet access while leaving its LAN and Spoolman
   access available.
3. Search `SUNLU PLA`, page the results, select one result, and open its import
   review.
4. Search a broad term such as `PLA` and an intentionally obscure no-match term.
5. Confirm all searches and detail selection complete without Community HTTP
   traffic, a watchdog reset, or a Wi-Fi error.
6. Restore internet access and explicitly update the catalog. Confirm progress is
   visible and the station remains responsive.
7. Interrupt or reject an update and confirm the prior catalog remains Ready and
   searchable.
8. Complete a Community import through canonical Spoolman readback, create a
   physical spool, and write its tag.

Record broad, no-match, and paged-search measurements:

| Measurement | Broad search | No-match search | Paged search |
| --- | --- | --- | --- |
| Elapsed milliseconds | Pending | Pending | Pending |
| Internal heap minimum | Pending | Pending | Pending |
| Largest internal block minimum | Pending | Pending | Pending |
| PSRAM minimum | Pending | Pending | Pending |
| Backend stack minimum | Pending | Pending | Pending |
| UI stack minimum | Pending | Pending | Pending |
| UI remained responsive | Pending | Pending | Pending |

Static compiler stack estimates, native tests, browser tests, and host-side pack
inspection do not establish physical acceptance. Record the exact firmware SHA
and catalog SHA-256 with the measurements.

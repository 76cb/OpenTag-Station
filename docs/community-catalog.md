# Local Community catalog

> **1.0 status: disabled.** Community import/search is disabled for 1.0 after physical ESP32-S3 testing demonstrated a miniz inflater-state memory overwrite. The implementation is retained for redesign in 1.1. The following is retained development history, not an rc.9 operating procedure.

OpenTag Station 1.0.0-rc.4 compiles the pinned SpoolmanDB-Community JSON snapshot
into `community/community.pack`. Both the WT32 and browser submit Community commands
to the backend worker, which searches this local pack. Normal search and detail
selection perform no internet request.

The format begins with a fixed 120-byte versioned header and a block directory.
Search records contain source ID, manufacturer, name, material, display color,
nominal weight, and normalized searchable text. Detail records retain every field
accepted by the existing Community import contract. Index and detail data use
independent raw-DEFLATE blocks with a 64 KiB expanded limit. Each block has a CRC,
and the pack has a complete payload CRC and source SHA-256. Detail selection seeks
to and inflates one bounded block.

Generate and inspect a pack with:

```text
python tools/community_catalog.py compile --source filaments.json --output community.pack --catalog-version YYYY-MM-DD --source-revision REVISION
python tools/community_catalog.py inspect community.pack
```

The deterministic compiler rejects unknown source fields, invalid required values,
duplicate source IDs, malformed JSON, and output above 2.5 MiB. CI inspects the
committed production pack and enforces its record count and size.

Catalog updates fetch a small manifest and pack from the fixed OpenTag Pages origin.
The backend writes `/community.new`, verifies the declared size and SHA-256, checks
the complete internal structure and checksums, then renames the active catalog through
a rollback file. Network, truncation, hash, format, storage, or install failure leaves
the previous active catalog available. Routine application OTA does not write LittleFS.
The Web Flasher factory image contains an initial `community.pack` for new installs.

The catalog version is a snapshot date rather than a freshness deadline. An older
valid catalog remains searchable offline. Settings and the Community chooser report
the installed version, record count, byte size, and offer an explicit update action.

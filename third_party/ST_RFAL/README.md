# ST RFAL vendor boundary

No vendor source is present yet. This is an explicit external gate, not an
implicit dependency download.

Acquisition attempt recorded 2026-08-23: the official
`https://www.st.com/en/embedded-software/stsw-st25rfal002.html` delivery is
request-controlled. No authoritative archive was delivered, so no archive
filename, internal RFAL version, untouched SHA-256, complete delivered SLA0051
license, redistribution decision, import list, or patch log can be recorded.
No unofficial mirror was imported.

The project will import an exact ST `STSW-ST25RFAL002` release only after its
version, archive checksum, and SLA0051 redistribution obligations are recorded.
Vendor files remain unmodified in this directory. The ESP32-S3 adaptation lives
under `src/platform/rfal/`.

Required acquisition record before import:

- ST product/release and internal RFAL version;
- original download URL or delivery identifier and archive filename;
- SHA-256 of the untouched archive;
- complete delivered license text and redistribution decision;
- exact upstream file list and a patch log for any unavoidable modifications.

Do not substitute a different NFC controller library here. The only supported
reader architecture is ST25R3916B plus RFAL.

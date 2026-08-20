# ST RFAL vendor boundary

No vendor source is present yet. This is an explicit external gate, not an
implicit dependency download.

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

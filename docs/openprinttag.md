# OpenPrintTag strategy

## Authority

The baseline is the official
[OpenPrintTag repository at `e0dab1a`](https://github.com/prusa3d/OpenPrintTag/commit/e0dab1ae16838d2c342e7cfc509455441b7d8eba),
inspected 2026-08-17 and rechecked 2026-08-20. The format intentionally has no
explicit version number:
compatible fields are added under stable integer keys, deprecated keys are not
reused, and an incompatible future format would use another MIME type.

## Format

The current format is NFC-V/ISO15693 memory containing an NDEF TLV. An NDEF
message contains an unchunked MIME record of
`application/vnd.openprinttag`. Its payload contains three CBOR maps:

1. meta at payload offset zero, describing region offsets and sizes;
2. main, for largely static package/material information;
3. optional auxiliary, for dynamic data such as consumed weight and storage.

Each section is at most 512 bytes. Readers must accept non-canonical map order,
skip unknown keys and enum values, and preserve unknown raw CBOR during updates.
The auxiliary region is at least 16 bytes when present and 32 bytes is
recommended. Its position is defined by metadata; readers must not assume it is
after the main region.

NFC-V UIDs are normalized internally to the eight-byte network-order form with
`0xE0` first. Display formatting is separate so library-specific byte order
cannot corrupt UUID derivation.

## Model and codec boundaries

The codec is host-testable and independent of NFC hardware:

```text
raw tag bytes
  -> Type 5 capability container / TLV parser
  -> NDEF record parser
  -> bounded CBOR parser with raw unknown-field retention
  -> OpenPrintTag normalized model + validation report
```

Consumed-weight encoding produces a proposed byte image without changing the
allocated region layout. Untouched CBOR key/value encodings—including unknown
future fields—remain byte-for-byte intact. The codec decodes the proposed image
again before returning it. The NFC-V layer then produces a full-block diff,
rejects locked blocks, checks the one expected UID before each write, rereads
each affected block, and compares exact bytes. It never reports a transport
write as verified merely because the write command returned success.

The domain model covers every main and auxiliary field defined at the pinned
revision: identities, GTIN, class/type/name/abbreviation, brand and origin,
dates, nominal and actual weights/lengths, container properties, RGB/LAB/RAL
colors, temperatures, FFF and SLA material properties, write protection,
consumed weight, workgroup, storage/purchase data, and preserved unknown entries.
Parsing is capped at a 4096-byte image, 512 bytes per CBOR region, 128 map
entries, and 12 nesting levels; malformed bounds, duplicate keys, invalid UTF-8,
non-finite data, and invalid field relationships are rejected or surfaced as
validation errors.

## Safe update transaction

1. Inventory one tag and freeze its normalized UID.
2. Read capability container, geometry, required memory, security/write status,
   and all OpenPrintTag regions.
3. Decode and validate bounds before allocating or parsing nested CBOR.
4. Produce an updated representation while retaining unknown fields and bytes.
5. Confirm record/region offsets and sizes are unchanged for a normal update.
6. Compute affected tag blocks and reject protected or out-of-range writes.
7. Write one block at a time with tag-presence and UID checks.
8. Re-read every affected block and compare exact expected bytes.
9. Re-read/decode the logical record and verify the intended field.
10. Report success only after both physical and semantic verification.

Original bytes remain cached through the interaction. A failed verification is
an explicit retryable/non-retryable error and never becomes a successful
inventory operation. Routine backend changes do not continuously rewrite tags.

Initialization/reuse is a distinct operation because it may alter the complete
NDEF structure, protection, AFI/DSFID, and allocation. SLIX2 `PROTECT PAGE` and
password behavior must be implemented only with the exact tag capability and
vendor command documentation.

## Test fixtures

The native suite embeds two 312-byte official fixtures from revision `e0dab1a`:
the normal FFF example and the two-step unknown-field update result. It verifies
their documented region layout and fields, safe auxiliary modification, and
unknown main-field byte preservation. Synthetic fault cases cover truncation,
oversize images, invalid offsets, duplicate keys, excess nesting, locks, tag
replacement, multiple tags, and block readback mismatch. Additional official
minimal/optional/max-payload fixtures and parser fuzzing remain release-hardening
work rather than a physical-NFC dependency.

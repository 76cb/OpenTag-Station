# Production writer: consolidated physical acceptance

Use the production artifact from the feature PR after all CI gates pass. Do not
merge before acceptance. PR #27 already passed the memory/backend/NFC blocker;
use its physical readings as the comparison baseline.

Configured services: Spoolman `http://192.168.1.215:7912` (the adapter appends
`/api/v1`), text extra fields `opentag_instance_uuid` and `nfc_uid`; FilaBridge
`http://192.168.1.155:5000`. These addresses are configuration, never firmware
constants. Keep one evidence log with firmware SHA, screenshots, operation IDs,
serial output, checksums, Spoolman IDs and exact FilaBridge readback. An HTTP
receipt is not evidence of a successful physical write.

1. **Normal boot.** Install the PR production artifact using the existing flasher.
   Boot with saved configuration; verify healthy backend discovery, scale and NFC,
   no restart loop and zero bus errors. Record the exact firmware commit.
2. **Browser and touchscreen.** Open Tags in both interfaces. Verify current tag
   state, visible Write / Rewrite entry, spool ID entry, disabled confirmation
   before a preview, and readable progress/result areas. Keep the browser open.
3. **Memory/stack baseline.** Record settled internal free/minimum/largest block,
   PSRAM, task high-water values, socket counts and uptime. PR #27 measured about
   89–94 KiB settled free, 73.5 KiB minimum, and 73–78 KiB largest internal block.
   Compare after completed operations, not only during temporary allocations.
4. **Compatible blank tag.** Place exactly one expendable blank NXP SLIX2 tag.
   Automatic reading may report no OpenPrintTag yet; the writer's preview will
   classify its complete content. Blank requires all 312 usable bytes zero.
   The known tag `E0:04:01:08:66:27:D8:D4` may already contain the diagnostic
   initializer (`9E639911`); use a separate blank tag for genuine blank acceptance.
5. **Existing Spoolman catalog.** Browse vendors, filaments and spools. Exercise
   search, material filter, more than one page and Search / Refresh. Compare IDs
   with live Spoolman. Opening/browsing must perform no tag writes.
6. **Community search.** Select SpoolmanDB Community. Search vendor/product, material
   and SKU or another source property. Confirm results are usable after the initial
   bounded browser download. Repeated searches reuse the tab cache.
7. **Import one missing filament.** Select a genuinely absent product; inspect
   source versus proposed vendor/filament and unknown values. Confirm import and
   verify canonical Spoolman GET/readback IDs. Preview/import the same source again:
   expect reuse, not duplicate vendor/filament creation.
8. **Create/select the physical spool.** Create a spool from that canonical
   filament, supplying only known initial/used weight, tare, price/location/batch
   or notes. Verify the returned canonical spool ID. Also exercise selection of an
   existing spool; Community connectivity must not be required for that path.
9. **Generate preview.** Choose Preview initialize / rewrite. Wait for complete
   current reads and canonical Spoolman retrieval. Check Initialize classification,
   current/proposed metadata, vendor, filament, material, color and UUID.
10. **Verify the exact plan.** Record UID, insertion generation, current and target
    checksums, spool ID, changed block numbers/count and warnings. Require 80 × 4
    = 320 physical bytes, 312 OpenPrintTag bytes, writable blocks 0–77 and preserved
    blocks 78–79. Remove/replace before confirmation as a negative check: fail
    without modifying the replacement, then reread and obtain a fresh preview.
    Multiple tags, protected blocks and unknown/non-OpenPrintTag content must also
    be refused; use appropriate expendable fixtures where available.
    For a UID owned by another spool (including an archived spool), require the
    previous and target spool IDs, UID and explicit move warning. Multiple owners
    must refuse preview; changing the confirmed previous ID must refuse writing.
11. **Initialize.** Explicitly confirm that exact UID/generation/checksum/spool
    plan, including the disclosed previous UID owner. Keep tag and power in place. Confirmation from an obsolete preview must
    fail. Touchscreen confirmation must refer to the same displayed target.
12. **Per-block readback.** Observe bounded writing progress. Every changed block
    must be written once and read back; unchanged blocks must not be written.
    An injected communication/readback failure must stop subsequent writes.
13. **Full reread/decode.** Require final complete 320-byte comparison, valid
    production decoding, matching target checksum and preserved tail. Record
    resulting metadata and zero bus errors. The populated checksum intentionally
    differs from the diagnostic initializer checksum.
14. **Spoolman association.** Verify the exact UUID and NFC UID by independent
    Spoolman GET after physical PASS. Exercise association pending by making
    Spoolman unavailable during the local physical sequence on an expendable
    transaction: restore connectivity and Retry association without extra NFC
    writes. Repeat through reboot: preview the journaled tag, require recovered
    association-pending, then retry. For a controlled partial write, reboot and
    reread; only saved old/new blocks may enter explicit recovery. Never claim
    partial success, recover an unrelated UID, or overwrite unknown torn bytes.
    Repeat with a partial old/new image that still decodes: require recovery.
    Valid CBOR containing any unauthorized block must be refused. Exact original
    permits ordinary preview; exact target recovers association pending.
    For an approved move, verify previous UID clear/readback precedes target
    PATCH, previous UUID remains, and final UID lookup returns only the target.
    Failed cleanup must prevent target PATCH. Failed target PATCH after cleanup
    must remain pending and retry successfully without NFC writes, also after reboot.
15. **Remove/reinsert.** Remove the finished tag; stale identity/candidates must
    clear. Reinsert and require fresh stable inventory and OpenPrintTag decoding.
16. **Automatic Spoolman resolution.** Require the exact associated physical
    spool, with UUID/UID mapping and no manual recreation of filament metadata.
17. **Rewrite a supported property.** Change a supported canonical Spoolman value
    (for example the filament color). Preview the same tag/spool; Current versus
    Proposed must show the change and retain its stable UUID. Confirm rewrite.
18. **Minimal rewrite verification.** Require only the previewed changed blocks,
    complete reread/decode and unchanged preserved tail. Repeat with no canonical
    change: a no-change plan must write zero blocks. Previewing another spool must
    clearly indicate repurposing and an appropriate unique identity.
19. **Consumed-weight update.** Set a known canonical Spoolman usage value. Choose
    Update consumed weight from Spoolman, review Current/Proposed and confirm.
20. **Auxiliary-only verification.** Require the smallest changed auxiliary block
    plan, unchanged main metadata and blocks 78–79, and exact decoded consumed
    weight. The initialized layout uses aux offset 276, encoded size 35 (requested
    32). Scale movement and periodic polls must never trigger tag updates.
21. **Stable scale weight.** Place the resulting physical spool on the scale.
    Obtain stable=yes and a fresh measurement; reject overload/unstable readings.
22. **Weight reconciliation.** Check the actual tare source, gross/net and
    used/remaining weight. Apply the existing explicit reconciliation workflow;
    verify authoritative Spoolman readback and any warnings before assignment.
23. **Detect the printer.** Discover `Casy's Prusa XL`, exact printer ID
    `printer_1785006977542801400_535`, through configured FilaBridge.
24. **Detect tools.** Confirm T1–T5 map to backend tool IDs 0–4. Retain existing
    occupied-tool and active-print safeguards.
25. **Assign T1.** Assign this actual resolved Spoolman spool to T1 (backend 0),
    completing any applicable explicit occupancy/active-print confirmations.
26. **Exact FilaBridge readback.** Independently GET the backend state. Require
    the exact printer ID, tool 0 and selected spool ID; a successful POST alone
    does not pass. Confirm the station displays verified assignment.
27. **Unassign and verify.** Unassign T1 with the existing workflow and verify
    exact empty/unassigned backend state by GET. Other tools must be unaffected.
28. **Combined soak.** Run for several minutes and at least ten completed cheap
    health cycles. Repeat catalog/search, previews, no-change plans, explicit
    weigh/reconcile, REST and WebSocket use. Record settled heap/PSRAM and task
    minima after each cycle. Require no monotonic settled allocation growth,
    inaccessible UI, reset/watchdog, unrequested writes or NFC bus errors.
    Compare compiler reserves with CI and record physical stack high-water values;
    compiler estimates alone do not certify the combined physical workload.

Record PASS/FAIL for every step and retain the production artifact. The diagnostic
bundle is a separate regression artifact. See [writer contracts and mapping](openprinttag-writer.md)
for the supported write boundary and interruption limitations.

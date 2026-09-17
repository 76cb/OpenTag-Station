# Production NFC / OpenPrintTag

The normal firmware supports automatic read-only recognition and explicitly
confirmed OpenPrintTag initialize/rewrite/mutable updates. The complete boundary,
mapping, Community contract and recovery policy are in
[production writer](openprinttag-writer.md).

The sole NFC/RFAL owner is the shared 16,384-byte opentag-backend task. Scale
remains on Wire (GPIO10/11); NFC uses the validated ELECHOUSE Wire1 binding
(GPIO13/14, IRQ12, 0x50, 100 kHz). Touch stays on LovyanGFX software I2C.
No task, transport, RFAL fork, or stack increase was introduced.

Periodic polling remains read-only: three stable inventory rounds trigger two
complete compared reads, with generation-fenced workflow handoff and fresh stable
weighing. No NFC activity runs in httpd, UI callbacks, or backend HTTP frames.
The explicit writer is dispatched separately at the backend run-loop boundary.

Only the approved NXP 80×4-byte layout is writable. Blocks 0–77 form the 312-byte
OpenPrintTag image; 78–79 are always preserved. Unknown application data is
refused. The diagnostic write UI remains disabled. Source plus object/ELF guards
restrict the destructive primitive to the private production binding and guarded
writer. No arbitrary raw writer or lock/protection API is exposed.

The old decoder baseline remains pinned to e0dab1a with additive read compatibility
for specification 7e09cc3's integer-micrometre diameter key 61. The new writer emits
that current key. Existing legacy-key reads and initializer golden fixtures remain
tested. A populated writer image is also validated by the independent pinned
upstream Python decoder.

Recognition, removal/reinsertion, and stationary read soak previously passed;
PR #27 physically restored settled internal heap to about 89–94 KiB, minimum
73.5 KiB, with zero NFC bus errors and functioning backends/browser/scale.
Those are the baseline. Writing requires the single consolidated
[physical acceptance procedure](release-validation.md).

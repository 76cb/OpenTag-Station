# Recovery and safety model

A tag write cannot be made atomic across multiple NFC blocks, local storage and
a remote inventory service. The station records a checksum-protected journal and
checks observed memory against original and intended images before resuming.
The retained final two blocks remain part of verification even though not written.

| State / situation | Meaning | User action |
|---|---|---|
| Writing / verifying | Physical operation active | Keep tag and power stable; wait or inspect status |
| Recovery required | Interrupted physical work | Present the same original tag and request the offered recovery preview |
| Association pending | Physical image verified; remote link incomplete | Restore the backend and use the association retry action |
| Clear recovery | Blank target not durably verified | Present the same tag; let the journal classify its image |
| Unlink pending | Tag blank; inventory/local cleanup incomplete | Restore original settings and retry unlink; tag presence not required |
| Unknown/torn block | Neither original nor intended image is safely established | Stop; preserve journal and sanitized diagnostics for investigation |

Prerequisites for recovery are the original tag where physical work is incomplete,
stable supply and unchanged backend identity settings. Inspect the state first,
perform the offered action once, and wait for exact verification. Do not delete
journal files, flash an erase-all image or swap in another tag to clear a warning.
Those actions discard the evidence needed to determine safe progress.

Success requires both the physical checkpoint and any owner-bound remote cleanup.
An HTTP receipt, a surviving header, or a blank-looking name is insufficient.
See [writer journaling](../advanced/journaling.md) for durable ordering and
[Clear / Reuse](../daily-use/clear-reuse.md) for cleanup semantics.

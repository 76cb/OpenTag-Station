# Identity fields and ownership

The station uses configured Spoolman extra-field keys for NFC UID and
OpenPrintTag instance UUID, plus a bounded local cache of confirmed mappings.
These identify one physical spool; a display name or material match is not proof
of ownership. Keep the keys consistent across station settings and Spoolman.

Before first linking, inspect Settings → Integrations and the Spoolman adapter's
connection result. Use the documented supported identity-field types and compare
a known spool. Changing field keys while a writer or clear journal is pending can
make cleanup unsafe; restore the original configuration before retrying it.

Clear / Reuse first verifies the tag is blank, then queries supported UID spellings
and instance identity. It binds the unique owner durably before mutating only those
two extra values to null. Readback checks removal; final identity queries detect
ownership conflicts. Usage and unrelated extras are preserved. A different/new
owner refuses cleanup rather than erasing that owner's association.

If an identity matches multiple records, resolve the conflict in canonical
inventory and refresh the station. Do not repeatedly confirm whichever record
appears first. Local confirmed mappings are not permission to bypass a contradictory
backend owner. See the [Spoolman technical reference](../reference/spoolman.md)
for key validation, encoding and compatibility details.

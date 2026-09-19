# What the station does

The station combines four views of one physical spool:

| System | Owns | What the station does |
|---|---|---|
| NFC tag | Physical tag identity and portable OpenPrintTag data | Reads; writes only through preview, confirmation and verification |
| Scale | Gross mass of spool plus filament | Produces an explicit stable measurement; subtracts resolved empty-spool weight |
| Spoolman | Canonical filament and physical spool records | Resolves identity, edits selected fields and verifies requested remaining-weight updates |
| FilaBridge | Printer/toolhead mapping and consumption integration | Requests a confirmed assignment and verifies the returned mapping |

Home focuses on the spool currently on the station. Inventory manages your
Spoolman records. Printer shows assignments. Settings groups
integration, station, scale, network, display and advanced controls. The touchscreen
provides Home, Weigh, Assign, Tag and Settings.

A tag read is not a weight update. Leaving a spool on the reader does not keep
PATCHing inventory. Auto-update after explicit Weigh is off by default. Updating
Spoolman does not automatically rewrite the tag, and a successful HTTP response
alone does not establish a verified physical write or printer assignment.

You need the supported hardware, trusted Wi-Fi, Spoolman for canonical inventory,
and FilaBridge only if printer assignment is wanted. Community is disabled for 1.0. Local operation does not require a station cloud account. See [quick start](quick-start.md) for the complete first-spool sequence.

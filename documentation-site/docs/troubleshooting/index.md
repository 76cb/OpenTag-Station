# Troubleshooting by symptom

Start with the last known good state and change one thing at a time. Record
firmware VERSION/SHA, operation phase, safe error text and the affected workflow.
Never post tokens, real tag identifiers, private addresses or personal printer
names in a public screenshot.

| Symptom | Guide |
|---|---|
| No reader / scale after assembly | [Wiring faults and validation](../hardware/wiring.md) |
| Reader ready but no tag | [Tag detection](tag.md) |
| Unsupported tag or failed write | [Tag writing](write.md) |
| Spoolman unavailable | [Spoolman connection](spoolman.md) |
| Community catalog fails | [Community download/import](community.md) |
| Weight differs or is unstable | [Weight and mechanics](weight.md) |
| FilaBridge offline / assignment fails | [Printer assignment](assignment.md) |
| Cannot reach station | [Wi-Fi recovery](wifi.md) |
| Pending journal after interruption | [Recovery states](journal.md) |
| Station does not boot | [Factory recovery](recovery.md) |

Success is the intended verified state, not the absence of an error banner. A
network failure after a mutation can leave uncertainty; inspect canonical state
before any new operation. Do not erase a recovery journal or lower safety checks
to make an action button available. Preserve a private diagnostic record and share
only sanitized excerpts for engineering investigation.

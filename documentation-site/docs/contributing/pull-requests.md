# Contributing changes

Start from current main or the explicitly requested review branch. Keep changes
focused, preserve production safety boundaries, and describe the user-visible
trigger and outcome in the PR. Do not remove host coverage simply because a
standalone development image is no longer distributed.

For a code change, run the relevant local checks and complete CI. For an API or
integration change, verify expected-value/revision fences, idempotency, ownership,
readback and refusal behavior. For UI changes, test exact desktop/mobile widths,
keyboard/modal flows and memory budgets. Record real hardware checks separately
from native/fixture results.

For documentation, update the manual in the firmware project's documentation-site
directory, run strict build/link/privacy checks and export the reviewed source to
the dedicated docs repository. Hardware changes require regenerated diagrams and
source assertions. Never publish private service addresses, credentials, real tag
serials or personal printer names. Use generic fixture data and guarded capture
tools; all public PNGs require provenance.

Release PRs must retain an honest pending-acceptance status until external visual,
documentation and physical checks complete. Final VERSION changes and tags require
the [release process](release.md). PR #32 remains unmerged for this acceptance;
documentation publication alone is not product acceptance.

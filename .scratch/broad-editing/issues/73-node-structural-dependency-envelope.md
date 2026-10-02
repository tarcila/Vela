# 73 — Confirm explicit structural dependency carrier and preparation cap

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/mcp/tools/EditTools.hpp
**Kind:** extends

## Assumption

Enabled-only node entries require `structural_version` copied from node inspection, in addition to `version` (`node_edit_version`). The spec requires the inspected structural chain and missing required dependency tokens to reject before admission, but does not spell out this field's wire name. Reuse the inspection field rather than invent a second token or capture the hierarchy at execution, which would miss reparent/change-back between inspection and submission.

Preparation walks at most 4096 ancestor steps across one batch; excess returns `busy` without mutation or consumption of restore eligibility. Only compact existing NodeDependencyWalk records are retained and charged to the shared journal budget. This implements the required bounded fail-or-yield behavior without an unbounded snapshot or a second scheduling mechanism.

## Revisit if rejected

Rename the carrier consistently in parser/schema/docs/tests, or agree a different bounded preparation policy. Do not weaken inspected structural history checks or allow unbounded safe-point work.

## Comments

Filed during 05; neither choice widens write authority or enables transform editing.

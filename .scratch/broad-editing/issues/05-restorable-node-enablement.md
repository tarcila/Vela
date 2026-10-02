# 05 — Enable or disable nodes in restorable atomic batches

**What to build:** Show/hide existing nodes through the complete authorized node-edit path, with truthful activity, synchronization, and exact conditional restoration.

**Blocked by:** 28
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 1, 5, 7, 8, and 9 for enabled-state edits; Testing Decisions 7–12 as applicable.

## Current behavior

Nodes can be inspected and versioned, but Agent Edit Batches and restoration journals currently target only supported object parameters.

## Acceptance criteria

- [ ] Scene registration explicitly opts into node edits and discovery advertises exposure; existing object edit grants alone do not widen node writes. Viewer agent-integration mode opts in.
- [ ] Compatible node-discriminated edit entries accept only a Boolean enabled value on an existing node of any supported node kind, with safe identity/version/structural checks.
- [ ] Batch limits, permissions, replay identity, current-state validation, preparation failure, and no-partial-commit rules match existing object edits; unrelated sibling writes do not conflict.
- [ ] Complete state is visible before coalesced normal layer notifications; exactly one ordinary scene-sync acknowledgement follows. Committed sync failures and post-commit cancellation are reported honestly.
- [ ] Commit results include Batch Reference, scene revision, and node_versions/dependencies without manufacturing object tokens.
- [ ] Journal/activity budgets cover node state. Restore recovers only the changed flag, detects subsequent relevant writes, preserves unrelated state/defaults, and follows permanent conflict, one-way consumption, expiry, and replay rules.
- [ ] Local/external Restore, Disable agent edits, Disconnect, and Resynchronize retain their existing authority and lifecycle behavior.
- [ ] Headless and MCP tests plus a thin viewer check demonstrate enable → inspect → restore; all existing object-only tests remain green.

## Out of scope

Transform edits, layer activation changes, structural editing, broader object fields, and journal/result-store redesign.

## Comments

### Orchestrator dependency update

03 was split before implementation into 25–28. This ticket waits for terminal
replacement 28, so it cannot ship editable nodes with partial identity,
change-back, or ancestry safety. No edit/restoration requirement was weakened.

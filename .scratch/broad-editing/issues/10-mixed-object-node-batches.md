# 10 — Commit and restore mixed object/node batches

**What to build:** Apply already supported object-parameter edits and placement edits together as one all-or-nothing live scene adjustment and one conditional restoration.

**Blocked by:** 06
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5, 7–9 for mixed batches; Testing Decisions 8–10 and 12.

## Current behavior

Object edits and the new node path work, but mixed target batches must be explicitly unified and verified rather than committed by independent loops.

## Acceptance criteria

- [ ] Node and legacy object entries share a scene-scoped edits list, combined 64-entry limit, target-kind-aware replay identity, and compatible legacy envelope.
- [ ] Validate all permissions, exposure, identities, target versions, values, dependencies, and preparation capacity together; one failed entry changes no target or version and publishes no committed journal.
- [ ] Observers see only complete object/node state and one normal scene-sync acknowledgement; post-commit failure is reported honestly.
- [ ] Return one Batch Reference, scene revision, object versions, and node_versions; do not change legacy object-only result meanings.
- [ ] One bounded journal restores both target kinds atomically, with their appropriate narrow dependencies, defaults/unrelated state preserved, permanent conflicts, one-way consumption, and correct local/external authority.
- [ ] Node fields added by other completed slices integrate without per-field special restoration forks; broader object parameters remain read-only.
- [ ] Headless/MCP tests inject one stale object, one stale node, invalid fields, and preparation failure, then verify valid mixed commit/restore and existing replay behavior.

## Out of scope

Expanding the object parameter allowlist, result-store extraction, whole-scene undo, and changing synchronization/permission policies.

## Comments

# 28 — Observe precise structural and ancestry dependencies

**What to build:** Complete 03's inspection contract with opaque ancestry dependencies and separately checkable narrow structural preconditions, tracked through real normal-core topology changes.

**Blocked by:** 27
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 4 and 7 (identity, structure, ancestry); Testing Decisions 3, 4, 7, 11, and 12.
**Files:** src/vsr/core/Forest.hpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/README.md, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 03 before implementation. After 25–27, bounded layer/node queries, genuine lifetimes, local observations, and fine target versions exist. They do not yet provide complete structural/ancestry dependencies for later local versus world-space edits. Forest has normal subtree copy/move/erase operations; checking only final parent pointers misses move-away/back.

## Acceptance criteria

- [ ] Inspection exposes opaque `ancestry_version` plus separately checkable structural dependency information covering target identity, layer membership, and relevant parent chain. Bind dependencies to their actual live target/domain; wrong-kind or stale identities cannot redirect.
- [ ] Relevant topology history is detectable: reparent/move-away-and-back, ancestor removal/recreation, layer membership changes, reset/replacement, and clone/copy distinctions, including between queries. Use actual normal Forest/Layer mutation paths, not final parent/value fingerprints or whole-layer structural epochs.
- [ ] Structural dependencies for local edits remain unchanged by otherwise live parent transform/enabled writes. Ancestry transform dependencies detect parent/grandparent transform writes including change-back. Keep these distinctions usable by 05 and 09; do not collapse them into one coarse node/layer token.
- [ ] Unrelated sibling topology/state, other layers, names, and object parameter changes do not invalidate the target's narrow structural/ancestry dependencies. A move preserves the same live node handle while invalidating affected dependencies. No wholesale identity regeneration on notifications.
- [ ] Deep-chain dependency construction and checking obey existing shared work/scratch/deadline budgets and slice yielding, including cancellation and scratch release; no unbounded full-scene snapshot or hidden uncharged recursion. Cursor/revision behavior remains correct.
- [ ] Document the required notifying custom-host topology/mutation contract and audit existing core mutation entry points, preserving viewer/Lua/animation/importer correctness. If a required normal path cannot be covered safely, file and stop on the load-bearing hole rather than accepting a fingerprint fallback.
- [ ] Public headless tests observe target/local/ancestry tokens through real normal mutations: parent transform change-back, reparent change-back, unrelated sibling insertion/removal, parent removal/ABA, stable identity on move and distinct identity on clone. Thin MCP tests demonstrate returned dependency fields; full integration and relevant core tests pass.
- [ ] Recheck every original 03 acceptance criterion across completed 25–28, including bounded hierarchy filters/records, local matrix/array distinctions, lifetime safety, normal mutation participation, and existing state clients. Record exact combined regression commands and results; no criterion is silently deferred to 04/05.

## Implementation and verification guidance

The confirmed seam is public headless Integration with real Scenes and host-owned safe points, plus thin MCP checks. No test-only hook. Work red/green one behavior at a time. Numeric world/SRT derivation belongs to 04; this ticket supplies dependency observations, not conversion or node writes. Use 27's precise write history for ancestor transform dependencies without treating ancestor changes as child editable-state writes.

Build with at most four jobs using progress.md guidance; logs belong in `.scratch/broad-editing-logs/`. Run changed cases, full `vsr::integration`, relevant core tests, and formatting; record exact commands/results. Before reporting, incorporate a changed integration tip and reverify.

## Out of scope

Numeric world/SRT derivation and edit affordances (04), editing/restoration (05 onward), new structural editing APIs, persistent IDs/archive changes, and broader refactors.

## Comments

### 2026-10-02 split from 03
Terminal replacement of 03: chain 25 → 26 → 27 → 28. The orchestrator must replace the existing 03 blocking edges on 04 and 05 with 28 before dispatching those tickets; this worker did not edit sibling tickets. Resolving 28 means the complete original observation/identity/version contract, not merely ancestry tokens.

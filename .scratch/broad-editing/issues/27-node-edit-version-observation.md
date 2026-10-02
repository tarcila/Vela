# 27 — Observe precise node edit versions across normal mutations

**What to build:** Expose fine node editable-state tokens that detect intervening writes, including change-back, through ordinary core mutation paths and public inspection.

**Blocked by:** 26
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 4 and 7 (editable state); Testing Decisions 3, 4, 7, and 12.
**Files:** src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/core/Forest.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/README.md, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 03 before implementation. After 26, local node state and safe lifetimes are readable, but editable-state tokens are not complete. Current LayerNodeData setters/assignments mutate payload without fine per-node mutation history; layer-wide scene delegate signals do not identify which target was written.

## Acceptance criteria

- [ ] Node inspection returns an opaque host-issued node edit-version token bound to the still-live node identity and registration/launch. Version covers the node's transform and own enabled state, conservatively within that node.
- [ ] A target transform or enabled write followed by restoration of the original value advances its token even if both occur between inspections or before a single coalesced layer notification. Final-state fingerprints alone cannot satisfy this requirement.
- [ ] Rename alone stales relevant inspection cursors but leaves that node's editable token unchanged. Unrelated sibling writes, other layers, object parameters, and layer activation do not advance every node's edit version. Parent transform writes leave a child's local editable version unchanged.
- [ ] Normal LayerNodeData mutation paths participate: matrix/SRT setters, enabled setters, default reset, payload assignment/type changes as relevant, and copy/move behavior. Preserve stored default baselines and lifetime distinctions from 26. Tracking must work through ordinary viewer/Lua/animation/importer/host calls, not an integration-specific setter callers must opt into.
- [ ] Ordinary delegate publication and batched/coalesced layer signals preserve precise per-node history; scene/render/inspection revisions remain correct without replacing identities or using whole-layer conflict versions.
- [ ] Document the custom-host notifying mutation contract and unsupported raw-memory writes. Audit representative real viewer/Lua/animation/importer call sites and record coverage or any load-bearing hole; do not silently claim untested paths or broaden this into unrelated rewrites.
- [ ] Public headless tests inspect tokens before/after actual normal core writes, change-away/back, assignment/default reset, rename, sibling/parent changes, and copy/move. Thin MCP tests verify the token is present in wire records. Existing integration and relevant Scene/Forest tests pass.

## Implementation and verification guidance

Use the confirmed public headless seam with real Scenes and ordinary mutators; no test-only mutation hook or private-map assertions. Read actual setter and assignment semantics before designing tracking. Storage relocation must not look like a new target identity or an unrelated edit, while copying/assigning new editable content must not erase write history. Keep version observations bounded and account retained/query state in existing limits.

Build with at most four jobs using progress.md guidance; capture logs in `.scratch/broad-editing-logs/`. Run red/green changed cases, full `vsr::integration`, relevant core tests, and formatting; record exact commands/results. This slice still does not make downstream edits ready: 28 supplies separately checkable structure/ancestry dependencies.

## Out of scope

Ancestry/structural dependency token completion (28), world/SRT numeric observations (04), edits/restoration (05 onward), structural editing APIs, archive IDs, and broader application/core refactors.

## Comments

### 2026-10-02 split from 03
Third vertical observational slice. Coarse layer revisions or final-value hashes are expressly insufficient. Downstream 04/05 remain blocked until 28 completes the original contract.

# 27 — Observe precise node edit versions across normal mutations

**What to build:** Expose fine node editable-state tokens that detect intervening writes, including change-back, through ordinary core mutation paths and public inspection.

**Blocked by:** 26
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 4 and 7 (editable state); Testing Decisions 3, 4, 7, and 12.
**Files:** src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/core/Forest.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/README.md, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 03 before implementation. After 26, local node state and safe lifetimes are readable, but editable-state tokens are not complete. Current LayerNodeData setters/assignments mutate payload without fine per-node mutation history; layer-wide scene delegate signals do not identify which target was written.

## Acceptance criteria

- [x] Node inspection returns an opaque host-issued node edit-version token bound to the still-live node identity and registration/launch. Version covers the node's transform and own enabled state, conservatively within that node.
- [x] A target transform or enabled write followed by restoration of the original value advances its token even if both occur between inspections or before a single coalesced layer notification. Final-state fingerprints alone cannot satisfy this requirement.
- [x] Rename alone stales relevant inspection cursors but leaves that node's editable token unchanged. Unrelated sibling writes, other layers, object parameters, and layer activation do not advance every node's edit version. Parent transform writes leave a child's local editable version unchanged.
- [x] Normal LayerNodeData mutation paths participate: matrix/SRT setters, enabled setters, default reset, payload assignment/type changes as relevant, and copy/move behavior. Preserve stored default baselines and lifetime distinctions from 26. Tracking must work through ordinary viewer/Lua/animation/importer/host calls, not an integration-specific setter callers must opt into.
- [x] Ordinary delegate publication and batched/coalesced layer signals preserve precise per-node history; scene/render/inspection revisions remain correct without replacing identities or using whole-layer conflict versions.
- [x] Document the custom-host notifying mutation contract and unsupported raw-memory writes. Audit representative real viewer/Lua/animation/importer call sites and record coverage or any load-bearing hole; do not silently claim untested paths or broaden this into unrelated rewrites.
- [x] Public headless tests inspect tokens before/after actual normal core writes, change-away/back, assignment/default reset, rename, sibling/parent changes, and copy/move. Thin MCP tests verify the token is present in wire records. Existing integration and relevant Scene/Forest tests pass.

## Implementation and verification guidance

Use the confirmed public headless seam with real Scenes and ordinary mutators; no test-only mutation hook or private-map assertions. Read actual setter and assignment semantics before designing tracking. Storage relocation must not look like a new target identity or an unrelated edit, while copying/assigning new editable content must not erase write history. Keep version observations bounded and account retained/query state in existing limits.

Build with at most four jobs using progress.md guidance; capture logs in `.scratch/broad-editing-logs/`. Run red/green changed cases, full `vsr::integration`, relevant core tests, and formatting; record exact commands/results. This slice still does not make downstream edits ready: 28 supplies separately checkable structure/ancestry dependencies.

## Out of scope

Ancestry/structural dependency token completion (28), world/SRT numeric observations (04), edits/restoration (05 onward), structural editing APIs, archive IDs, and broader application/core refactors.

## Comments

### 2026-10-02 iteration — resolved
Salvaged all seven non-scratch paths from WIP `vyzyyynvsoxqwulkynltuttkzknqkvos`; deliverable `rtnmtzmkonptxvyxlssqllsyupwplwkl`. Integration bookmark remains `24db29e5`. Restored core history increments on ordinary writes/assignment and moved-from payloads; construction/relocation preserves history while Forest copy receives a distinct lifetime. No snapshot fallback or whole-layer edit version.
Kept both reported erase/reuse failure assertions and strengthened them with actual slot-retirement/replacement-lifetime checks; failures did not reproduce after rebuilding the restored sources and installing. The earlier cursor test already uses the real `cursor` field in the restored WIP. Existing red/green logs remain in `.scratch/broad-editing-logs/27-*`; final checks are `27-final-*` outside the task subtree.
Public queries cover every normal setter, change-back/coalescing, defaults, assignments, copy/move/relocation, unrelated writes, rename/cursor staleness, lifetime retirement and launch/registration boundaries; MCP checks schema/filter parsing and the wire token before/after change-back. README now documents the precise contract and representative viewer/Lua/animation/importer audit. Optional UI/Lua/USD frontend execution is not claimed; current default baseline and reset behavior are tested. Ancestry/edit readiness remains ticket 28's scope.
Verified: `cmake --build _out/_cmake -j 4 && cmake --install _out/_cmake` → passed with existing Release cache; `_out/_cmake/vsrIntegrationTests '[node-version],[node-inspection],[node-mcp]'` → 698 assertions/11 cases passed; `_out/_cmake/vsrIntegrationTests` → 9,389 assertions/206 cases passed; `ctest --test-dir _out/_cmake --output-on-failure` → 36/36 passed including Forest/Scene/integration; `clang-format --dry-run --Werror src/vsr/scene/LayerNodeData.{hpp,cpp} src/vsr/integration/identity/Identity.hpp src/vsr/integration/inspection/Inspection.hpp src/tests/integration/test_Integration{Identity,Inspection,McpTools}.cpp` → passed.
Holes filed: none

### Orchestrator recovery — interrupted mutation-version pass

The worker hit the harness stdout-event limit after initial version changes
and red/green coverage. Additional mutation-path tests still report failures,
so no resolved status is claimed. Preserve the code-only WIP before a bounded
continuation fixes assignment/move/default-path semantics and verifies all
normal paths, MCP, and regressions.

### 2026-10-02 split from 03
Third vertical observational slice. Coarse layer revisions or final-value hashes are expressly insufficient. Downstream 04/05 remain blocked until 28 completes the original contract.

# 28 — Observe precise structural and ancestry dependencies

**What to build:** Complete 03's inspection contract with opaque ancestry dependencies and separately checkable narrow structural preconditions, tracked through real normal-core topology changes.

**Blocked by:** 27
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 4 and 7 (identity, structure, ancestry); Testing Decisions 3, 4, 7, 11, and 12.
**Files:** src/vsr/core/Forest.hpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/README.md, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 03 before implementation. After 25–27, bounded layer/node queries, genuine lifetimes, local observations, and fine target versions exist. They do not yet provide complete structural/ancestry dependencies for later local versus world-space edits. Forest has normal subtree copy/move/erase operations; checking only final parent pointers misses move-away/back.

## Acceptance criteria

- [x] Inspection exposes opaque `ancestry_version` plus separately checkable structural dependency information covering target identity, layer membership, and relevant parent chain. Bind dependencies to their actual live target/domain; wrong-kind or stale identities cannot redirect.
- [x] Relevant topology history is detectable: reparent/move-away-and-back, ancestor removal/recreation, layer membership changes, reset/replacement, and clone/copy distinctions, including between queries. Use actual normal Forest/Layer mutation paths, not final parent/value fingerprints or whole-layer structural epochs.
- [x] Structural dependencies for local edits remain unchanged by otherwise live parent transform/enabled writes. Ancestry transform dependencies detect parent/grandparent transform writes including change-back. Keep these distinctions usable by 05 and 09; do not collapse them into one coarse node/layer token.
- [x] Unrelated sibling topology/state, other layers, names, and object parameter changes do not invalidate the target's narrow structural/ancestry dependencies. A move preserves the same live node handle while invalidating affected dependencies. No wholesale identity regeneration on notifications.
- [x] Deep-chain dependency construction and checking obey existing shared work/scratch/deadline budgets and slice yielding, including cancellation and scratch release; no unbounded full-scene snapshot or hidden uncharged recursion. Cursor/revision behavior remains correct.
- [x] Document the required notifying custom-host topology/mutation contract and audit existing core mutation entry points, preserving viewer/Lua/animation/importer correctness. If a required normal path cannot be covered safely, file and stop on the load-bearing hole rather than accepting a fingerprint fallback.
- [x] Public headless tests observe target/local/ancestry tokens through real normal mutations: parent transform change-back, reparent change-back, unrelated sibling insertion/removal, parent removal/ABA, stable identity on move and distinct identity on clone. Thin MCP tests demonstrate returned dependency fields; full integration and relevant core tests pass.
- [x] Recheck every original 03 acceptance criterion across completed 25–28, including bounded hierarchy filters/records, local matrix/array distinctions, lifetime safety, normal mutation participation, and existing state clients. Record exact combined regression commands and results; no criterion is silently deferred to 04/05.

## Implementation and verification guidance

The confirmed seam is public headless Integration with real Scenes and host-owned safe points, plus thin MCP checks. No test-only hook. Work red/green one behavior at a time. Numeric world/SRT derivation belongs to 04; this ticket supplies dependency observations, not conversion or node writes. Use 27's precise write history for ancestor transform dependencies without treating ancestor changes as child editable-state writes.

Build with at most four jobs using progress.md guidance; logs belong in `.scratch/broad-editing-logs/`. Run changed cases, full `vsr::integration`, relevant core tests, and formatting; record exact commands/results. Before reporting, incorporate a changed integration tip and reverify.

## Out of scope

Numeric world/SRT derivation and edit affordances (04), editing/restoration (05 onward), new structural editing APIs, persistent IDs/archive changes, and broader refactors.

## Comments

### 2026-10-02 iteration — resolved
Salvaged all nine code paths from WIP `yoxxxowkzyrtururvvskklnnpnvyvoxl` onto clean recovery `uyospwkmpzllvqkxpkmstpzoxuzmmppv`; deliverable `pvxoszqrvotmpopnmzumvkkltotuopno`. No further code omissions found. Integration bookmark remains `57fd8a5a4ec634ff13736df340b0b0f9aa85c509`.
Actual observer-contract audit: Forest insertion/explicit-slot insertion stamps creation; copy uses fresh lifetimes; move stamps only its root (descendant chains include it); erase/clear retire actual pool lifetimes. Layer/Scene membership/reset/replacement retire domains without wholesale identity regeneration. Ordered transform stamps exclude enabled-only writes; bounded incremental chain steps use existing visit/slice/cursor state rather than snapshots. Public tests cover parent/grandparent transform change-back, target/ancestor reparent change-back, sibling independence, wrong domains, removal/ABA, clone/relocation, resumed deep-chain equivalence, cancellation/deadline and terminal scratch release; MCP tool admission/status verifies both dependency fields and parent-write distinction end-to-end.
Original 03 aggregate audit across resolved 31/32/26/27/28: compatible object/layer/node modes and intersecting bounded filters/cursors; complete layer/root/node records, hidden/effective enablement, shared-object placements, local asymmetric matrices and explicit array/reference distinctions; genuine lifetime/domain safety; fine editable/structural/ancestry history; normal notification/revision stability; public/MCP and existing clients all pass. No original criterion is deferred to 04/05; world/SRT derivation and editing remain explicitly out of scope.
Source audit confirmed Viewer BaseViewport/ObjectEditor setters and signals, LayerTree enabled/copy paths, Lua LayerBindings setters plus CoreBindings notification exposure, TransformBinding and UsdInstancerFileBinding setters/signals, GLTF tracked insertion, and layer deserialization setters. README documents required coalesced Scene signals for custom hosts/scripts/live deserialization, unsupported raw-memory/pointer rewrites and cross-Forest relinking. Optional UI/Lua/USD execution is not claimed.
Verified: `cmake --build _out/_cmake -j 4 && cmake --install _out/_cmake` → passed using existing Release cache; `_out/_cmake/vsrIntegrationTests '[node-dependency],[node-version],[node-inspection],[node-mcp]'` → 881 assertions/13 cases; `_out/_cmake/vsrIntegrationTests '[node-dependency],[node-version],[node-inspection],[node-mcp],[layer-inspection],[layer-mcp]'` → 1,473 assertions/22 cases; `_out/_cmake/vsrIntegrationTests` → 9,145 assertions/208 cases; `ctest --test-dir _out/_cmake --output-on-failure` → 36/36 passed including Forest/Scene/integration; `clang-format --dry-run --Werror src/vsr/core/Forest.hpp src/vsr/scene/LayerNodeData.hpp src/vsr/scene/LayerNodeData.cpp src/vsr/integration/identity/Identity.hpp src/vsr/integration/inspection/Inspection.hpp src/tests/integration/test_IntegrationIdentity.cpp src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → passed. Current logs: `.scratch/broad-editing-logs/28-continuation-*`; counts above are rerun results, not copied historical full-run counts.
Holes filed: none

### Orchestrator recovery — output-limit interruption after verification

The worker produced structural/ancestry history changes and passing focused,
full-integration, CTest, and formatting logs, but the harness stopped it before
committed status/reporting. Park the code-only attempt; a bounded continuation
must verify all acceptance criteria and commit the actual outcome. No resolved
state is inferred solely from the interrupted process.

### 2026-10-02 split from 03
Terminal replacement of 03: chain 25 → 26 → 27 → 28. The orchestrator must replace the existing 03 blocking edges on 04 and 05 with 28 before dispatching those tickets; this worker did not edit sibling tickets. Resolving 28 means the complete original observation/identity/version contract, not merely ancestry tokens.

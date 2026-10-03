# 21 — Observe complete current host scene boxes

**What to build:** A complete scene-level host-value bounds observation through admitted public headless and thin MCP interfaces, with no callback execution or geometry scan.

**Blocked by:** none
**Status:** wontfix
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 15 before source work. No bounds query, binding or cache exists. Inspector builds admitted safe-point observations with revision tokens and shared ScratchPool accounting; OperationEngine provides replay, current authority, cancellation, deadlines and bounded records. SceneIdentity tracks render-relevant scene content including arrays/layers/transforms. Confirm its notifications cover every dependency used here rather than assuming a scene token is sufficient.

## Acceptance criteria

- [ ] Add admitted `vsr_bounds(request_id, scene_id, optional include_disabled)` through public headless and thin MCP. Scene default means active layers and effectively enabled contributors; explicit true/false and effective defaults participate in canonical identity. Until 22, a node_handle request is explicitly unsupported, never silently treated as a scene query.
- [ ] Expose a host-owned value binding for a complete scene box covering the exact scene identity, current revisions and requested visibility choice. The host supplies the revisions actually covered, not a box automatically stamped current by inspection. Available values return source kind, matched scene/target provenance and independently correct world min/max. Distinguish available, empty, pending and unavailable; missing binding, incomplete coverage and empty content never manufacture a zero box. Pending here is an explicit host-owned value state, not autonomous work.
- [ ] Reject nonfinite/reversed boxes and stale identity, geometry/reference, array-content, layer/structure, transform and visibility dependencies before publication. Conservative whole-scene invalidation is allowed if tests establish full notification coverage. A box from old successful render synchronization must not become current after edits or failed synchronization. CPU scene inspection need not require ready rendering. Host refresh/replacement must itself be safe-point and bounded.
- [ ] Validate bounds policy with defaults of 256 node/geometry steps and 4096 vertex values per slice, 4096 nodes and 1,048,576 vertex values per operation, five seconds from admission and 8MiB shared temporary bounds scratch. Provide an explicit finite cache budget and bounded binding/cache/result storage with observable eviction; reserve/release through shared accounting, including cancellation/deadline/failure. Value observations do not scan geometry or vertices and must not report fictitious scan counters. Reject invalid policies before admission.
- [ ] Preserve scene read authority during work/publication/retrieval, owner minimal receipts without scene-bearing payload after withdrawal, request admission/replay/conflict rules, operation status/cancel and bounded retention. Cache lookup never labels stale boxes current or revives evicted identity; retained operation results preserve historical provenance rather than becoming fresh observations. Reasons are bounded machine-readable codes, not host addresses, arrays or private diagnostics.
- [ ] TDD one public behavior at a time using real Scenes and literal supplied world boxes. Prove all four states, missing/incomplete/invalid/stale values, explicit visibility identity/defaults, geometry references, array writes, transforms/visibility/structure changes, failed sync with old provider revision, host refresh, shared scratch exhaustion/release, finite cache eviction, cancellation/queue deadline and current read withdrawal. Thin MCP proves admission, polling, defaults, replay/conflicts and payload suppression. No driver or scanner qualification is inferred.
- [ ] Build/install changed libraries in existing `_out/_cmake` and `_out` before tests; run positive new public cases and existing integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands/case counts and limitations; no fresh configure, cold build, CUDA disable or success-with-skip claim.

## Out of scope

Node/subtree value observations (22), cooperative provider execution (23), automatic analytic/triangle scanning (16/17/18/03), device acquisition, capture, geometry edits and bulk export. No blocking world-bounds helper, GPU download, driver wait or renderer setup even for device-only/proxy data; only an already-current complete host value can cover such content.

## Comments

Created by the 15 grain decision. This delivers a usable complete scene observation, including both public interfaces, validity, authority and finite storage; it is not a schema/cache-only layer. The validated policy is reusable by 22/23 even though this value-only slice performs no vertex work. All Files are a subset of 15's scope. The next slices add independently usable target/provider capabilities rather than implementing a private horizontal layer.

### 2026-10-03 iteration — wontfix
split into 25, 26. No source work started: the new API/storage plus dependency, lifecycle and two-interface test matrix cannot be safely implemented and verified in this sitting.
25 delivers complete default-visible scene observations through headless and MCP with all four states, supplied revisions, dependency validity, fixed validated limits and finite storage/authority/lifecycle. 26 adds explicit include-disabled selection and configurable validated bounds limits, retaining all 25 behavior.
Existing 22 still names aggregate 21; the orchestrator must reconcile that edge to the completed split (26 transitively includes 25). No sibling, spec/addendum or capture-policy blocker was edited. No retry WIP was supplied.
Verified: `jj file track --include-ignored .scratch/camera-capture/issues/{25-default-visible-scene-bounds,26-selectable-scene-bounds-policy}.md` and Python assertions of status/milestone/scope/serial edges/complete observable acceptance coverage plus Python whitespace checks and `jj diff --stat` → passed. `jj diff --check` is unsupported by this jj version; used the explicit whitespace check instead. Documentation-only split; no build/runtime test, bounds implementation or device qualification claimed.
Holes filed: none

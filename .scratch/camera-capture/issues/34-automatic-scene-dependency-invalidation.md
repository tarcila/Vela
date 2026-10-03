# 34 — Invalidate scene dependencies on public layer mutations

**Status:** wontfix
**Milestone:** 1
**Blocked by:** 29
**Specification:** [Camera & Capture](../spec.md), Decision 3; authorized decision in29.
**Files:** src/vsr/core/Forest.hpp, src/vsr/scene/Scene.hpp, src/vsr/scene/Scene.cpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/SceneDependency.hpp, src/vsr/integration/identity/Identity.hpp, src/tests/test_Forest.cpp, src/tests/test_Scene.cpp, src/tests/test_Layer.cpp, src/tests/test_LayerNodeData.cpp, src/tests/integration/test_IntegrationIdentity.cpp

## Current behavior

29 contains six real-Scene public mutation reproductions that leave both scene and inspection tokens unchanged. Payload noteEdit advances precise local histories but not whole-scene validity; instance-parameter setters do neither. Direct public Forest edits bypass manual scene signals. Many UI/network/Lua/IO callers already signal manually. Forest also backs DataTree and is a generic core container. Copy/move assignment currently transfers m_layer from the source; Forest relocation and subtree copy have distinct identity semantics. Scene owns shared Layer pointers that callers can retain. A raw epoch pointer is not proved safe merely by Scene member order.

## Desired behavior

- [ ] Automatic scene-owned dependency invalidation covers attached public node payload/visibility/object-reference/instance-parameter edits and hierarchy insert/erase/clear/copy/reparent/removal/reuse/change-back. Reproduce all six29 classes plus instance parameters through actual public APIs, not only explicit signal controls. Both externally visible scene/inspection revisions advance; repeated reads without writes remain stable and foreign scenes stay independently qualified.
- [ ] Revision reads remain bounded O(1), no scene fingerprint/traversal and no growing per-query history. Use an owned/lifetime-qualified scene epoch or equivalent. Identity observes dependency changes consistently before all revision snapshots/comparisons; do not acknowledge an epoch without invalidating both relevant tokens. Retain normal signal-driven invalidation independently.
- [ ] Preserve render/UI UpdateDelegate callback delivery/counts, existing explicit notifications, beginLayerEditBatch structural batching and beginUpdateBatch work-coalescing semantics. Do not emit coarse scene callbacks from every raw setter or quietly convert callers to a new notification contract. Conservative scene invalidation is allowed; notification overhaul is not.
- [ ] Preserve precise editVersion/transformVersion, ancestry ordering/change-back, node lifetime/topology identities across relocation/reparent/copy, object use counts and destination ownership. Instance-parameter dependency invalidation must not silently change unrelated existing precise edit/transform semantics. Detached temporaries are not attached nodes. Copy/assignment/move-out/Forest storage relocation need distinct handling; destination attachment cannot become a stale source-Layer pointer. Do not introduce gratuitous API incompatibility or reinterpret cross-Scene object indices.
- [ ] Generic Forest remains Scene-independent and default behavior/DataTree unchanged. Any optional generic mutation/binding seam must be minimal and lifetime-safe, including root construction, reused slots, copies and relocation. Never expose an unqualified mutable pointer to a destroyed Scene epoch. Retained Layers or detached payloads cannot use newly dangling owner/epoch/object-resolution pointers after owner destruction. Do not blindly notify from constructors/destructors. Inspect existing lifetime behavior before choosing a hook design.
- [ ] TDD with real Scenes: all missing mutation classes; payload copy/assignment/cross-layer copy followed by source destruction; correct destination invalidation; move-out versus reserve relocation; root construction; subtree reparent/copy/removal/reuse/change-back; instance setters/clear; retained Layer/temporary lifetime; object-count balance; alternating revision getter order and explicit signals. Assert no new callbacks and unchanged batching/history. Core Forest without the hook and DataTree regression behavior remain unchanged.
- [ ] Reuse existing CUDA/UI/network/Lua/SDL3-enabled `_out/_cmake`; build/install before positive new identity/Forest cases, existing integration/broad-editing regressions and full applicable CTest. Exercise memory-safety cases with available sanitizers/Valgrind where feasible and report what actually ran. No cold configure/CUDA disable, absent test match or fake sanitizer success.

## Out of scope

Host boxes/storage/MCP implementation28, geometry scans/providers, PNG33/cap changes, capture exposure/qualification, broad callback/API overhaul, standards cleanup30/31 and unrelated modules. No spec/addenda edits, rawgit, remote publication or original pi-integration user artifacts.

## Comments

### Authorized human disposition and design constraints

User approved separately scoped scene/core repair while preserving existing callbacks, batching, precise histories and generic Forest independence.29 is the resolved authorization question, not a repair-completion claim.28 waits for this ticket's full replacement chain if split.

Fresh planner proposed Scene epoch + optional generic Forest hook + payload noteEdit forwarding + O(1) Identity folding. Fresh advisor identified fatal omissions in the naive raw-pointer variant: retainable shared Layers, destination ownership during assignment/subtree copy, detached temporaries, move-out versus relocation and instance history changes. Treat that design as a candidate, not mandated code. Prefer lifetime-qualified owned context and optional generic destination-payload binding if needed; prove semantics with the tests above. Scope does not require inventing unsupported cross-Scene transfers.

If20minutes cannot deliver the full repair, split **before coding** into complete independently observable vertical validity slices with preserved guarantees, not schema/hooks/tests-only layers. Parent becomes wontfix; all28 dependency reconciliation waits for the terminal replacement. A partial repair does not qualify complete host bounds. Escalate a load-bearing compatibility problem rather than weakening requirements.

### 2026-10-03 iteration — wontfix
split into 35, 36, 37 before source changes. The full repair plus lifetime/ownership proofs and enabled build/install/regressions is too large for one20-minute sitting: Forest membership, subtree destination binding and payload assignment/move-out semantics are separate public behaviors, not one safe noteEdit patch.
35 delivers actual insertion/removal/clear/reuse revision invalidation;36 delivers actual copy/reparent invalidation with destination ownership;37 delivers attached payload/instance/assignment invalidation and terminal cumulative qualification. Each preserves callback/batching/history/lifetime guarantees and O(1) reads for its slice; all Files are subsets of34. Original acceptance boxes remain unchecked; no repair or complete bounds validity is claimed.
Orchestrator reconciliation required:28 must wait for terminal37 (transitively35/36), replacing its aggregate34 edge. No sibling tracker or ledger was edited; authorization29 remains resolved. No source/spec/addenda/user configuration/artifact changes, retry WIP, builds, code tests or sanitizer runs. Prior work was read; nothing was salvaged because retry is none.
Verified: `jj diff --summary` → exactly1 modified parent ticket and3 added replacement tickets; no source changes. Inline Python split validation →3/3 children pass ready-for-agent/milestone1/Files-subset checks, edges35→29,36→35,37→36, parent wontfix with7/7 original criteria unchecked. New positive code-test cases/assertions:0/0 (no implementation); baseline tests were not rerun or presented as repair evidence.
Holes filed: none.

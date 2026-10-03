# 36 — Invalidate revisions on subtree copy and reparent

**Status:** ready-for-agent
**Milestone:** 1
**Blocked by:** 39
**Specification:** [Camera & Capture](../spec.md), Decision 3; authorization in 29; second vertical replacement of 34.
**Files:** src/vsr/core/Forest.hpp, src/vsr/scene/Scene.hpp, src/vsr/scene/Scene.cpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/SceneDependency.hpp, src/vsr/integration/identity/Identity.hpp, src/tests/test_Forest.cpp, src/tests/test_Scene.cpp, src/tests/test_Layer.cpp, src/tests/test_LayerNodeData.cpp, src/tests/integration/test_IntegrationIdentity.cpp

## Current behavior

Split from 34 before source changes. Read 34's Comments, 29 and implemented35 before starting. Public `node->container()->copy_subtree` and `move_subtree` manipulate membership directly. Reparent advances topologyVersion but does not notify SceneIdentity. Copy constructs `T(source->value())`; LayerNodeData copy construction retains source m_layer and increments that owner's object use count. Destination ownership is therefore not guaranteed by simply adding a scene epoch hook. Forest relocation and subtree cloning have different identity semantics. Forest is Scene-independent.

## Desired behavior

- [ ] With real Scenes, public subtree reparent and copy invalidate both externally visible scene and inspection revisions automatically. Reparent change-away/change-back remains observable even without an intervening read. Copies/removal/reuse cannot resurrect old validity. Exercise same-layer operations and actual supported same-Scene cross-layer copies through public Forest APIs. Foreign Scenes remain independently qualified; no unsupported cross-Scene transfer is invented or object index reinterpreted.
- [ ] Copied nodes are bound to their destination Layer, not a stale source Layer. Copies retain appropriate value/default/instance data and acquire distinct node lifetimes; reparent and reserve relocation preserve lifetime and precise payload history. Verify public `layer()`, object resolution and use-count balance after source Layer removal/destruction. Source mutation/removal must not corrupt copied destination values or attachment. Subsequent destination membership changes must invalidate the correct Scene.
- [ ] Preserve precise editVersion/transformVersion and ancestry ordering/change-back semantics, topology identities, object counts, root restrictions and invalid/no-op reparent behavior. Preserve existing public APIs; any optional generic destination binding seam must be minimal and default-off/Scene-independent, including root construction, reused slots, copies and relocation. Do not solve relocation by treating every move as an editable move-out.
- [ ] Reuse/extend35's lifetime-qualified validity mechanism with O(1) revision reads, no scene fingerprint or growing history, and consistent folding before snapshots/comparisons. Both tokens invalidate regardless of alternating getter order. Retain normal explicit signal invalidation independently. Retained Layers and detached/copied payloads cannot acquire dangling owner/epoch/object-resolution pointers after source or destination destruction. Constructors/destructors are not blind notification points.
- [ ] No new render/UI UpdateDelegate callbacks for direct copy/reparent or raw setters. Preserve callback counts, explicit notifications, beginLayerEditBatch and beginUpdateBatch semantics. Do not change unrelated precise histories or setter behavior as a shortcut.
- [ ] TDD actual public copy/reparent behavior with real Scenes, including nonleaf subtree copies, same-Scene cross-layer copy followed by source destruction, destination ownership and object count balance, reparent away/back, copies followed by removal/reuse, retained destination Layer destruction, root/no-op/cycle restrictions, reserve relocation, alternating getters, stable reads and explicit signals. Assert callback counts, batching, lifetime/topology/edit history invariants. Keep35's positive regressions and generic Forest/DataTree behavior passing.
- [ ] Build/install in existing CUDA/UI/network/Lua/SDL3-enabled `_out/_cmake` and `_out` before positive new tests. Run integration/broad-editing regressions and full applicable CTest, plus available Valgrind/sanitizers on source-destruction/ownership cases where feasible. Record exact commands and actual positive case/assertion counts and limitations; no cold configure, CUDA disable, absent matches or fake sanitizer success. Bounded text logs only in `.scratch/camera-capture-logs/36-*`.

## Out of scope

Payload mutator/assignment/move-out and instance-parameter invalidation (37); complete host bounds28; encoder33; callback overhaul, precise-history redesign, arbitrary cross-Scene transfers, unrelated cleanups. No complete scene-bounds validity claim from this subset. No spec/addenda/sibling/ledger/user configuration/artifact changes, rawgit or publication.

## Comments

### 2026-10-03 split — ready-for-agent
Independent observable deliverable: public subtree copying/reparenting actually invalidates revisions while preserving destination ownership and lifetime semantics. Not merely a binding seam or test slice. Files are a subset of34; implementation is unstarted. Depends on35 and feeds terminal37. 28 must remain blocked on37 (including35/36 transitively); the orchestrator reconciles that edge, not this ticket's implementer. Payload mutation repair remains unqualified until37.

### 2026-10-03 orchestrator — membership split reconciled
35 split into38 insertion/lifetime→39 destructive membership and cumulative validity. Terminal39 now gates this transfer slice; insertion alone cannot qualify all membership behavior. No original requirement was removed.

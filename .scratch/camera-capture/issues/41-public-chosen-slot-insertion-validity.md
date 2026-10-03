# 41 — Invalidate revisions on chosen-slot public insertion

**Status:** ready-for-agent
**Milestone:** 1
**Blocked by:** 43
**Specification:** [Camera & Capture](../spec.md), Decision 3; authorization 29; terminal vertical replacement of 38.
**Files:** src/vsr/core/Forest.hpp, src/vsr/scene/Scene.hpp, src/vsr/scene/Scene.cpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/SceneDependency.hpp, src/vsr/integration/identity/Identity.hpp, src/tests/test_Forest.cpp, src/tests/test_Scene.cpp, src/tests/test_Layer.cpp, src/tests/test_LayerNodeData.cpp, src/tests/integration/test_IntegrationIdentity.cpp

## Current behavior

Read 38's complete criteria and Comments, 35/34 split context, resolved authorization 29 and implemented 40 before starting. Forest::insert_last_child_at and ForestNode::insert_last_child_at do not notify scene dependency validity. ObjectPool::insert_at rejects INVALID_INDEX and occupied slots, creates default-constructed gap payloads for a new high index, and assigns into successful chosen slots. It constructs insertion temporaries even on failure; reserve/append can relocate nodes. These storage moves are not editable payload move-out operations. Preserve actual supported chosen-index semantics, not invented ones.

40 supplies ordinary first/last/Scene-helper invalidation and the required both-owner lifetime-safe foundation. Neither 40 nor this ticket introduces subtree transfer or attached payload edit invalidation. Source for this split is unstarted at ticket creation.

## Desired behavior

- [ ] Through real Scene-owned Forest and ForestNode public insert_last_child_at APIs, successful chosen-slot insertion automatically invalidates both externally visible scene and inspection revisions. Cover insertion at next index, high chosen index with gaps, erased-slot reuse and reuse after change-away/change-back with no intermediate read. Occupied/INVALID_INDEX failure adds no membership or automatic validity change; gap slots remain empty. New memberships have new node lifetimes. Empty membership may conservatively invalidate scene and must invalidate inspection.
- [ ] Reuse 40's fixed per-scene owned lifetime-qualified mechanism. Revision snapshots/comparisons stay bounded O(1), without scene walks/fingerprints or growing query history. Both getter orders invalidate both tokens; repeated reads and foreign Scenes remain independently qualified. Explicit signal invalidation remains independent, including controls around failed insertions.
- [ ] Successful slots, gaps, insertion failure temporaries and reserve/append relocation acquire no stale attachment/owner state. Detached temporaries are not attached nodes. Preserve precise editVersion/transformVersion, surviving node lifetime/topology identities and exact object use-count balance on insertion, failure, reuse, removal as setup and relocation. Do not treat storage moves as editable move-out. Generic Forest seam stays optional/minimal/Scene-independent/default-off, with Forest/DataTree behavior unchanged. No blind constructor/destructor notifications or unsupported cross-Scene index reinterpretation.
- [ ] Keep 40's complete safe ownership foundation valid across chosen-slot operations. Retained object-containing Layers after Scene destruction, detached object-containing payloads after Layer destruction while Scene is live, and detached payloads after Scene destruction all resolve/access/destroy safely. Qualify both owner lifetimes, not merely Scene lifetime/member order. Chosen-slot successes/failures must not leak object counts or dereference dangling epoch/Scene/Layer/object-resolution pointers. Do not defer this safety to 39/36/37 or weaken 40's guarantees.
- [ ] Direct chosen-slot insertion adds no coarse render/UI UpdateDelegate callbacks. Exact ordinary helper explicit signals and callback counts remain unchanged. Assert beginLayerEditBatch structural batching and beginUpdateBatch work coalescing for mixed ordinary/chosen-slot insertion and failed attempts. Conservative revision invalidation is not a callback contract overhaul.
- [ ] TDD one actual chosen-slot public behavior at a time through real Scenes/tokens, not private epoch controls. Cover all success/failure/gap/reuse paths, root safety, detached temporaries, reserve relocation, both getter orders, stable reads, independent Scenes and explicit controls. Assert exact object counts, callback/batching counts and precise histories/lifetimes. Actually destroy owners in lifetime cases. Run 40's ordinary public insertion/helper/root/temporary/reuse/lifetime cases together with these tests; generic Forest/DataTree regressions pass without a hook.
- [ ] Terminal insertion qualification reviews every one of 38's seven original acceptance criteria and proves their union across 40–41, including all ordinary and chosen-slot APIs, both-owner lifetime safety, O(1) validity, precise histories, callbacks/batching, relocation and object counts. No original 38 criterion is deferred to 39/36/37. This qualifies insertion only, not destructive membership, payload/transfer validity or complete host bounds.
- [ ] Reuse existing CUDA/UI/network/Lua/SDL3-enabled _out/_cmake; build and fresh-install changed libraries into _out before positive tests. Run new public cases, 40 cases, existing integration/broad-editing regressions, core Forest/DataTree tests and full applicable CTest. Run available Valgrind/sanitizers on lifetime cases where feasible; record exact commands, actual positive case/assertion counts and limitations. Formatting checks pass. No cold configure/CUDA disable, absent matches or fake sanitizer claims. Bounded text logs only in .scratch/camera-capture-logs/41-*.

## Out of scope

Automatic destructive membership invalidation and cumulative original-35 qualification (39); subtree copy/reparent/destination transfer validity (36); attached payload setter/assignment/move-out and instance parameters (37); complete host bounds (28); encoder 33; callback overhaul, precise-history redesign, global container migration and unrelated cleanup. No complete bounds claim, weakened guards, spec/addenda/sibling/ledger/user configuration/artifact/rawgit/push edits.

## Comments

### 2026-10-03 split — ready-for-agent
Split from 38 before source changes. This implements actual chosen-slot invalidation and cumulatively qualifies insertion, not a tests-only terminal task. Depends on ordinary insertion plus safe lifetime foundation 40. All Files are subsets of 38. The orchestrator must replace 39's aggregate-38 blocker with terminal 41 (including 40 transitively) before destructive membership work; do not edit 39 here. 39 still qualifies the original-35 membership union, 37 the complete original-34 repair, and 28 remains blocked on 37. No source implementation or qualification is claimed by this split.

### 2026-10-03 orchestrator — ordinary insertion split reconciled
40 split into42 Scene-helper insertion/lifetime→43 direct ordinary insertion and cumulative qualification. Terminal43 now gates chosen-slot validity; full lifetime and callback/history guarantees remain binding.

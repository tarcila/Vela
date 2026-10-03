# 23 — Observe bounded cooperative host bounds providers

**What to build:** Complete scene/node observations from explicitly registered cooperative host providers through headless and MCP, retaining bounded progress but never partial authoritative results.

**Blocked by:** 22
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 15 before source work. After 21/22, complete current host-owned scene/subtree values are observable with revision validation, visibility semantics, finite storage and current authority through both public interfaces. Pending is a supplied value state; there is no admitted cooperative provider execution. Inspector/OperationEngine already offer safe-point yielding and cancellation/deadline contracts.

## Acceptance criteria

- [ ] Extend explicit host binding to a cooperative provider returning complete revision-qualified scene/subtree boxes for the requested target/visibility choice. Public `vsr_bounds` observations and polling distinguish pending, empty, unavailable and available; publish only complete validated results with source kind, actual matched revisions and meaningful local/world boxes as established by 21/22. A missing/unsupported/incomplete provider never produces a zero box or partial union.
- [ ] Run provider work only on the owning-thread safe point with a genuinely bounded cooperative contract. Supply validated per-slice remaining budgets, resumable bounded state and actual progress counters: defaults 256 node/geometry steps or 4096 vertex values per slice, 4096 nodes and 1,048,576 vertex values total, five seconds from admission. Enforce total/slice exhaustion, pending wake/yield semantics and no-progress behavior without busy-spinning. Count actual provider work; no fictional automatic scans or one-call-per-pump claim of boundedness. Arbitrary blocking callbacks are not admitted as qualified providers.
- [ ] Reserve progress/scratch/cache/result bytes before retaining them using the shared 8MiB temporary scratch and explicit finite cache budget from 21/22. Bound concurrent pending providers, state size and retained progress; observable eviction releases owned state and reservations. Cancellation/deadline/provider failure/capacity exhaustion cannot publish a partial or late authoritative result, leak reservations, or make a terminated provider restart on replay. Completed-result retention remains bounded under existing operation rules.
- [ ] Revalidate actual scene/node identity and geometry/reference/array-content/structure/ancestor/transform/visibility provenance before every progress slice and publication. Provider output declares the revisions actually covered; mismatched/stale/incomplete/invalid boxes are unavailable, including old ready-view boxes after failed render synchronization. Removal/reuse and binding replacement cannot redirect in-flight or cached work. Reuse 21/22's validated value contract rather than duplicating weaker checks.
- [ ] Enforce current scene read authority during work/publication/retrieval, preserving owner minimal receipts after revocation without scene-bearing payload. Preserve admission canonical identity including effective visibility defaults, replay/conflicts, status/cancel, bounded retention and finite machine-readable reasons. Exceptions/faults must not leak callback diagnostics, addresses or arrays.
- [ ] This provider class is host-only/GPU-work-free: no driver setup, GPU download, synchronization wait, renderer creation or blocking world-bounds call. Device-only/proxy/ambiguous content is unavailable unless already covered by a complete current host box. Driver provider lifecycle is explicitly unsupported, not disguised behind a pending state or NO_WAIT flag.
- [ ] TDD controllable public providers with real Scenes and independently literal local/world boxes. Prove bounded actual progress, multiple safe points, pending/wake, complete versus incomplete results, nonfinite/reversed/stale output, changes during pending work, failed sync revision mismatch, removal/reuse, shared scratch contention, finite cache/progress eviction, cancellation/deadline/fault cleanup, request replay/conflicts and current read withdrawal. Thin MCP exercises admission, polling/progress, defaults, conflicts and payload suppression. No private hooks, automatic scanners or driver qualification claims.
- [ ] Build/install freshly changed libraries in existing `_out/_cmake` and `_out`, then run positive new public cases and all existing integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands/case counts/limitations. Check 21–23 jointly satisfy every original 15 acceptance bullet before downstream analytic work proceeds; no fresh configure/cold build/CUDA disable.

## Out of scope

Automatic sphere/subtree/cone/cylinder/triangle scanners (16/17/18/03), driver acquisition, capture, structural editing, bulk export and callback access to unrestricted diagnostics or GPU APIs.

## Comments

Created by the 15 grain decision. This completes a distinct observable capability: an explicitly registered host can advance pending bounds work cooperatively and retrieve a complete current scene/subtree result through both interfaces. It is not a provider-interface-only or cleanup-only layer. 21–23 replace all of 15; existing 16's dependency on split parent 15 must be expanded by the orchestrator to the completed replacements, not treated as satisfied by wontfix. No sibling ticket or source was changed by this split.

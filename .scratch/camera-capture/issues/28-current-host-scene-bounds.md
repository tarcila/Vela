# 28 — Observe current complete host scene bounds

**What to build:** Extend the admitted default-visible scene-bounds query with complete revision-qualified host values, all four observation states, conservative dependency validity and finite observable storage through public headless and thin MCP interfaces.

**Blocked by:** 27, 29, 37
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 25 before source work. Once 27 resolves, public headless and MCP vsr_bounds support omission/false default-visible scene queries, rejecting true/node before admission, and report missing_binding without a box. Admission/replay, shared scratch, five-second queue-inclusive deadline, cancellation, bounded history and current read suppression are tested. No host bounds value exists yet. SceneIdentity notifications for parameters/references, array unmap, layer transforms/structure, activation and filtering are only candidates for conservative invalidation, not proof of complete dependency coverage.

## Acceptance criteria

- [ ] Add a public host-owned complete scene value binding with exact scene identity, revisions actually covered, default-visible selection, source kind and world min/max. Installation/replacement is bounded owning-thread safe-point work. Do not automatically stamp an arbitrary supplied box current. Host values may describe CPU content or an already-acquired provider value; no new provider callback or device acquisition occurs. Queries retain 27's omission/false identity rules and unsupported true/node rejection.
- [ ] Through the existing admitted query return available, empty, pending and unavailable with matched scene/target provenance. Available returns independently correct literal world min/max. Empty has no box; pending is only an explicit supplied host state, never autonomous work. Missing/incomplete coverage is unavailable, never a partial union or zero box. Reject nonfinite/reversed boxes and foreign/stale identities or revisions before publication; report bounded machine-readable reasons. Bound source/reason metadata without unbounded diagnostic strings.
- [ ] Prove conservative whole-scene dependency invalidation using real Scene mutations: geometry parameters and references, array writes/unmap, node/local/ancestor transforms, visibility/filtering, active-layer changes, structure/membership, removal/reuse and change-back. Repair notifications within the listed scope where needed; if a required fix is outside scope, file/block on a hole instead of claiming coverage. An old successful-render provider revision stays stale after edits and failed synchronization. CPU observations do not require ready rendering. Explicit host refresh covering newly current revisions restores availability without device work.
- [ ] Retain 27's validated fixed defaults: 256 node/geometry steps and 4096 vertex values per slice, 4096 nodes and 1,048,576 vertex values per operation, five-second admission-to-completion deadline and at most 8MiB shared temporary bounds scratch. Define explicit finite binding/cache entry and byte budgets with observable eviction; bound values/results and any replacement bookkeeping, and do not retain growing identity history. Actual temporary allocations reserve/release through the Inspector's shared accounting. Value-only queries do not invent scan counters. Exhaust fixed capacity using supplied values/held existing inspection work, not configurable policy.
- [ ] Preserve scene read authority at execution, publication and retrieval, including value replacement while requests are queued. Owner minimal receipts after withdrawal have no scene/box/provenance payload. Reuse request replay/conflicts, status/cancel and bounded result retention. Historical available results keep original revisions after edits/replacement/eviction, never masquerade as fresh values. Eviction cannot revive old identities; cache miss/staleness reasons are bounded and machine-readable. Cancellation, deadline, failure and rejected admission release temporary reservations.
- [ ] TDD one observable behavior at a time through the public headless seam with real Scenes and independently supplied literal world boxes. Prove all four states, missing/incomplete/invalid/stale values, default-visible provenance, every dependency class above, provider revision after failed sync, safe host refresh, finite storage eviction, shared scratch exhaustion/release, cancellation, queue deadline and read withdrawal. Thin MCP proves admission/polling and all four state payloads, defaults, unsupported selections, replay/conflicts and payload suppression. Run 27's cases unchanged. Do not infer scanner/callback/GPU/driver/capture qualification.
- [ ] Build/install changed libraries in existing `_out/_cmake` and `_out` before tests. Run positive new cases and existing integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands, case counts and limitations. No fresh configure, cold build, CUDA disable, absent test match or success-with-skip claim.

## Out of scope

Including disabled/inactive contributors or configurable limits (26); node/subtree values (22); cooperative callbacks (23); automatic analytic/triangle scans/unions (16/17/18/03); capture/device acquisition, edits and export. No blocking bounds helper, GPU download, driver wait or renderer setup. Specialized/device-only content requires an already-current complete host value.

## Comments

Created by the 25 grain decision. This delivers actual complete host bounds through both interfaces, not just a cache/schema layer. All Files are a subset of 25. Together 27 and 28 preserve every acceptance criterion of 25, including dependency validity, finite storage, truthfulness and lifecycle; neither delegates basic correctness to 26. Existing 26 still names aggregate 25 and must be reconciled by the orchestrator to 28 (which transitively includes 27); no sibling ticket is edited by this split.

### 2026-10-03 iteration — ready-for-human
Read completed 27 and dependent 26, frozen spec and prior work; no retry WIP. Before source implementation, a public headless diagnostic with a real Scene confirmed local/ancestor transform writes, ancestor visibility, Layer erase, empty-node insertion and direct Forest insertion do not advance SceneIdentity scene/inspection tokens; an explicit host signal control does. This is not complete bounds dependency validity.
Filed 29 for human authorization of the required scene/core invalidation repair outside this ticket's Files, as required by the third criterion. No current box was published, no acceptance box ticked, and no spec/addenda/sibling/source/user configuration/artifact changes or device work. All supplied-value/state/storage/MCP implementation remains for a later iteration after 29; baseline tests passing does not resolve this ticket.
Verified: `cmake --build _out/_cmake -j 4` then `cmake --install _out/_cmake` → passed, existing CUDA/UI/network/Lua/SDL3/MCP enabled configuration and fresh install, no configure/fallback.
Verified: `.scratch/camera-capture-logs/28-notification-probe` (compiled from the cached Identity test compile command and existing integration link libraries) → intentional exit 1, six missing-invalidation cases of seven, one positive signal control; durable reproduction and evidence are in 29. No new functionality claimed.
Verified: `_out/_cmake/vsrIntegrationTests '[bounds],[bounds-mcp]'` → 8 cases passed (344 assertions); `_out/_cmake/vsrIntegrationTests '[integration]'` → 276 cases passed (36,320 assertions), unchanged 27 and integration/broad-editing baseline, no skips/absent match. Diagnostic/build/install/regression logs remain ignored under `.scratch/camera-capture-logs/28-*`.
Holes filed: 29-scene-bounds-invalidation-scope (extends)

### 2026-10-03 iteration — ready-for-human
Re-read the frozen spec, completed 27, dependent 26 and unresolved 29 against the unchanged source and prior commits; no retry WIP. Ticket 29 still has no authorization or repair, so the existing load-bearing block remains. The dispatch does not authorize changes outside this ticket's Files.
Rechecked `Layer::erase`, `Scene::insertChildNode` and public payload mutation paths and reran the existing diagnostic: six mutation classes still leave scene/inspection revisions unchanged, while the explicit notification control advances both. Do not implement/publish supplied current bounds until 29's authorized repair and real-Scene coverage establish conservative validity; all acceptance boxes remain unchecked.
No source/spec/addenda/sibling/user configuration/artifact changes, new tickets, hidden device work or functionality claims. Existing hole 29 is reused, not duplicated; remaining host-value/state/storage/MCP work is unchanged.
Verified: `cmake --build _out/_cmake -j 4` then `cmake --install _out/_cmake` → passed using existing CUDA/UI/network/Lua/SDL3/MCP enabled configuration and fresh `_out` install, no configure/fallback.
Verified: `.scratch/camera-capture-logs/28-notification-probe` → intentional exit 1, six missing-invalidation cases of seven with one positive notification control; this is unresolved red evidence, not a passing qualification.
Verified: `_out/_cmake/vsrIntegrationTests '[bounds],[bounds-mcp]'` → 8 cases passed (344 assertions); `_out/_cmake/vsrIntegrationTests '[integration]'` → 276 cases passed (36,719 assertions), unchanged baseline only, no skips/absent matches. Logs remain ignored under `.scratch/camera-capture-logs/28-recheck-*.log`.
Holes filed: none (existing 29-scene-bounds-invalidation-scope remains blocking)

### 2026-10-03 orchestrator — human decision applied
29's scope authorization is resolved.34 now gates this ticket's complete host values; if34 splits, wait for its terminal replacement chain. Neither user approval nor baseline tests establish dependency validity. This execution fixes the two blockers only;28 and the broader camera graph do not automatically resume before the requested cap-impact discussion.

### 2026-10-03 orchestrator — repair split reconciled
34 split before source changes into35 membership→36 subtree transfers→37 attached payloads and full qualification. Terminal37 now gates this ticket; partial membership validity does not qualify complete current boxes. All original requirements retained.


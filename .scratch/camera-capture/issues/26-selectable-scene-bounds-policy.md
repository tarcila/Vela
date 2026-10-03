# 26 — Observe host scene boxes with explicit visibility

**What to build:** Extend the usable scene bounds observation to explicit include/exclude-disabled choices and validated configurable bounds policy, through public headless and thin MCP interfaces.

**Blocked by:** 28
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 21 before source work. After 25, vsr_bounds supplies complete current host values for default-visible scenes through admitted public headless and MCP queries. Omission/false are supported; true and node_handle reject explicitly. All four states, supplied revision provenance, conservative dependency invalidation, finite storage/shared scratch and operation authority/lifecycle are already implemented and tested. The bounds policy uses validated fixed defaults; no scanner or provider callback exists.

## Acceptance criteria

- [ ] Support explicit `include_disabled:true` for complete scene values including disabled/inactive contributors; retain scene omission's active-layer/effectively-enabled default and explicit false. Bind the original choice and effective defaults to canonical request identity. A box supplied for one visibility choice must not satisfy another. Thin MCP schema/metadata and public headless behavior agree; node_handle remains explicitly unsupported until 22.
- [ ] Hosts can supply independent complete values for each supported visibility selection at revisions they actually cover. Literal visibly different world boxes demonstrate default/false versus true with inactive layers, disabled nodes and disabled ancestors. Available/empty/pending/unavailable, source kind and matched scene/target provenance retain 25's contract. Incomplete or absent selection coverage never falls back to another selection or becomes an authoritative partial union; nonfinite/reversed boxes remain unavailable.
- [ ] Qualify invalidation for both selections with geometry references, array writes, layer activation, node/ancestor transforms, effective visibility, structure, removal/reuse and change-back. Conservative whole-scene invalidation remains allowed. A formerly ready-view supplied box stays stale after failed synchronization; host refresh explicitly covers current revisions and selection. Historical retained results remain historical even after binding replacement/eviction.
- [ ] Add validated host bounds-policy configuration, retaining defaults of 256 node/geometry steps and 4096 vertex values per slice, 4096 nodes and 1,048,576 vertex values per operation, five seconds from admission and 8MiB shared temporary bounds scratch. Reject zero, nonfinite, overflow/inconsistent and otherwise invalid policies before admission or request ID consumption. Apply meaningful finite limits to value work and storage without fictitious node/vertex scan counters. Provide explicit finite binding/cache budgets; all configured bounds scratch shares Inspector accounting rather than creating a second unaccounted pool. Policy changes cannot let replay reinterpret an old accepted request; bind correctness-bearing policy/defaults according to existing canonical rules.
- [ ] Bound simultaneous per-selection binding/cache/result storage and make eviction observable. Tests with deliberately narrow valid budgets prove exhaustion, replacement/eviction and reservation release on cancellation, queue deadline, failure and admission rejection. No cache entry revives evicted identity or automatically stamps newly covered revisions; no autonomous work starts for host pending values. Preserve 25's read checks during work/publication/retrieval and payload-free owner receipts after withdrawal.
- [ ] TDD public headless and thin MCP behavior for omission/explicit false/true, identity replay/conflicts, selection-specific empty/pending/unavailable, policy validation before admission and exact literal world min/max. Extend real-Scene dependency tests for disabled/inactive contributors and both selections; verify storage/scratch exhaustion/release and bounded retention across visibility/policy changes. Run all 25 cases unchanged. No scanner/provider callback or device qualification is inferred.
- [ ] Build/install changes in existing `_out/_cmake` and `_out` before relevant tests; run positive new cases and existing integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands/case counts and limitations; no fresh configure/cold build/CUDA disable or success-with-skip claim.

## Out of scope

Node/subtree values (22), cooperative callbacks (23), automatic analytic/triangle scans or unions (16/17/18/03), capture policy (20/24), driver bounds, downloads/waits/renderer setup, geometry edits and export. This configures bounds policy only, never capture policy or the frozen spec.

## Comments

### Orchestrator dependency reconciliation after25

25 split into27→28. This visibility/policy extension waits for complete host
scene values and their validity/storage guarantees from terminal28, not merely
a missing-binding protocol query. No original bounds requirement was dropped.

Created by the 21 grain decision. This adds independently observable all-content scene observations and host-selectable resource constraints, not a horizontal private implementation layer. All Files are a subset of 21; serialized after 25 to preserve its complete default-visible capability. Together 25 and 26 cover every acceptance criterion of 21; subsequent node/provider work still waits for the complete aggregate.

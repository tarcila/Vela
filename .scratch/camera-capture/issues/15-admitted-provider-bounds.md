# 15 — Observe revision-qualified host bounds through headless and MCP

**What to build:** Complete admitted scene/node bounds observation using explicitly supplied current host boxes, without device acquisition or automatic geometry scanning.

**Blocked by:** none
**Status:** wontfix
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp (new if needed), src/vsr/integration/identity/Identity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 02. No bounds query/provider/cache exists. Inspector already supplies admitted safe-point observations, shared scratch accounting and revision tokens; SceneIdentity supplies lifetime-safe node handles and incremental ancestry observations. Reuse those public contracts, not rendering world-bounds helpers.

## Acceptance criteria

- [ ] Add admitted vsr_bounds(request_id, scene_id, optional node_handle, optional include_disabled) through the public headless seam and thin MCP. Bind effective scene/node visibility defaults and explicit choices to canonical request identity; scene defaults to active/effectively enabled content, node defaults to its entire existing subtree and reports effective enablement separately.
- [ ] An explicit host-owned value/provider binding supplies a complete box for the requested target/visibility choice and the revisions actually covered. Return available/empty/pending/unavailable distinctly, source kind, matched scene/target provenance, world min/max and meaningful node-local min/max. No missing binding, empty target or incomplete provider union becomes a fabricated zero box.
- [ ] Validate finite ordered boxes and complete coverage; reject stale geometry/reference, array-content, node identity/structure/ancestor, transform and visibility provenance. Removal/reuse cannot redirect a cached result. Conservative invalidation is permitted; stale boxes cannot be returned as current after change or failed render synchronization.
- [ ] Validate host bounds limits using the spec defaults: 256 node/geometry steps and 4096 vertex values per slice, 4096 nodes and 1,048,576 vertex values per operation, five seconds from admission, 8MiB shared temporary bounds scratch, and an explicit finite cache budget. Enforce shared reservations, bounded provider progress/cache/result storage, cancellation, eviction and deadline cleanup without partial authoritative results. Counters must describe actual work, not fake scanning.
- [ ] Provider execution is owning-thread safe-point, bounded and GPU-work-free for this slice. Device-only/proxy/ambiguous data is unavailable unless an already-current host box covers it. No hidden download, driver wait, renderer setup or call to blocking world-bounds helpers; driver acquisition remains out of scope.
- [ ] Preserve current scene read authority during work and retrieval, owner minimal receipts without scene-bearing payload after revocation, admission/replay/conflicts, operation status/cancel and bounded retention. Unavailable reasons are machine-readable; no addresses, bulk arrays or private callback diagnostics leak.
- [ ] TDD public headless tests with real Scenes and explicit value providers prove independently literal local/world boxes, empty/missing/pending/invalid/stale providers, hidden-target defaults, removal/reuse, array/transform/reference changes, bounded provider progress, shared scratch/cache eviction, cancellation/deadline and current read withdrawal. Thin MCP tests exercise admission, polling, defaults, request conflicts and payload suppression.
- [ ] Build/install fresh relevant changes in existing _out/_cmake and run existing integration regressions in the enabled CUDA/UI/network/Lua/SDL3 configuration; record exact commands/case counts. No driver qualification is claimed by value-provider tests.

## Out of scope

Automatic sphere scanning (16), subtree/scene analytic aggregation (17), cone/cylinder support (18), triangles (03), driver acquisition, capture, structural editing and bulk export.

## Comments

Created by the 02 grain decision. This is a complete observation slice: an explicitly registered host can expose a current complete scene or subtree box through both public headless and MCP interfaces. Later automatic CPU providers use the same admission/authority/cache contract. The headless seam is already confirmed; use one red-green behavior at a time. The original omitted Files; listed paths are its inspected in-scope implementation/test surface, not an architecture migration.

### 2026-10-03 iteration — wontfix
split into 21, 22, 23. No source work started: current code has no bounds binding/query/cache, and scene values, lifetime-safe subtree observations and cooperative provider progress with public tests cannot all fit the 20-minute sitting.
21 delivers complete admitted scene host-value observations; 22 adds complete node-local/world subtree values and bounded ancestry; 23 adds cooperative host providers and checks all original obligations. Each slice includes both public headless and MCP behavior, authority/lifecycle and positive-test requirements, with Milestone 1 and Files restricted to this ticket. No spec/addenda/sibling source/ticket changes; the orchestrator must expand 16's dependency on 15 to completed replacements 21–23.
Verified: `jj file track --include-ignored .scratch/camera-capture/issues/{21-scene-host-value-bounds,22-node-host-value-bounds,23-cooperative-host-provider-bounds}.md` and Python assertions of split scope/status/milestone/serial edges/criteria → passed; only 15 and the three new records changed. Documentation-only split; no build/runtime/new positive bounds test or driver qualification claim.
Holes filed: none

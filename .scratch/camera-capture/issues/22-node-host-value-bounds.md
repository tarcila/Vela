# 22 — Observe complete current host subtree boxes

**What to build:** Extend admitted bounds observations to existing node targets with supplied local/world subtree boxes, lifetime-safe provenance and bounded ancestry validation through headless and MCP.

**Blocked by:** 26
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 15 before source work. After 21, complete explicit scene host values can be queried with finite storage and authority/lifecycle checks; node targets remain explicitly unsupported. Existing SceneIdentity supplies lifetime-safe node handles and incremental ancestry observations. Reuse those APIs, not renderer world-bounds helpers.

## Acceptance criteria

- [ ] Add optional node_handle to public headless `vsr_bounds` and thin MCP. Resolve an existing node at a safe point; removal/reuse cannot redirect the handle or cached box. Node omission retains 21's scene contract. Node default includes the entire existing subtree even when hidden; explicit include/exclude choices and effective defaults bind canonical request identity. Report effective enablement separately, including ancestor/layer effects.
- [ ] Host values cover the complete requested subtree and visibility choice at the revisions actually supplied by the host. Return matched target/scene and source provenance, finite ordered world min/max and meaningful finite ordered node-local subtree min/max; explicitly explain unavailable local representation rather than fabricate it. A provider can supply independently literal boxes; inspection does not scan or union geometry. Empty/incomplete/missing/pending/available states retain 21's truthful contract, and incomplete unions never appear authoritative.
- [ ] Validate node lifetime, structure/ancestors, geometry/reference, arrays, transform and visibility dependencies. Conservative invalidation is permitted only when all relevant changes are observed. Supplied old ready-view boxes remain stale after edits/failed synchronization. Cache eviction/removal/change-back never resurrects an identity or stamps a box current; successful retained observations retain their original historical provenance.
- [ ] Ancestry/identity validation yields under 256 node/geometry steps per slice and 4096 nodes per operation by default, using actual work counters and the shared validated policy from 21. No hidden unbounded subtree walk or copying of growing identity history. Bound local/world metadata, shared 8MiB temporary scratch, finite cache and result retention; cancellation, five-second admission deadline, capacity failure and eviction release reservations without publishing partial boxes. Value-only queries perform no vertex scanning.
- [ ] Preserve 21's admission/replay/conflicts, operation references/status/cancel and current scene read authority throughout validation and retrieval. Owner minimal receipts after revocation contain no node/box/provenance payload; reasons remain machine-readable and bounded. Host binding updates are owning-thread safe-point work. No device calls, downloads, waits or renderer setup, including for ambiguous transform-array/proxy content without a complete current host value.
- [ ] TDD real Scene public headless cases with independently supplied literal local/world subtree boxes, hidden subtree versus scene defaults, explicit visibility conflicts, empty/incomplete/invalid values, stale geometry/arrays/references, ancestor/local transforms/visibility, structure changes, node removal/slot reuse and change-back. Prove deep ancestry yields/exhaustion, shared scratch/cache eviction, cancellation/deadline and current read withdrawal. Thin MCP proves node admission/polling, defaults, conflicts and suppression. Do not claim automatic extent computation or driver qualification.
- [ ] Build/install changes in existing `_out/_cmake` and `_out`; run positive new cases plus existing integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands/case counts. Retain all 21 scene behaviors; no fresh configure/cold build/CUDA disable.

## Out of scope

Cooperative provider callbacks (23), analytic/triangle scanning/aggregation (16/17/18/03), capture, structural edits, GPU bounds acquisition, bulk export and blocking rendering bounds helpers.

## Comments

### Orchestrator dependency reconciliation after21

21 split into25→26. This subtree extension consumes the complete scene-value,
visibility-choice and validated bounds-policy contract; terminal26 now gates22.
No lifetime, completeness, work-limit or authority requirement was relaxed.

Created by the 15 grain decision. This is a complete user-visible node/subtree observation through both interfaces, not an ancestry helper-only ticket. Reuse 21's scene binding/policy/storage/lifecycle and extend the target semantics with genuinely bounded identity work. It intentionally consumes supplied complete subtree boxes; automatic unions remain 17's separate scope.

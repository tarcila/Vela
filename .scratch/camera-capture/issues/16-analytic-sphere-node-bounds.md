# 16 — Observe automatic host sphere bounds for existing leaf nodes

**What to build:** Independently correct automatic sphere local/world bounds for existing leaf-node placements through the admitted headless/MCP query, with incremental host-array work and truthful unsupported results.

**Blocked by:** 23
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 02. After 15, an explicit current host box can be observed, but analytic geometry without such a provider remains unavailable. Geometry exposes sphere parameters through ordinary Scene objects/arrays; no analytic extent computation exists in integration.

## Acceptance criteria

- [ ] Through vsr_bounds, automatically obtain complete local/world bounds for a supported host-resident sphere geometry/surface on an existing leaf node. Apply ancestry and node placement faithfully; use independent literal center/radius and transformed-corner oracles, including translations, rotations and nonuniform scale. Shared geometry queried at different existing nodes produces distinct correct world boxes.
- [ ] Support valid sphere center/radius representations used by project fixtures, including effective scalar versus array radii and supported indexing. Missing required data, malformed types/counts/indices, negative/nonfinite radii, nonfinite centers/transforms, unsupported representations and incomplete contributors report structured unavailable reasons, not a guessed box. Truly empty geometry remains empty.
- [ ] Honor node visibility defaults/explicit choices and effective enablement. Non-leaf targets requiring aggregation and scene queries without a complete current host provider may remain explicitly unavailable until 17; never return just the leaf contribution as a complete subtree/scene.
- [ ] Scan host data incrementally under the limits and shared scratch/cache policy delivered by 15, including bounded ancestry and primitive work. Charge actual array values/geometry steps; enforce slice/total/deadline, cancellation and eviction with no partial authoritative box or oversized retained snapshot.
- [ ] Cache dependencies cover geometry/reference, host array content/version/lifetime, node identity/structure/ancestry, transform and visibility. Changed arrays, change-back, removal/reuse and shared-object edits invalidate affected observations; no stale box is current.
- [ ] Proxy/device-only arrays and ambiguous transform-array placement remain unavailable without a revision-qualified current host box. Use public host data access only; no GPU mapping/download, driver wait or renderer setup.
- [ ] TDD through the confirmed public headless seam with real Scenes and thin MCP proves worked boxes, shared placements, hidden/empty/invalid data, array writes/reference changes, ancestry changes, stale handles and incremental work limits/current read withdrawal. Run/build/install fresh relevant changes and full integration regressions in existing enabled _out/_cmake; record commands/counts.

## Out of scope

Automatic non-leaf/scene unions (17), cone/cylinder extents (18), triangle vertex scanning (03), new geometry formats, Scene mutation, capture and driver acquisition.

## Comments

### Orchestrator dependency reconciliation after15

15 split into21→22→23. Automatic analytic scanning consumes the complete
scene/node value and cooperative-provider lifecycle; terminal23 now gates16.
No bounds completeness, provenance, authority or work-limit rule was relaxed.

Created by the 02 grain decision. This is a complete automatic observation for a selected existing sphere leaf, including transforms and bounded lifecycle through both public interfaces; it is not an internal scanner-only deliverable. Keep unsupported interim aggregation explicit rather than publishing partial boxes. Reuse the provider/cache contract of 15 and leave geometry/array source APIs unchanged.

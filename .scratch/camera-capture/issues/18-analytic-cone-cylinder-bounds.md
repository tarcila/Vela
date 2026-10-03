# 18 — Extend complete analytic bounds to cones and cylinders

**What to build:** Correct bounded host cone/cylinder extents within the existing scene/subtree bounds observation, preserving completeness, revisions and all headless/MCP lifecycle guarantees.

**Blocked by:** 17
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 02. After 17, automatic sphere scene/subtree unions and explicit current host boxes are observable; unsupported analytic contributors correctly make a union unavailable. Geometry already declares cone/cylinder subtypes and cap settings; integration does not compute their extents.

## Acceptance criteria

- [ ] Automatically compute correct cone/cylinder extents for supported host-resident endpoint/radius/index representations used by project fixtures, and expose complete local/world node/subtree and world scene boxes through vsr_bounds. Shared analytic objects and mixed sphere/cone/cylinder subtrees retain correct transformed unions.
- [ ] Independently worked axial and oblique primitive fixtures verify extents, including supported cap/radius forms and nonuniform transformed placement. Do not infer radius conventions or extent equations from implementation output; document supported ANARI parameter semantics in tests. Malformed/missing/nonfinite/negative data and unsupported or ambiguous representations are explicitly unavailable, never guessed complete boxes.
- [ ] Reuse actual per-slice/total node/array counters, shared scratch, deadline/cancellation and finite cache policy from 15–17. Large primitive arrays yield and terminate truthfully at limits; no partial box, unbounded scan/materialization, hidden download, renderer creation or blocking driver acquisition.
- [ ] Geometry/reference/index/radius/endpoint array writes and lifetimes, identity reuse, ancestry/structure/transforms and visibility invalidate relevant cached extents/unions. Unsupported specialized geometry, proxy/device-only arrays and ambiguous transform-array placement still require a current complete host provider or return unavailable.
- [ ] TDD public headless and thin MCP tests prove independent primitive and mixed/subtree boxes, shared placement, hidden/empty content, invalid inputs, stale geometry/arrays/handles, bounded work/cancellation/eviction and current scene read withdrawal; preserve sphere/provider results and request replay/default identity.
- [ ] Build/install fresh changes in existing _out/_cmake and run full integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 remaining enabled. Record exact commands/counts. Confirm that 15–18 collectively satisfy all six original 02 acceptance bullets before downstream triangle work proceeds.

## Out of scope

Triangle vertex scanning (03), additional specialized geometry formats, driver acquisition, capture, Scene mutation, bulk data export and architecture migration.

## Comments

Created by the 02 grain decision. This is a user-observable analytic coverage extension through both confirmed public interfaces, not an internal math-only ticket. Together 15–18 replace all of 02: explicit current host boxes/admission/cache lifecycle, automatic sphere placements, complete scene/subtree aggregation, and further supported analytic extents. Triangles remain 03; other specialized geometry remains truthfully unsupported rather than silently omitted.

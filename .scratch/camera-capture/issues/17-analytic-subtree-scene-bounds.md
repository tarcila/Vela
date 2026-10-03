# 17 — Observe complete bounded analytic subtrees and active scenes

**What to build:** Complete automatic sphere-containing subtree and scene local/world bounds, including shared placements, hierarchical transforms and the specified visibility defaults, through headless and MCP.

**Blocked by:** 16
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 02. After 16, supported sphere leaves have automatic complete bounds; other targets require an explicit current host box or report unavailable. SceneIdentity and Inspector already expose lifetime-safe layers/nodes and bounded hierarchy/ancestry traversal.

## Acceptance criteria

- [ ] vsr_bounds automatically returns complete unions for existing supported subtrees and registered scenes, reusing current leaf/provider extents. Nodes provide meaningful subtree-local and world boxes; scene boxes are world-space. Independently worked nested/shared fixtures distinguish local placement from ancestor placement and prove correct unions without duplicate-identity confusion.
- [ ] Scene queries default to active layers and effectively enabled contributors; node queries default to the entire existing subtree even if hidden and report effective enablement separately. Explicit include/exclude choices affect all descendants/ancestors/layers consistently and are part of request/cache identity. Empty/disabled-only targets return empty without invented min/max.
- [ ] Every relevant contributor must be accounted for. Unsupported/incomplete geometry, invalid transforms, device/proxy data or ambiguous transform-array instancing without a complete current host box makes the result unavailable with a structured reason, never a falsely complete supported-only union. Lights/non-geometric nodes do not invent geometry.
- [ ] Use iterative resumable traversal with actual counters and no whole-subtree materialization/unbounded recursion: 256 node/geometry steps or 4096 vertex values per slice, 4096 nodes and 1,048,576 values total, five-second admission deadline and 8MiB shared temporary bounds scratch by default. Validate narrowed host limits; exhaustion/cancellation returns truthful unavailable/timeout/terminal receipts and no partial authoritative box.
- [ ] Preserve bounded caches/scratch across concurrent admitted work, eviction and cancellation; no stale completed box or unsafe partial progress survives as a current result. Geometry/reference/array changes, node removal/reuse, structure/ancestor/transform changes and layer/node visibility changes invalidate the appropriate union. Changes between slices cannot publish mixed revisions.
- [ ] TDD public headless and thin MCP tests prove literal multi-object unions, shared objects under different parents, deep ancestry, subtree-versus-scene defaults, empty/hidden/mixed unsupported contributors, stale caches/handles, large/deep limits, interleaved progress, cancellation/deadline/cache eviction/shared scratch and current read revocation. No hidden device work or synchronization dependency.
- [ ] Install fresh relevant changes and run full broad-editing identity, inspection, mixed/local/world edit, restoration, session/replay, rendering and synchronization regressions in existing enabled _out/_cmake. Record exact commands/counts, including the new headless/MCP cases.

## Out of scope

Cone/cylinder extents (18), triangle scanning (03), driver acquisition, capture, structural editing, unrestricted export and unrelated cache architecture work.

## Comments

Created by the 02 grain decision. This is the complete scene/subtree observation slice rather than a traversal-only implementation ticket. Reuse 15's lifecycle and 16's sphere extents; bounds remain truthful for unsupported analytic contributors until 18. The final replacement ticket 18 completes 02 before downstream triangle work (03) proceeds; 03's existing dependency on split parent 02 must be expanded by the orchestrator to completed replacements, not treated as satisfied by wontfix.

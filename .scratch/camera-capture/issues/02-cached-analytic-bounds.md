# 02 — Observe cached analytic scene and subtree bounds

**What to build:** Read local/world bounds of supported analytic geometry, shared instances and existing node subtrees with truthful revision, visibility and unavailable states.

**Blocked by:** none
**Status:** wontfix
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.

## Current behavior

Node hierarchy/transforms are readable, but bounds are not. The device world-bounds helper waits and is not a qualified bounded inspection provider.

## Acceptance criteria

- [ ] Admitted vsr_bounds supports scene_id, optional node_handle and the specified scene/node visibility defaults; available/empty/pending/unavailable are distinct.
- [ ] Supported analytic extents and shared/transformed subtrees yield independently correct local/world boxes. Unsupported/incomplete contributors never become a falsely complete union or a fabricated zero box.
- [ ] Cache results bind actual geometry/reference, array, identity/structure, transform/visibility revisions; removal/reuse and relevant changes invalidate old results.
- [ ] Enforce slice/node/deadline/shared-scratch and bounded-cache policy, with cancellation/eviction and no partial authoritative box.
- [ ] Device-only/proxy/ambiguous instance data without a current host box is unavailable; no hidden download, blocking driver wait or renderer setup.
- [ ] Public headless and thin MCP tests cover independently worked boxes, hidden/empty/shared content, stale caches/handles, invalid values, work limits and current read authority; regressions pass.

## Out of scope

Host triangle vertex scanning (03), capture, structural editing, unrestricted data export and driver acquisition disguised as inspection.

## Comments

### 2026-10-03 iteration — wontfix
split into 15, 16, 17, 18. No source implementation started: there is no bounds API/cache/provider today, and admitted provider lifecycle, incremental analytic arrays, hierarchical aggregation and regression verification cannot all fit this sitting's 20-minute budget.
15 delivers complete revision-qualified host-box observation through public headless/MCP; 16 adds automatic sphere leaf placements; 17 adds complete bounded subtree/scene unions; 18 adds cone/cylinder coverage and checks the original acceptance obligations. Every part preserves Milestone 1, truthful unavailable interim coverage, current authority and bounded work. The original omitted Files; the parts name inspected in-scope integration/test paths. No spec, addenda or sibling ticket/source edits. Existing 03's dependency on 02 must be expanded to completion of all replacements by the orchestrator; wontfix is not completion of bounds.
Verified: `jj file track --include-ignored .scratch/camera-capture/issues/{15-admitted-provider-bounds,16-analytic-sphere-node-bounds,17-analytic-subtree-scene-bounds,18-analytic-cone-cylinder-bounds}.md` and a Python graph/scope assertion using `jj diff --name-only` → passed; exactly 02 and four new records changed, replacement statuses/milestones/files/serial edges/acceptance sections present. Documentation-only split; no build, runtime, CUDA or bounds correctness claim.
Holes filed: none

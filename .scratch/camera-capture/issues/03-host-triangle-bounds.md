# 03 — Build bounded host-resident triangle bounds

**What to build:** Obtain correct cached bounds for imported host-resident triangle geometry and transformed piece subtrees, sufficient to place an eye outside a queen's geometry.

**Blocked by:** 18
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and End-to-end acceptance.

## Current behavior

The preceding slice provides bounds queries/cache/visibility/lifecycle for analytic contributors; imported vertex geometry remains unavailable.

## Acceptance criteria

- [ ] Incrementally scan supported host-resident triangle data within explicit per-slice/total/deadline/scratch limits; combine local and transformed/shared subtree extents correctly.
- [ ] Use public data access without publishing bulk arrays or mapping/downloading device/proxy data. Unsupported attributes/contributors report truthful reasons, not truncated unions.
- [ ] Geometry/reference/array-content writes, transforms, identity reuse and visibility choices invalidate exact cache dependencies; stale boxes are never current.
- [ ] Pending construction, cache eviction, cancellation and budget exhaustion release/retain only bounded safe state and publish no partial authoritative box.
- [ ] Headless/MCP tests use independent vertex/corner oracles, shared geometry, transformed hierarchy, changed arrays, empty/unsupported data and large bounded work.

## Out of scope

New geometry formats, GPU downloads, capture, camera mutation, data export and broad cache architecture changes.

## Comments

### Orchestrator dependency reconciliation

02 split into15→16→17→18. Terminal18 verifies the complete original admitted
analytic bounds/cache/aggregation contract; this triangle extension waits for
that completion rather than treating the split parent as resolved.

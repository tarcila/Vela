# 09 — Move nodes in world space with ancestry-safe restoration

**What to build:** Position a placement in scene coordinates without reconstructing the hierarchy in the agent, while preventing stale parent conversions and preserving restorable parent/child batches.

**Blocked by:** 04, 06
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for world positioning/ancestry; Testing Decisions 6–10 and 12.

## Current behavior

World state/dependency tokens can be inspected and local position can be edited/restored. World-space requests and their ancestry checks are not yet enabled.

## Acceptance criteria

- [ ] space world requires the inspected ancestry_version and means the world position of the node's local origin, not its geometric center/bounds.
- [ ] Convert through finite invertible proposed final parent matrices. Reject singular/invalid or nonrepresentable conversions without pseudoinverse/fallback; preserve the node's linear part.
- [ ] Parent/grandparent transform writes, including change-back, conflict with stale world positioning; local edits retain narrower rules and unrelated siblings do not block either path.
- [ ] Parent/child requests use inspected pre-commit dependency validation and proposed final ancestor matrices; reversed entry orders produce the same complete result.
- [ ] Journal records the correct post-commit ancestor state, including ancestors changed in the batch. Its own commit must not make an untouched batch immediately unrestorable.
- [ ] Conditional restoration detects intervening required ancestry changes, restores affected parent/child state atomically, and preserves unrelated state with existing permanent-conflict/retention behavior.
- [ ] Dependency traversal and saved chains consume bounded work/preparation/journal budgets.
- [ ] Headless and MCP tests use independently worked translated/rotated/nonuniform parents, singular rejection, change-back, order permutations, and exact restoration.

## Out of scope

World rotation/scale/matrix editing, reparenting, bounds centering, camera movement, and lossy transform repair.

## Comments

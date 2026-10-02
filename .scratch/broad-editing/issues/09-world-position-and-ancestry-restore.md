# 09 — Move nodes in world space with ancestry-safe restoration

**What to build:** Position a placement in scene coordinates without reconstructing the hierarchy in the agent, while preventing stale parent conversions and preserving restorable parent/child batches.

**Blocked by:** 04, 06
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for world positioning/ancestry; Testing Decisions 6–10 and 12.

## Current behavior

World state/dependency tokens can be inspected and local position can be edited/restored. World-space requests and their ancestry checks are not yet enabled.

## Acceptance criteria

- [x] space world requires the inspected ancestry_version and means the world position of the node's local origin, not its geometric center/bounds.
- [x] Convert through finite invertible proposed final parent matrices. Reject singular/invalid or nonrepresentable conversions without pseudoinverse/fallback; preserve the node's linear part.
- [x] Parent/grandparent transform writes, including change-back, conflict with stale world positioning; local edits retain narrower rules and unrelated siblings do not block either path.
- [x] Parent/child requests use inspected pre-commit dependency validation and proposed final ancestor matrices; reversed entry orders produce the same complete result.
- [x] Journal records the correct post-commit ancestor state, including ancestors changed in the batch. Its own commit must not make an untouched batch immediately unrestorable.
- [x] Conditional restoration detects intervening required ancestry changes, restores affected parent/child state atomically, and preserves unrelated state with existing permanent-conflict/retention behavior.
- [x] Dependency traversal and saved chains consume bounded work/preparation/journal budgets.
- [x] Headless and MCP tests use independently worked translated/rotated/nonuniform parents, singular rejection, change-back, order permutations, and exact restoration.

## Out of scope

World rotation/scale/matrix editing, reparenting, bounds centering, camera movement, and lossy transform repair.

## Comments

### Orchestrator recovery — interrupted world-position pass

The worker hit the stdout-event limit after world-position implementation and
initial/full verification logs. No committed resolution is inferred. Preserve
code-only WIP, then finish acceptance audit, parent/child post-commit dependency
restoration checks, and committed status in a bounded continuation.

### 2026-10-02 iteration — resolved
Salvaged all six code paths from WIP `wyqowoomvwrvxqwyuwskpozkuxklnmus`; deliverable `mqmrsoonvxlyyzslmnyuuwwlxpsnmskt`.
Audited exact-zero pivot rejection, float32 converted-local range, canonical space/ancestry fields, bounded dependency walks, and post-commit ancestry capture after every parent write. Added shear/reflection/tiny-invertible proof and immediate versus later-change-back parent/child restoration tests.
Added MCP independently worked rotated/nonuniform grandparent conversion, entry permutations, singular rejection, grandparent change-back, exact restoration, and placement documentation. Rebased onto current integration `355a2e8c` (tickets 10/74), preserving both MCP test additions; prepared final matrix/state slots remain for the later ticket-08 SRT merger.
Verified: `cmake --build _out/_cmake --parallel 4 && cmake --install _out/_cmake` → pass; `_out/_cmake/vsrIntegrationTests '[integration]'` → 248 cases, 21019 assertions pass; `ctest --test-dir _out/_cmake --output-on-failure --parallel 4` → 36/36 pass; `clang-format --dry-run --Werror` on the six changed C++ paths → pass. Logs: `/tmp/broad-editing-09-{build,install,integration,ctest}-current.log`.
Holes filed: none

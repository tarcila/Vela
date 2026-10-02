# 12 — Verify the complete feature in the viewer and document it

**What to build:** Make the full broader-inspection/placement-editing feature usable and documented in the live viewer, and verify that all branches of the implementation preserve existing integration behavior.

**Blocked by:** 02, 08, 10, 11, 74
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 9; Testing Decisions 11, 12, and Acceptance workflow.

## Current behavior

The preceding tickets individually deliver read/edit/restore slices and the portable chess workflow. Final viewer acceptance, complete user documentation, and cross-configuration verification remain.

## Acceptance criteria

- [ ] Exercise integration-enabled viewer placement inspection/live rearrangement/Restore and truthful Agent Activity. Imported chess assets may be optional, but the portable procedural acceptance must actually run.
- [ ] Tool descriptions and user guidance explain object versus node identity, filtering/page limits, typed/unavailable values, explicit node edit exposure, local/world origin positioning, VSR degree rotations, matrix convention, conflicts, and restoration.
- [ ] Guidance never promises that scene camera inspection/editing changes the interactive or offscreen camera; deferred tools and the current write allowlist remain explicit.
- [ ] Verify all spec behavioral requirements together: bounded expanded inspection, lifetime/change-back tracking, grouped and mixed batches, parent/child dependencies, synchronization/authority/replay, exact restore, and budget/expiry paths.
- [ ] Full integration/MCP/rendering/viewer regression succeeds after build/install; unrelated configurations with integration or MCP disabled still build. Record exact commands and any environmental limitation honestly.
- [ ] No implementation ticket remains unresolved or silently partial, and no UI/Lua/Pi dependency is introduced into the headless library.

## Out of scope

New features beyond the specification, publishing PRs/issues, silently weakening acceptance, shared result-store/FlatMap changes, and modifying user-local MCP config or screenshot artifacts.

## Comments

### Orchestrator audit dependency

74 completes the existing bounded name-filter requirement after direct stored-
name find calls were found during implementation audit. Final verification must
include oversized-name/beyond-preview continuation tests; this is not a new
feature or a reason to weaken the specification.

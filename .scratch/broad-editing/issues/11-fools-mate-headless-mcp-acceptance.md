# 11 — Demonstrate Fool's Mate entirely through live MCP

**What to build:** Identify four existing chess placements, move them into Fool's Mate in one batch, verify them, and restore their exact original state on the same running scene without Lua/offline workarounds.

**Blocked by:** 09
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Testing Decisions Acceptance workflow and prior art; User Stories 39, 51–55, and 61.

## Current behavior

The original experiment needed separate scene reconstruction and a manually executed Lua script. The preceding slices now expose hierarchy and local/world restorable placement edits.

## Acceptance criteria

- [ ] Provide a portable procedural fixture with 32 named piece placements, shared object references, a non-identity board parent, and an independently frozen square-coordinate oracle.
- [ ] Discover/inspect targets and submit one batch for white f-pawn to f3, white g-pawn to g4, black e-pawn to e5, and black queen to h4; verify live result and Batch Reference.
- [ ] Restore exact prior matrices/enablement/representations; other 28 pieces, shared objects, parent, names, and defaults remain unchanged.
- [ ] Repeat via deterministic loopback MCP calls and real host safe points, not direct client scene mutation, source reading, shell, Lua, or asset reload. Automated correctness is geometric, not aesthetic/model-dependent.
- [ ] Include parent dependency, stale-target rejection, truthful operation status/activity, and narrow restoration behavior in the demonstrable flow.
- [ ] Existing rendering can provide a thin visual/synchronization check without changing cameras or capture settings.

## Out of scope

Queen's-eye screenshot/camera settings/image export, broad material changes, missing-asset exemptions for the procedural oracle, and architectural refactoring.

## Comments

# 11 — Demonstrate Fool's Mate entirely through live MCP

**What to build:** Identify four existing chess placements, move them into Fool's Mate in one batch, verify them, and restore their exact original state on the same running scene without Lua/offline workarounds.

**Blocked by:** 09
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Testing Decisions Acceptance workflow and prior art; User Stories 39, 51–55, and 61.

## Current behavior

The original experiment needed separate scene reconstruction and a manually executed Lua script. The preceding slices now expose hierarchy and local/world restorable placement edits.

## Acceptance criteria

- [x] Provide a portable procedural fixture with 32 named piece placements, shared object references, a non-identity board parent, and an independently frozen square-coordinate oracle.
- [x] Discover/inspect targets and submit one batch for white f-pawn to f3, white g-pawn to g4, black e-pawn to e5, and black queen to h4; verify live result and Batch Reference.
- [x] Restore exact prior matrices/enablement/representations; other 28 pieces, shared objects, parent, names, and defaults remain unchanged.
- [x] Repeat via deterministic loopback MCP calls and real host safe points, not direct client scene mutation, source reading, shell, Lua, or asset reload. Automated correctness is geometric, not aesthetic/model-dependent.
- [x] Include parent dependency, stale-target rejection, truthful operation status/activity, and narrow restoration behavior in the demonstrable flow.
- [x] Existing rendering can provide a thin visual/synchronization check without changing cameras or capture settings.

## Out of scope

Queen's-eye screenshot/camera settings/image export, broad material changes, missing-asset exemptions for the procedural oracle, and architectural refactoring.

## Comments

### Orchestrator recovery — interrupted acceptance implementation

The worker hit the stdout-event limit after portable fixture and headless/MCP
workflow tests were added. The current headless chess scenario reports SIGSEGV;
no acceptance success is claimed. Preserve this private WIP and debug the actual fixture/query
assumptions in a bounded continuation, retaining strict oracles. The host's
agent definition changed during the run to one code-plus-ticket commit and
removed the home scratch-commit hook; use the current workflow without
recreating or changing host configuration. This ready-for-human status records
the interrupted attempt, not a product decision blocker.

### 2026-10-03 iteration — resolved
Salvaged unpublished WIP `ttmtqxwy` in place, keeping its portable 32-placement fixture and independent binary-exact square oracle. Fixed the test's JSON assumptions: names use `name.text`; SRT-backed `local_matrix` is raw 3×3 storage, so reading column 3 was out of bounds. Verify local origins through `local_position`, guard matrix shapes, and retain exact raw-value/SRT/default restoration assertions. Compare optional operation references by members without expanding the production API.
Headless safe points verify one four-move batch, exact restoration, stale targets, parent change-back dependencies, truthful activity/synchronization, and preservation of a later unrelated sibling edit. Real authenticated loopback MCP discovers the scene ID and target handles/tokens through tools, verifies queued versus committed status, moves/verifies/restores with bounded polling, and rejects repeated restoration. Direct fixture access is only host setup and an independent unchanged-state oracle; client discovery/mutation uses tools exclusively. Existing RAII server/private-runtime cleanup is reused.
The other 28 placements, shared references/objects, board parent, names, enablement, representations, and defaults are checked unchanged; the existing rendering/synchronization regression supplies the thin check without camera/capture changes. Viewer/imported-asset and cross-configuration acceptance remains ticket 12's scope.
Verified: `cmake --build _out/_cmake --parallel 4 && cmake --install _out/_cmake --prefix "$PWD/_out"` → pass (explicit local prefix avoids the cache's unwritable `/usr/local` default).
Verified: `_out/_cmake/vsrIntegrationTests '[chess]'` → 1 case / 6233 assertions pass; `_out/_cmake/vsrIntegrationTests '[MCPchess]'` → 1 case / 2666 assertions pass; `_out/_cmake/vsrIntegrationTests '[integration]'` → 258 cases / 30789 assertions pass.
Verified: `ctest --test-dir _out/_cmake --output-on-failure --parallel 4` → 36/36 pass; `clang-format --dry-run --Werror src/tests/integration/ChessFixture.hpp src/tests/integration/test_IntegrationEdit.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → pass. Logs: `.scratch/broad-editing-logs/11-{build,install,headless,mcp,integration,ctest,format}-current.log`.
Holes filed: none

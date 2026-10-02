# 12 — Verify the complete feature in the viewer and document it

**What to build:** Make the full broader-inspection/placement-editing feature usable and documented in the live viewer, and verify that all branches of the implementation preserve existing integration behavior.

**Blocked by:** 02, 08, 10, 11, 74
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 9; Testing Decisions 11, 12, and Acceptance workflow.

## Current behavior

The preceding tickets individually deliver read/edit/restore slices and the portable chess workflow. Final viewer acceptance, complete user documentation, and cross-configuration verification remain.

## Acceptance criteria

- [x] Exercise integration-enabled viewer placement inspection/live rearrangement/Restore and truthful Agent Activity. Imported chess assets may be optional, but the portable procedural acceptance must actually run.
- [x] Tool descriptions and user guidance explain object versus node identity, filtering/page limits, typed/unavailable values, explicit node edit exposure, local/world origin positioning, VSR degree rotations, matrix convention, conflicts, and restoration.
- [x] Guidance never promises that scene camera inspection/editing changes the interactive or offscreen camera; deferred tools and the current write allowlist remain explicit.
- [x] Verify all spec behavioral requirements together: bounded expanded inspection, lifetime/change-back tracking, grouped and mixed batches, parent/child dependencies, synchronization/authority/replay, exact restore, and budget/expiry paths.
- [x] Full integration/MCP/rendering/viewer regression succeeds after build/install; unrelated configurations with integration or MCP disabled still build. Record exact commands and any environmental limitation honestly.
- [x] No implementation ticket remains unresolved or silently partial, and no UI/Lua/Pi dependency is introduced into the headless library.

## Out of scope

New features beyond the specification, publishing PRs/issues, silently weakening acceptance, shared result-store/FlatMap changes, and modifying user-local MCP config or screenshot artifacts.

## Comments

### Orchestrator recovery — interrupted final acceptance

The worker reached the stdout-event limit. Full CPU interactive/network build,
68 CTest entries, and headless/MCP chess logs passed; integration-off built and
MCP-off was configured but still needs completion. Imported-viewer discovery
succeeded, then the smoke driver used scene_id instead of discovery's actual
id field and stopped before edit/restore. No successful viewer workflow is
claimed yet. The orchestrator stopped only this test's launcher/viewer/Xvfb;
restart with private Xauthority and UNIX-only Xvfb, not unauthenticated TCP.

Generated build-* directories were accidentally auto-tracked in the dirty
working copy. Added the standard build-*/ ignore pattern and untracked their
artifacts without deleting caches; they must never enter feature commits.
Preserve this private WIP and finish real acceptance/documentation/configuration
checks under the current one-commit workflow. This status records a harness
interruption, not a new product-design decision.

### Orchestrator audit dependency

74 completes the existing bounded name-filter requirement after direct stored-
name find calls were found during implementation audit. Final verification must
include oversized-name/beyond-preview continuation tests; this is not a new
feature or a reason to weaken the specification.

### Final verified closure

The recovery notes above are historical and superseded by the successful final
runs. This closes ticket 12 only; no new implementation or graphical inspection
was performed during bookkeeping. Proof logs are local ignored runtime artifacts
under `.scratch/broad-editing-logs/`, not committed attachments or credentials.

**Build/install and regression commands** (from the workspace root, using the
existing configured caches):

```bash
cmake --build _out/_cmake --parallel 4
cmake --install _out/_cmake
ctest --test-dir _out/_cmake --output-on-failure
cmake --build build-integration-off --parallel 4
ctest --test-dir build-integration-off --output-on-failure
cmake --build build-mcp-off --parallel 4
ctest --test-dir build-mcp-off --output-on-failure
_out/_cmake/vsrIntegrationTests '[chess]'
_out/_cmake/vsrIntegrationTests '[MCPchess]'
clang-format --dry-run --Werror src/apps/tutorial/vsrTutorialIntegrationHeadless.cpp src/vsr/integration/mcp/tools/EditTools.hpp
```

- Main cache: Release/Ninja, CUDA OFF, integration/MCP ON, interactive apps/UI,
  Lua and networking ON; installation prefix `_out`. `12-rebuild.log` and
  `12-reinstall.log` record the final tutorial rebuild and install;
  `12-retest.log` passes all **68/68** CTest entries (also `12-ctest.log`).
  The combined integration suite covers bounded inspection/name continuation,
  lifetimes/change-back, grouped/mixed writes, parent/child dependencies,
  synchronization/authority/replay, restore and budget/expiry behavior; existing
  rendering, viewer/app and network regressions remain included.
- Integration-OFF cache: Release, CUDA/UI/interactive/networking OFF,
  integration/MCP OFF. `12-integration-off-build.log` completes the build;
  `12-integration-off-ctest.log` passes **35/35**.
- MCP-OFF cache: Release, CUDA/UI/interactive/networking OFF, integration ON,
  MCP OFF. `12-mcp-off-build.log` completes the build;
  `12-mcp-off-ctest.log` passes **36/36**. These configurations verify the
  headless library without adding UI/Lua/Pi dependencies.
- The two exact chess commands above were repeated at closure and passed:
  **6233 assertions** headless and **2666 assertions** authenticated MCP.
  The prior final logs are `12-headless-chess-final.log` and
  `12-mcp-chess-final.log`; both are positive portable acceptance, not merely
  discovery or startup tests.

**Actual viewer proof:** `12-viewer-smoke.log` reports PASS for an authenticated,
UNIX-only imported viewer: four named placements, atomic live edit, exact
matrix/placement Restore, shared objects unchanged, and existing CPU view
renders before/moved/restored with a change after edit. The recovery driver is
`python3 .scratch/broad-editing-logs/12-viewer-smoke.py`, with its owned viewer,
proxy and private-Xauthority Xvfb launcher already running for that recorded
smoke. The log confirms owned viewer/proxy/Xvfb shutdown and private Xauthority
removal; closure found zero owned smoke/viewer processes. No new image files,
image attachments, base64 RPC payloads or graphical verification were read or
produced at closure. CPU rendering under Xvfb is the environment limitation:
this is not a GPU/display-quality certification or a claim of manually clicking
Agent Activity controls. Truthful activity and restore semantics are covered
by regression tests; the live imported workflow proves actual edit/restore.

**Documentation proof:** Python `json.loads` over fenced JSON examples and
relative Markdown link existence checks in `docs/live-placement-editing.md`,
`docs/pi-attachment.md`, the viewer README and the MCP README passed:
**4 files, 6 JSON examples, 7 relative links, 0 errors**. Outputs were counts only,
not example/payload dumps. The exact clang-format command above passed for both
changed C++ files. Guidance covers identities, filter/page bounds, typed states,
node opt-in, local/world origins, degrees/matrices, conflicts/restoration,
unchanged object write allowlist and deferred camera/view features.

**Dependency audit:** Required implementation tickets (including 74) are resolved;
03/25 were superseded (`wontfix`) by their resolved split tickets. Ticket 73 remains
an explicitly **nonblocking** structural-envelope follow-up (`needs-triage`), not
an unresolved required implementation slice. No sibling ticket or specification
was modified. Tracked-file audit found zero `build-*` or runtime proof/log files;
`build-*/` is ignored. This ticket and all already-completed source/documentation
changes are finalized together in the same unpublished change, with no push.


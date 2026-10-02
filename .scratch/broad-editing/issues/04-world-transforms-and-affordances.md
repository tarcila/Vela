# 04 — Inspect world transforms and faithful SRT affordances

**What to build:** Understand a placement's scene-space transform and determine whether position, rotation, scale, or matrix editing is valid before issuing an edit.

**Blocked by:** 28
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 4 and 6; Testing Decisions 2, 5, and 6 observation behavior.

## Current behavior

The preceding hierarchy slice exposes local state and safe identities. Derived world transforms and explicit faithful-SRT editing affordances are not yet exposed.

## Acceptance criteria

- [x] Return valid derived world matrices and local positions, with an explicit unavailable reason for invalid/non-finite or otherwise unsupported ancestry instead of fabricated identity transforms.
- [x] Expose VSR degree-based azimuth/elevation/roll and scale only when reconstructing the current matrix is faithful under the specified tolerance; indicate unsupported convenience edits explicitly.
- [x] Preserve complete matrix data and reflection/shear/degeneracy distinctions; matrix-derived observations need not promise a unique Euler decomposition.
- [x] Node kinds and edit exposure accurately distinguish supported scalar transforms from references, empty nodes, and arrays; observation itself grants no write permission.
- [x] Deep ancestry derivation obeys bounded work/scratch limits and correct cursor/version coverage.
- [x] Public headless and thin MCP tests use asymmetric independently worked matrices, transformed parents, shear, degeneracy, and invalid state.

## Out of scope

Writing transforms, bounds queries, editing array instances, camera/capture controls, and lossy transform repair.

## Comments

### 2026-10-02 iteration — resolved
Salvaged all three code paths from WIP `lstqoryssmkxnvxwlqlskqklqqzzzmuk` onto clean failure `qxxwkwrtrrqlwxqqyplxuxmsoxwwlqxp`; deliverable `xmykklllywqyswwsmwulwookkvooppmk`. Integration bookmark remains `f5c25016826e1b1d0e356f473d1d6620fbbdb67c`; no 05 edits. The preserved edge-red log predates the salvaged fixes: a matrix-derived singular state whose cached SRT faithfully reconstructs it is available, and invalid scalar matrices still permit full replacement (representability, not permission).
Complete row matrices preserve non-finite components as typed JSON tags; world matrices carry unavailable reasons for invalid/projective/array ancestry and finite-input overflow. Cached degree-based Y/X/Z rotation and signed/zero scales are exposed only after per-linear-component reconstruction at the specified tolerance; shear and unfaithful reflection remain unavailable. Constant-size world cursor state participates in existing authenticated revision-bound work/scratch accounting.
Expanded public and loopback MCP tests independently check asymmetric transformed parents, float32 precision, signed reflected/degenerate SRT, matrix-derived degeneracy/reflection, shear, NaN and both infinities, projective/array ancestry, and world overflow without fabricated data. Existing deep-chain tests now verify resumed world translation; cancellation/deadline/scratch/cursor and identity/version/dependency regressions passed in the full suite. Thin MCP descriptions and carrier docs agree with the node editor convention and explicitly deny permission grants.
Verified: `cmake --build _out/_cmake -j 4 && cmake --install _out/_cmake` → passed using existing Release cache; `_out/_cmake/vsrIntegrationTests '[node-transforms],[node-transform-mcp],[node-inspection],[node-mcp]'` → 521 assertions/11 cases passed; `_out/_cmake/vsrIntegrationTests` → 9,592 assertions/212 cases passed; `ctest --test-dir _out/_cmake --output-on-failure -j 4` → 36/36 passed including core Forest/Scene; `clang-format --dry-run --Werror src/vsr/integration/inspection/Inspection.hpp src/vsr/integration/mcp/tools/QueryTools.hpp src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → passed. Rerun logs: `.scratch/broad-editing-logs/04-continuation-{build,install,focused,integration,ctest}.log` (existing ignored log area, not commit evidence; exact commands/results above are durable).
Holes filed: none

### Orchestrator recovery — interrupted transform-observation pass

The worker hit the stdout-event limit after initial world/SRT implementation
and red/green coverage. Edge-case tests still have failures; no resolved state
is claimed. Park the code-only attempt, then resume fidelity/unavailable/deep
budget edge cases and full MCP/core regression before committing completion.

### Orchestrator dependency update

03 was split before implementation into 25–28. This ticket waits for terminal
replacement 28, which establishes the complete original observation/identity/
version/dependency contract; the specification and this ticket's scope are
unchanged.

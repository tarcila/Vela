# 04 — Inspect world transforms and faithful SRT affordances

**What to build:** Understand a placement's scene-space transform and determine whether position, rotation, scale, or matrix editing is valid before issuing an edit.

**Blocked by:** 28
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 4 and 6; Testing Decisions 2, 5, and 6 observation behavior.

## Current behavior

The preceding hierarchy slice exposes local state and safe identities. Derived world transforms and explicit faithful-SRT editing affordances are not yet exposed.

## Acceptance criteria

- [ ] Return valid derived world matrices and local positions, with an explicit unavailable reason for invalid/non-finite or otherwise unsupported ancestry instead of fabricated identity transforms.
- [ ] Expose VSR degree-based azimuth/elevation/roll and scale only when reconstructing the current matrix is faithful under the specified tolerance; indicate unsupported convenience edits explicitly.
- [ ] Preserve complete matrix data and reflection/shear/degeneracy distinctions; matrix-derived observations need not promise a unique Euler decomposition.
- [ ] Node kinds and edit exposure accurately distinguish supported scalar transforms from references, empty nodes, and arrays; observation itself grants no write permission.
- [ ] Deep ancestry derivation obeys bounded work/scratch limits and correct cursor/version coverage.
- [ ] Public headless and thin MCP tests use asymmetric independently worked matrices, transformed parents, shear, degeneracy, and invalid state.

## Out of scope

Writing transforms, bounds queries, editing array instances, camera/capture controls, and lossy transform repair.

## Comments

### Orchestrator dependency update

03 was split before implementation into 25–28. This ticket waits for terminal
replacement 28, which establishes the complete original observation/identity/
version/dependency contract; the specification and this ticket's scope are
unchanged.

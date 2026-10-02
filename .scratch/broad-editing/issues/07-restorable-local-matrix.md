# 07 — Replace complete local affine matrices safely

**What to build:** Explicitly edit a scalar transform node's full local matrix, including valid non-SRT affine state, and restore the original state exactly.

**Blocked by:** 05
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for transform; Testing Decisions 5, 8–10, and 12.

## Current behavior

The complete node-edit path supports enabled state. Full-matrix replacement is not an exposed operation.

## Acceptance criteria

- [ ] transform accepts a complete four-row/four-column local matrix using the specified column-vector math/last-column translation contract, independently of internal storage order.
- [ ] Finite binary64 inputs are validated before binary32 conversion; reject overflow, wrong shapes, and projective last rows instead of rounding them into valid affine input.
- [ ] Valid singular/sheared/reflected affine matrices use explicit matrix editing without convenience normalization; non-transform nodes reject and no wrapper/reference replacement is created.
- [ ] Full transform is mutually exclusive with position/rotation/scale entries on the same node, but can coexist with enabled state. Duplicate/conflicting entries reject atomically.
- [ ] Batches preserve fine conflicts, publication/synchronization, exact before-state/representation/default behavior, resource accounting, and conditional restoration.
- [ ] Headless and MCP tests use asymmetric matrix oracles, invalid input, exact restoration, and object-only regression checks.

## Out of scope

World-matrix replacement, rotation/scale conveniences, structural editing, lossy repair, and container/result-store refactoring.

## Comments

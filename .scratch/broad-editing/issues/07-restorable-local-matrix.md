# 07 — Replace complete local affine matrices safely

**What to build:** Explicitly edit a scalar transform node's full local matrix, including valid non-SRT affine state, and restore the original state exactly.

**Blocked by:** 05
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for transform; Testing Decisions 5, 8–10, and 12.

## Current behavior

The complete node-edit path supports enabled state. Full-matrix replacement is not an exposed operation.

## Acceptance criteria

- [x] transform accepts a complete four-row/four-column local matrix using the specified column-vector math/last-column translation contract, independently of internal storage order.
- [x] Finite binary64 inputs are validated before binary32 conversion; reject overflow, wrong shapes, and projective last rows instead of rounding them into valid affine input.
- [x] Valid singular/sheared/reflected affine matrices use explicit matrix editing without convenience normalization; non-transform nodes reject and no wrapper/reference replacement is created.
- [x] Full transform is mutually exclusive with position/rotation/scale entries on the same node, but can coexist with enabled state. Duplicate/conflicting entries reject atomically.
- [x] Batches preserve fine conflicts, publication/synchronization, exact before-state/representation/default behavior, resource accounting, and conditional restoration.
- [x] Headless and MCP tests use asymmetric matrix oracles, invalid input, exact restoration, and object-only regression checks.

## Out of scope

World-matrix replacement, rotation/scale conveniences, structural editing, lossy repair, and container/result-store refactoring.

## Comments

### Orchestrator recovery — interrupted matrix pass after tests

The worker hit the stdout-event limit after matrix/history/validation tests,
full integration (226 cases), and CTest (36/36) passed. No committed resolution
is inferred from the interrupted process. Preserve code-only WIP, then finish
acceptance audit and code/scratch commits in a bounded continuation.

### 2026-10-02 iteration — resolved
Salvaged all nine code paths exactly from WIP `pnwtvpqonllsxluuzuqzqxkzoltyuvow` onto clean `kvzmuqwkqwzmkqsrqpvtrzyzlrqxwllm`; deliverable `zmomtwnmspokqoqqvzvrynmmqsqrzxol`. Integration `77eb1d8a` remains unchanged. Worker 06's separate API is untouched; merger owns deduplication with this slice's `setTransformState(mat4, mat3)`.
Audited binary64 affine validation before rounding, explicit row-to-column mapping, scalar-kind restrictions, grouped transform/enabled state, atomic conflicts/preparation, shared journal accounting and conditional restore. Tests compare exact prior matrix/cache bytes (including noncanonical SRT and singular caches) and retain the original default baseline; headless/MCP asymmetric oracles and existing object-only coverage remain intact.
Verified: `jj diff --from pnwtvpqonllsxluuzuqzqxkzoltyuvow --to @ -- <nine restored paths>` → empty before commit; `clang-format --dry-run --Werror <eight changed C++ paths>` → passed; `cmake --build _out/_cmake --target vsrIntegrationTests anari_library_vsr -j 4 && cmake --install _out/_cmake --prefix "$PWD/_out" && _out/_cmake/vsrIntegrationTests '[node-matrix]'` → passed, 418 assertions/4 cases. Final continuation logs: `.scratch/broad-editing-logs/07-continuation07-{build,install,focused}.log`. Initial attempts used an unavailable `vsrMcpProxy` target and default `/usr/local` install prefix; corrected to existing targets and workspace-local prefix, without reconfiguration or source changes.
Reused exact-code prior full verification: `_out/_cmake/vsrIntegrationTests` → 14,232 assertions/226 cases (`07-all.log`); `ctest --test-dir _out/_cmake --output-on-failure` → 36/36 (`07-ctest.log`); matrix history/validation logs also pass. No new broad test run or viewer smoke is claimed.
Holes filed: none.

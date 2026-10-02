# 02 — Inspect typed parameter values and object references

**What to build:** Read the live camera, renderer, materials, and reference relationships with typed values rather than names/types alone, without acquiring broader write permissions or downloading arrays.

**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 3 and 9; Testing Decisions 1 and 12.

## Current behavior

Inspection exposes general parameter names/types and values only for the five already editable matte/directional-light fields.

## Acceptance criteria

- [x] Ordinary Boolean/integer/float/string/vector/matrix parameters have explicit ANARI-typed value representations; legacy editable values remain compatible.
- [x] Large integers use lossless typed decimal encoding and observed non-finite floats use explicit tags; neither is silently rounded or replaced by zero.
- [x] Valid/null/dangling object references are distinguished and valid references use opaque handles. Arrays expose only type/shape/count summaries without mapping bulk data or triggering GPU work.
- [x] Unsupported, truncated, and omitted representations carry explicit reasons. Identity/type/version correctness fields are never truncated; item/page/optional/scratch limits apply.
- [x] Inspection revisions and cursors cover all newly returned values/reference and summary metadata, including non-editable parameter changes.
- [x] Reading a new field does not widen the object edit allowlist or permissions.
- [x] Public headless tests and thin MCP tests cover typed values, references, representation limits, and existing object-only compatibility.

## Out of scope

Node inspection/editing, bulk array export, camera/view editing, asset provenance, and broader parameter writes.

## Comments

### Orchestrator recovery — interrupted first iteration

The subagent stopped at the harness stdout-event limit before reporting or
committing. Partial code and test changes exist, but no resolved outcome is
claimed. The orchestrator is parking that attempt off the integration chain
before a bounded continuation. Status remains ready-for-agent.

### 2026-10-02 iteration — resolved
Salvaged parked WIP `rtrmvotxxlylzymlrwpvvotvtlxzpwzl`; retained it and completed bounded UTF-8 string previews, explicit omission reasons, typed encodings, references and array summaries.
Added a non-copying Any string view and public headless revision/change-back/relationship tests plus a thin read-only MCP transport check. Edit allowlists and camera-specific scene revision semantics remain unchanged.
Deliverable: `rlpkrysllqrlysxqynrouruxqpsvplru`. Focused red: `[typed-inspection]` initially failed on missing bounded `longText`; green after bounded representation implementation.
Verified: `cmake --build _out/_cmake --target vsrIntegrationTests vsrMcpProxy -j 4` → passed; `_out/_cmake/vsrIntegrationTests '[typed-inspection]'` → 265 assertions, 6 cases passed (including MCP).
Verified: `_out/_cmake/vsrIntegrationTests '[integration]'` → 7429 assertions, 181 cases passed; `ctest --test-dir _out/_cmake -R 'vsr::integration' --output-on-failure` → 1/1 passed.
Verified: `clang-format --dry-run --Werror src/vsr/core/Any.hpp src/vsr/integration/inspection/Inspection.hpp src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → passed. Logs in `.scratch/broad-editing-logs/t02-*.log` are diagnostic only, outside the task subtree.
Holes filed: none

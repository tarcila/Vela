# 74 — Yield bounded name-filter matching on oversized stored names

**What to build:** Keep exact/substring filtering correct on real stored names without allowing one oversized name to monopolize a safe-point slice or bypass inspection work limits.

**Blocked by:** 01
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 (literal matching against stored names, bounded matching slices and actual traversal work); Testing Decisions 3 and 11.

## Current behavior

Implementation audit found object, layer, and node substring predicates calling the stored string's unbounded find operation directly. Input filter strings are capped, but stored names need not be; bounding node visits alone does not bound this work. Existing tests exercise sparse registries, not oversized names whose match is beyond the display preview.

## Acceptance criteria

- [x] Match exact/literal case-sensitive substring predicates against the real stored name, including a match beyond the display preview, without treating truncated preview text as authoritative.
- [x] Use bounded resumable matching work across safe-point slices and page continuation. Avoid pathological repeated-prefix/quadratic comparisons; charge query scratch and actual scanning to explicit bounded work policy.
- [x] Objects, layers, and nodes share consistent behavior. Empty substring, no-match, combined filters, direct handles, and deterministic unchanged-scene ordering remain compatible.
- [x] Continuations capture any needed partial-name progress and bind it to the complete query/revision; do not restart an unbounded scan on each continuation or skip a partly scanned target.
- [x] Cancellation, deadline, visit/work exhaustion, cursor staleness, scratch release, historical replay, and all existing page/item/string limits stay truthful. Do not solve the problem by silently rejecting every long stored name or imposing a new write restriction.
- [x] Public headless tests with oversized names, beyond-preview matches, repeated-prefix adversarial strings, small slice/work budgets, cancellation, continuation, and all three query kinds demonstrate bounded progress and correct results; thin MCP tests retain supported schema and results.
- [x] Full integration/CTest and formatting pass. Record exact commands and actual policy units in documentation if additional scan accounting is introduced.

## Out of scope

Regex/glob matching, Unicode normalization, new edit capabilities, changing existing node identity semantics, spec weakening, and architectural cleanup.

## Comments

### Orchestrator audit

This is completion of an existing approved bounded-inspection requirement, not a new feature or an undecided spec hole. Final verification 12 waits for this fix. The new number is the next actual ticket after 73; prior implementer reservations are not dummy issues.

### 2026-10-02 iteration — resolved
Implemented exact comparisons and linear KMP substring matching against real stored names, with authenticated phase/offset/prefix continuation state for all three kinds. Shared scratch charges the admission-time bounded input prefix table; name_work counts every stored-byte/fallback comparison, default 65536/page and 4096/slice. Existing client query fields remain unchanged; MCP descriptions and README disclose policy.
Deliverable: zuumxqxkupysytkwnussksuqxpzulxpl (a2dda750). Public regression failed before implementation (one oversized layer completed too early), then passed. No prior WIP supplied. Integration bookmark checked immediately before commit: unchanged at base 1169c988.
Verified: `cmake -S . -B _out/_cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSR_USE_INTEGRATION=ON -DVSR_USE_INTEGRATION_MCP=ON -DVSR_BUILD_INTERACTIVE_APPS=OFF -DVSR_BUILD_UI_LIBRARY=OFF` → fresh-workspace configure passed.
Verified: `cmake --build _out/_cmake -j 4` → passed; `_out/_cmake/vsrIntegrationTests '[bounded-names]'` → 3776 assertions / 4 cases passed; `_out/_cmake/vsrIntegrationTests` → 18607 assertions / 236 cases passed; `ctest --test-dir _out/_cmake --output-on-failure -j 4` → 36/36 passed.
Verified: `clang-format --dry-run --Werror src/vsr/integration/inspection/Inspection.hpp src/vsr/integration/mcp/tools/QueryTools.hpp src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → passed. Logs outside task subtree: `.scratch/broad-editing-logs/74-*.log`.
Holes filed: none

# 01 — Filter object inspection and advertise paging limits

**What to build:** Find the relevant live scene objects by literal name, type, subtype, or opaque handle without paging through every unrelated object, and expose the active page limits before a request is submitted.

**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 9; Testing Decisions 3 and 12.

## Current behavior

Object queries page through the registry without filters. The schema declares a page-size minimum but does not advertise the current maximum of 256. Cursor binding and bounded traversal already exist.

## Acceptance criteria

- [x] Discovery and query schema/descriptions disclose active default/maximum page sizes and supported query modes; default 64, maximum 256. Oversized page sizes reject before admission.
- [x] Object inspection accepts exact name, literal case-sensitive name substring, exact handle, ANARI object type, and subtype filters; combined filters intersect.
- [x] Malformed/unsupported filter combinations reject; no matches complete with zero items; stale exact handles report conflict without redirecting.
- [x] Cursors bind the full query/filter/page policy and reject substitution or relevant state changes. Identical request replay remains historical, not fresh.
- [x] Sparse/no-match scans preserve visit/slice/deadline/scratch limits and marked continuation semantics; no complete unbounded snapshot is built.
- [x] Public headless tests and thin MCP tests demonstrate the feature and preserve existing unfiltered object clients.

## Out of scope

New parameter value encoding, layer/node inspection, any new editing capability, container migrations, and result-store extraction.

## Comments

### Orchestrator recovery — interrupted first iteration

The subagent stopped at the harness stdout-event limit before reporting or
committing. Partial code and red/green logs exist, but no resolved outcome is
claimed. The orchestrator is parking that attempt off the integration chain
before a bounded continuation. Status remains ready-for-agent.

### 2026-10-02 iteration — resolved
Salvaged parked code WIP `syrnzstrsvlvnvyotyqwnzowynnszvmy` and completed MCP filter parsing, active-limit schemas/descriptions, and query documentation.
Object filters intersect, bind cursor policy, preserve historical replay and lifetime conflicts, and account retained filter state in shared scratch.
Added public sparse-scan/deadline/scratch checks; MCP red failed on missing schema maximum, then green passed. Prior headless red/green logs remain tracked under `logs/`; continuation logs are in `.scratch/broad-editing-logs/` outside the task subtree.
Deliverable change: `nxwqumkpzlsxyvzmpqluxopqxooptpnr`. Integration bookmark remained `622956eb` at commit time.
Verified: `cmake --build _out/_cmake --target vsrIntegrationTests -j 4` → passed; `_out/_cmake/vsrIntegrationTests '[object-filters]'` → 221 assertions in 5 cases passed; `ctest --test-dir _out/_cmake -R '^vsr::integration$' --output-on-failure` → 1/1 passed; `clang-format --dry-run --Werror src/vsr/integration/inspection/Inspection.hpp src/vsr/integration/mcp/tools/QueryTools.hpp src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → passed.
Holes filed: none

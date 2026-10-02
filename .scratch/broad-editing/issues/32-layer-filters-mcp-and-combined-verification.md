# 32 — Complete layer filtering, MCP exposure, and combined verification

**What to build:** Finish the original 25 layer observation contract with name-filtered queries through the public and MCP interfaces after 31 establishes safe lifetimes and bounded direct queries.

**Blocked by:** 31
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 4; Testing Decisions 2, 3, 4, and 12.
**Files:** src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/vsr/integration/mcp/README.md, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 25 before code. After 31, public and thin MCP layer queries have genuine opaque layer/root identities, unfiltered/direct queries, scene-order bounded paging, and notification-driven revisions. Layer name filters and their MCP exposure remain to be completed here. 01 already provides object filter, policy, scratch, and schema conventions to reuse.

## Acceptance criteria

- [x] Layer queries accept exact stored name and literal case-sensitive name_contains alongside exact layer handle; combined filters intersect. Duplicate stored names are legal; exact matching uses the real stored name, not a display preview or LayerMap key. No-match name queries succeed empty.
- [x] Reject object-only and node-only filters, unsupported kinds, malformed filters, and wrong-kind handles rather than ignoring them. Maintain stale exact-handle conflicts and authorization without widening permissions. Omission remains the existing object query.
- [x] Bind cursors to kind, all filters, page policy, scene/launch/revision and continuation position; reject changed query/state. Preserve historical replay. Sparse/no-match scans yield and terminate within all shared visit/slice/scratch/deadline/page/item/string limits, with cancellation and terminal scratch release; no hierarchy snapshot is built.
- [x] MCP schema/parsing/tool descriptions and discovery advertise precisely `objects` and `layers`, their supported filters, and active page/string limits. Returned layers expose handle, stored name, active flag and root-node handle. General `nodes` remains unsupported until 26. Document compatibility, handle kinds, and notifying host mutation expectations in the MCP README.
- [x] Public headless TDD covers intersecting filters, duplicate/exact/case-sensitive/substring matches, empty matches, invalid combinations, policy/cursor substitution, sparse bounded scans, replay, permission checks and lifecycle regressions from 31. Thin MCP tests cover schema, parsing, admission rejection and returned layer fields without duplicating business rules.
- [x] Recheck every original 25 criterion against completed 31+32, including safe ABA/reset/replacement/root lifetimes and ordinary-notification stability. Full integration tests, relevant existing core tests when applicable, and changed-C++ formatting pass. Record exact combined commands/results and both commit IDs; no criterion is deferred to 26.

## Implementation and verification guidance

Use the agreed public headless Integration safe-point seam with real Scenes, TDD one red/green behavior at a time, and thin MCP checks. No new test-only hooks. This ticket depends on 31's genuine lifetime implementation; it must not substitute pointer/slot/name/snapshot/final-value identity if that implementation needs correction. File a scoped blocker rather than silently weakening the original contract.

Use this workspace's own existing build or configure with progress.md's minimal flags; no CMake cache copying. Build with at most four jobs. Capture verbose logs in `.scratch/broad-editing-logs/` outside the task subtree. Verify targeted cases, full `vsr::integration`, and formatting; install changed libraries before tests if installed-library lookup can win. Verify current integration tip before reporting.

## Out of scope

New lifetime architecture (31), general node observations/lifetimes (26), fine node versions (27), ancestry dependencies (28), numeric transforms, edits/restoration, archive IDs, and unrelated refactors.

## Comments

### 2026-10-02 split from 25
Terminal replacement for 25: 31 → 32. Original 25 remains an unchecked, wontfix/split record; resolving 32 means all of its criteria hold across both parts. The orchestrator must reblock existing 26 on 32 before dispatching it; the splitting worker did not edit sibling tickets.

### 2026-10-02 iteration — resolved
Extended the existing kind-aware query engine with exact stored layer name and literal case-sensitive substring intersections; reused filter validation, cursor policy, shared bounds and thin MCP parsing. Updated MCP descriptions/README, not identity architecture or node/edit support. Deliverable change: `kowxsklxkmoxxpmurtkrzopulyssrsxk` (five explicit non-scratch paths, checked after commit); retry WIP: none.
Public headless TDD: `cmake --build _out/_cmake --target vsrIntegrationTests -j 4 && _out/_cmake/vsrIntegrationTests '[layer-filters]'` → red on name admission, then green (35 assertions/1 case). Thin MCP description red was followed by green; final coverage includes duplicate/stored/map-key/preview/case/literal/intersecting names, no matches, invalid admission, all filter/cursor substitution, historical replay, permissions, sparse visit/slice continuation, byte/item/string/scratch limits, cancellation, deadline and session-end release.
Combined original 25 audit across 31 (`uttoqkwvkrknwpxzwsmvuklkztqwnuns`) + this change: compatible objects/layers discovery and fields; disjoint opaque layer/root handles; exact/substring/handle intersections and rejection; scene-order shared bounded traversal/cursors; genuine creation/removal/reset/replacement lifetimes; root-preserving clear and ordinary-notification stability; public/MCP and existing integration regressions all hold. Every original criterion is covered; none is deferred to 26. No broader node/edit/interactive feature verification is claimed.
Verified: fresh workspace configured with the exact minimal `cmake -S . -B _out/_cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSR_USE_CUDA=OFF -DVSR_USE_INTEGRATION=ON -DVSR_USE_INTEGRATION_MCP=ON -DVSR_BUILD_APPS=ON -DVSR_BUILD_INTERACTIVE_APPS=OFF -DVSR_BUILD_UI_LIBRARY=OFF -DVSR_USE_LUA=OFF -DVSR_USE_NETWORKING=OFF -DVSR_USE_SDL3=OFF -DVSR_USE_TBB=OFF -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX="$PWD/_out"` → passed; `cmake --build _out/_cmake -j 4 && cmake --install _out/_cmake` → passed, before final tests.
Verified: `_out/_cmake/vsrIntegrationTests '[layer-inspection],[layer-mcp]'` → 591 assertions/9 cases passed; `_out/_cmake/vsrIntegrationTests` → 8,340 assertions/195 cases passed; `ctest --test-dir _out/_cmake --output-on-failure` → 36/36 passed, including integration, Forest and Scene. Logs: `.scratch/broad-editing-logs/32-*` outside tracked task subtree.
Verified: `clang-format --dry-run --Werror src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp src/vsr/integration/inspection/Inspection.hpp src/vsr/integration/mcp/tools/QueryTools.hpp` → passed. `jj log -r broad-editing --no-graph -n 1` → integration tip unchanged at `tllovwrm` / `74eaff5f`; all tests are on that base plus this deliverable.
Holes filed: none

# 10 — Commit and restore mixed object/node batches

**What to build:** Apply already supported object-parameter edits and placement edits together as one all-or-nothing live scene adjustment and one conditional restoration.

**Blocked by:** 06
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5, 7–9 for mixed batches; Testing Decisions 8–10 and 12.

## Current behavior

Object edits and the new node path work, but mixed target batches must be explicitly unified and verified rather than committed by independent loops.

## Acceptance criteria

- [x] Node and legacy object entries share a scene-scoped edits list, combined 64-entry limit, target-kind-aware replay identity, and compatible legacy envelope.
- [x] Validate all permissions, exposure, identities, target versions, values, dependencies, and preparation capacity together; one failed entry changes no target or version and publishes no committed journal.
- [x] Observers see only complete object/node state and one normal scene-sync acknowledgement; post-commit failure is reported honestly.
- [x] Return one Batch Reference, scene revision, object versions, and node_versions; do not change legacy object-only result meanings.
- [x] One bounded journal restores both target kinds atomically, with their appropriate narrow dependencies, defaults/unrelated state preserved, permanent conflicts, one-way consumption, and correct local/external authority.
- [x] Node fields added by other completed slices integrate without per-field special restoration forks; broader object parameters remain read-only.
- [x] Headless/MCP tests inject one stale object, one stale node, invalid fields, and preparation failure, then verify valid mixed commit/restore and existing replay behavior.

## Out of scope

Expanding the object parameter allowlist, result-store extraction, whole-scene undo, and changing synchronization/permission policies.

## Comments

### 2026-10-02 iteration — resolved
Existing 05/06/07 code already prepares, commits, notifies and restores object/node plans together; no production change was necessary. Deliverable `wlkvtwkktopwxuzxukwwsryotyvusxto` adds seven public headless/thin MCP regression cases covering matte opacity/color with matrix/position/enabled groups, one Batch Reference and both version lists, complete-state notifications, one synchronization acknowledgement, replay, shared 64-entry/journal/activity-byte limits, atomic stale/invalid/preparation failures, narrow local dependencies, permanent restore conflicts, consumption, exposure and local/new-session authority.
The initial regression run caught a test-only assumption: execution-time permission rejection has an operation-engine outcome without an edit-service result; assertions now use the public operation status. Restore preparation retains values, versions and eligibility; exact matrices/default baselines, unrelated parent transforms/names/light parameters survive. SRT08/world09 were not landed at dispatch; ticket12 retains the full cross-field verification after they land. No allowlist/container/result-store changes or holes. Current integration workspace remains an empty successor of base `1169c988`; `jj diff --from 1169c988 --to broad-editing@ --name-only` was empty, so no content merge was needed.
Verified: `cmake -S . -B _out/_cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/_out" -DVSR_USE_INTEGRATION=ON -DVSR_USE_INTEGRATION_MCP=ON -DBUILD_TESTING=ON -DVSR_BUILD_INTERACTIVE_APPS=OFF -DVSR_USE_CUDA=OFF` and `cmake --build _out/_cmake -j 4` → passed in this fresh workspace; `cmake --build _out/_cmake --target vsrIntegrationTests -j 4 && cmake --install _out/_cmake --prefix "$PWD/_out"` → passed after final test edits.
Verified: `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" _out/_cmake/vsrIntegrationTests '[mixed-edit]'` → 894 assertions/7 cases passed; `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" _out/_cmake/vsrIntegrationTests` → 15,633 assertions/239 cases passed; `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" ctest --test-dir _out/_cmake --output-on-failure -j 4` → 36/36 passed. `clang-format --dry-run --Werror src/tests/integration/test_IntegrationEdit.cpp src/tests/integration/test_IntegrationMcpTools.cpp` → passed. Logs: `.scratch/broad-editing-logs/10-{configure,build-final,regression-build-final,install,mixed-final,integration,ctest}.log` (outside task subtree). Interactive apps disabled; no GUI claim.
Holes filed: none

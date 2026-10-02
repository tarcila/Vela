# 08 — Edit faithful rotation/scale and group node fields

**What to build:** Rotate/scale existing transform placements consistently with VSR and combine position, rotation, scale, and enablement predictably without losing imported matrix state.

**Blocked by:** 04, 06, 07
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for conveniences/grouping; Testing Decisions 5, 8, 10, and 12.

## Current behavior

Read affordances, local position, and explicit matrix replacement exist. Rotation/scale conveniences and the complete grouped-field contract are not yet implemented.

## Acceptance criteria

- [x] rotation uses named azimuth/elevation/roll degree components and VSR's axes/order; scale uses three finite components. Zero/signed components are allowed only when resulting state is faithfully represented.
- [x] Reject non-finite/out-of-range inputs and unfaithful existing SRT reconstruction under the specified tolerance. Do not silently remove shear or otherwise normalize imported state.
- [x] Partial edits preserve translation unless position is specified. Retain supplied convenience angles consistently with VSR and full semantic request identity.
- [x] Group different fields per target with one consistent expected version; repeated fields and matrix-plus-SRT/position combinations reject before mutation.
- [x] Build proposed linear state then translation/enablement independently of entry order. Existing matrix/position/enablement behavior remains compatible.
- [x] Exact restoration preserves prior matrix/representation and default baseline, with fine conflicts, bounded journal, synchronization, and activity.
- [x] Headless and thin MCP tests cover independent angle/scale oracles, shear/reflection/degeneracy, field permutations, conflicts, and restoration.

## Out of scope

World-space rotation/scale, silent repairs, creation/wrappers, broader parameter writes, and generic result-store changes.

## Comments

### Orchestrator recovery — interrupted grouped-SRT pass

The worker hit the stdout-event limit with initial rotation support and an
initial green scenario, but grouped/permutation tests still fail. Park the
code-only attempt; a bounded continuation must correct proposed-state grouping
and exact restoration without weakening assertions, then verify MCP/full tests.

### 2026-10-03 iteration — resolved
Salvaged all five paths from WIP `znnswklnqultpumkwwpvqrvppwqxwnyk` onto clean failure `vyxkoqlupowoqvmtvpoptpszlpqywwtz`; incorporated integration bookmark `355a2e8c` (merged 10/74) before final verification. Deliverable: `mvsuskwswzyvptpwyvltrsqmopuuwysl`.
Retained every grouped/permutation/restoration assertion; fresh build/install passes them. Preparation initializes each node once, combines named degree rotation/signed-or-zero scale in proposed SRT, rebuilds only its linear part, retains translation, then commits one matrix/cache and independent enabled state per node. Added red/green parent-child tests in both orders: edit and restore ancestry results now refresh after ALL writes, with allocation-free rewalks of validated bounded chains. MCP replay/full-precision identity coverage and carrier documentation are complete.
World09 merger note: finalize every parent's proposed linear state before converting child world positions; never consume a parent state still awaiting this SRT finalization loop. This ticket implements local fields only; no world conversion is claimed.
Verified: `cmake --build _out/_cmake -j4 && cmake --install _out/_cmake --prefix "$PWD/_out"` → passed; `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" _out/_cmake/vsrIntegrationTests '[node-srt]'` → 1,216 assertions/6 cases passed, including thin MCP. Exact prior matrix/cache/default fidelity, all 24 field permutations, faithful signed/zero scale, unfaithful caches, malformed/range failures and restored final tokens pass.
Verified: `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" _out/_cmake/vsrIntegrationTests` → 20,822 assertions/249 cases passed; `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" ctest --test-dir _out/_cmake --output-on-failure -j4` → 36/36 passed; `clang-format --dry-run --Werror src/tests/integration/test_IntegrationEdit.cpp src/tests/integration/test_IntegrationMcpTools.cpp src/vsr/integration/edit/AgentEdit.hpp src/vsr/integration/mcp/tools/EditTools.hpp src/vsr/integration/values/EditableValues.hpp src/vsr/integration/restore/BatchRestore.hpp` → passed. Logs outside task: `.scratch/broad-editing-logs4/08-final-{build,install,full,ctest,format,focused}.log`. New dependency test failed 2 assertions before each edit/restore fix; no assertions weakened. Existing Release cache reused, four build/test jobs, no GUI claim.
Holes filed: none

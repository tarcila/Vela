# 06: Exact selection commit and conditional restoration

**What to build:** Commit the exact approved selection preview once, atomically and with current dependency/authority checks, then support established conditional restoration and historical retry replay.
**Blocked by:** 05
**Status:** resolved
**Milestone:** 1
**Specification:** [Truthful Views, Presented Frames and Safe Selection Translation](../spec.md), stories96–108/115–120 and decisions21–24.

## Current behavior

05 produces bounded edit-owned prepared plans and predictions. Existing atomic edit/Batch Reference/activity/conditional one-way restoration machinery remains the canonical commit path. This ticket completes real selection movement, not caller-generated low-level entries that lose preview/root-normalization guarantees.

## Acceptance criteria

- [x] Add admitted vsr_translate_commit using prepared reference plus its own request identity. Commit exactly the retained plan, not caller-modified translation inputs or a newly recomputed normalization. Validate source/launch/session and plan lifetime before exposing content or writing.
- [x] Recheck every correctness-bearing selected/root identity, node/structural/ancestry/normalization dependency and current read/edit/exposure immediately before mutation. Changed relations or relevant state fail stale/conflict/expired with zero writes and no silent rebase. Preserve existing narrow-dependency behavior for unrelated changes.
- [x] Reuse real edit prepare/commit allocation/restoration receipts so all roots commit or none. Preserve linear transforms, exact versions/ancestry histories/callbacks/batches and owner/count guarantees from existing source. Unsupported/singular/overflow/ineligible roots cannot cause partial success.
- [x] Successful commit consumes the preview once; another request cannot apply it again. Same admitted request replay returns its historical receipt under currentvisibility without mutation even after consumption. Definite staleness cannot revive through change-back; expire/evict/foreign tokens fail truthfully.
- [x] Preserve before/after-commit cancellation distinctions, activity and bounded cleanup. Return normal edit outcome/Scene revisions/resulting versions/Batch Reference/restore eligibility; no implicit rollback disguised as cancellation.
- [x] Existing conditional one-way restore recovers the exact original local matrices when eligible, with target/ancestor history, exposure/permissions and later-write/change-back conflicts intact. Failed validation creates no fake batch/restore record.
- [x] Public headless/MCP tests cover preview→commit→reinspection→restore, nested selection once-only world displacement, relevant hierarchy/permission/exposure changes, valid unrelated edits, consumed/expired/foreign plans, retries, cancellation, all-or-nothing failures and current result suppression. Reuse05prediction path, not duplicate maths.
- [x] Enabled fresh build/install, full relevant edit/restore/identity/protocol regressions and CTest, memory/plan/session cleanup and formatting pass with actual counts. Provide cumulative selection requirement evidence and preserve existing low-levelvsr_edit behavior.
- [x] Final integrated acceptance after both04and06 verifies the whole spec with fresh independent Standards/Spec review, required fixes and full evidence mapping. This is a final integration-stage gate, not a new dependency that makes commit require image retrieval. Live user chess edits/restoration or viewer restart require separate explicit permission; use isolated test Scenes otherwise.

## Out of scope

Displayed-frame implementation/fallback, fresh capture/quality, image cap/8K changes, topology creation, group rotation/scale/legal chess, broad result-store/authority redesign, user configuration or historical tracker closure.

## Comments

Only05 genuinely gates this new operation. The frame chain can finish independently, but the integration branch is not complete until both chains and final review pass. One fresh appropriately budgeted context; final independent review/merging is orchestrator work, not author self-review or a duplicate missing-review-route hole.

### 2026-10-05 iteration — resolved
Added admitted `vsr_translate_commit` bound to the entire generated opaque preview reference (including Scene routing metadata). It commits retained exact matrix/SRT pairs through the shared real edit allocation/atomic write/receipt/restoration phase, never recomputes or promotes an unselected parent, and validates every selected/inherited dependency plus authority/lifetime/deadline again after allocations before writes. Successful consumption is once-only; historical same-request receipts survive restore/expiry under current visibility. Stale/foreign/expired/allocation/permission/deadline failures have zero writes and no usable batch.
Normal conditional restore preserves exact matrices/cache/defaults and permanent target/ancestor change-back conflicts. Cancellation, sync failure, owner retirement, session close and current read suppression retain truthful committed outcomes. A narrowly tested one-condition engine correction allows already committed synchronization/cleanup after authority withdrawal, and activity classifies translation commits as normal edits; no broad manager/result-store refactor. Limits remain64/8/32/16MiB/120s/5s with bounded ancestry and physical-copy charges.
Verified (workspace root, installed `$PWD/_out/lib` first in `LD_LIBRARY_PATH`, workspace-local `TMPDIR`; full suites serialized):
- `cmake --build _out/_cmake --config RelWithDebInfo -j3` then `cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` → exits0, existing enabled CUDA/UI/network/Lua/SDL3/integration/MCP configuration preserved. `clang-format --dry-run --Werror` on all seven changed C++ files →exit0.
- `vsrIntegrationTests '[translation-commit]'` →2142 assertions/9 cases; union `'[translation-commit],[translation-preview]'` →3275 assertions/18 cases, including all05's1133 assertions/9 cases. Actual HTTP/MCP preview→commit→reinspect→restore plus exact real-Scene matrices,64 roots, normalized/inherited identity, narrow conflicts, one-use/replay, cancellation/authority/allocation/deadline negative controls and owner/copy/session cleanup.
- Cumulative01–06 public tag union →10806 assertions/58 cases. Full `'[integration]'` →127727 assertions/364 cases (poll totals vary). `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` →68/68 pass,72.64s; exits0.
- Targeted Valgrind `'[translation-commit],[translation-preview],[node-edit],[node-position]'` with `--error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect` →10086 assertions/43 cases,exit0,0 errors and0 definite/indirect lost bytes.2392 possibly-lost bytes/15 blocks,4186672 reachable bytes/180 blocks and driver warnings remain disclosed; no suppressions/universal GPU/GUI sanitizer claim.
- Workspace's rebuilt headless tutorial `--exit-after-activation` after install, own runtime directory →exit0. Runtime executable/device hashes and loader paths recorded; this configuration's tests are build-tree executables with statically linked VSR code, not an invented installed SDK path.
Evidence, exact commands/log pointers, TDD red failures, requirement mapping and limitations: [06-verification.md](../06-verification.md). Parent/bookmark equal dispatched `723fa3a765c3906629e8a1acebe72b77c54f8e76`; immutable spec hash unchanged. No other ticket/spec/addendum/ledger/executor/config/workspace or user's live Scene/viewer/controller changes. No new GPU or upstream model-generation qualification claim.
**Final independent whole-spec Standards/Spec review remains pending with the outer orchestrator after merging; the final integration-stage checkbox above is intentionally open.** Functional06 is verified, not a declaration that the complete integration branch has passed its final gate. Earlier accepted frame qualification and its disclosed instrumentation limits remain intact.
Commit: `wpopuxzqxrouqompnmyymxpxmsmpvyov` (one verified code-plus-ticket change).
### Final integration-stage acceptance
Both frame and translation chains landed. Independent Standards and Spec reviews raised zero required findings; three standards suggestions remain optional, not asserted defects. A follow-up maps all120stories with direct-source versus evidence-only/partial-inspection classifications; no exhaustive assertion-body, affine/PNG arithmetic re-derivation or older-trunk approval is claimed. Final merger independently verified68/68CTest,364integrationcases,43targeted Memcheckcases with0errors/definite/indirect leaks. Supported GPU presentation-profile and external-tool limitations remain intact. See [finish-review.md](../finish-review.md), [final-standards-review.md](../final-standards-review.md), [final-spec-review.md](../final-spec-review.md) and [final-spec-coverage.md](../final-spec-coverage.md). The integration-stage checkbox closes on this evidence; no live user mutation/restart or fresh one-off capture is inferred.

Holes filed: none. New tickets/splits: none;07–12 remain unused. Exact base-workflow outcome: [06-outcome.json](../06-outcome.json).

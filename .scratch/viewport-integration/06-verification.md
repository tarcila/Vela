# 06 — Exact selection commit and conditional restoration

## Outcome and scope

Resolved functional ticket 06 in workspace `/home/tarcila/Code/ANARI/vela/viewport-translation-commit`. No holes or splits; reserved 07–12 remain unused. **Final independent whole-spec Standards/Spec review and any resulting fixes remain the outer orchestrator's integration gate.** This author inspection is not independent review, and this ticket does not declare the complete integration branch finished.

Dispatch started from empty worker change `wpopuxzqxrouqompnmyymxpxmsmpvyov`, parent/integration bookmark commit `723fa3a765c3906629e8a1acebe72b77c54f8e76` (change `xunlopkplptlyylxslnqknxntywwltvr`). Both parent and `viewport-integration` were positively identical before source changes and again after verification. No reset, rebase, merge or bookmark movement was performed.

Immutable spec SHA-256 before and after implementation: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`. No addenda. Read ticket, blocker 05, approved executor, base ticket workflow and TDD guidance before implementation; read coding standards completely during implementation, before final verification. Existing local issue/domain setup was already present; no setup/configuration rewrite was necessary. Confirmed seams were the spec's public headless real-Scene integration and admitted MCP/HTTP interfaces.

Only eight relevant source/test/doc files changed:

- `src/vsr/integration/edit/Translation.hpp`
- `src/vsr/integration/edit/AgentEdit.hpp`
- `src/vsr/integration/mcp/tools/EditTools.hpp`
- `src/vsr/integration/activity/Activity.hpp`
- `src/vsr/integration/operations/Operations.hpp`
- `src/tests/integration/test_IntegrationEdit.cpp`
- `src/tests/integration/test_IntegrationMcpTools.cpp`
- `docs/live-placement-editing.md`

The Operations change is a single conditional correction, not a manager/result-store redesign: committed synchronization/cleanup must finish after authority withdrawal/session close. Before it, read withdrawal between mutation and synchronization stranded a committed operation with no terminal receipt. Current read still gates all protected result retrieval. Activity adds only `translate_commit` → normal edit classification. No generic map migration, new authority fields, encoder/image limits, Scene core counters, viewer/device reset or configuration changes were made.

## Implementation contract

- Qualified hosts now expose `vsr_translate_commit`. It accepts its own `request_id` and the exact generated `preview_ref`, not replacement selection/delta/matrices.
- The reference adds `scene_id` routing metadata alongside opaque launch/preview identity. The tool requires clients to copy the entire returned reference. Routing is canonicalized for replay and checked against the retained plan; it grants no authority. Keeping routing in the reference permits historical request replay after plan consumption/expiry without a separate unbounded routing store or retained Scene pointer.
- Commit resolves the edit-owned immutable plan only for its owner, launch and Scene, under current read/edit/exposure and lifetime checks. It validates **every** selected node, including inherited children, plus narrow structural/ancestry dependencies and the retained topmost-selected root relation. No common-parent promotion or new preparation/rebase occurs.
- Exact approved matrix/SRT pairs are copied to ephemeral live `PreparedBatch` nodes. The common `EditService::commitPrepared()` path performs the existing allocation, Batch Reference, restoration reservation, atomic Scene write, deferred signals, resulting versions and normal synchronization steps. Low-level `vsr_edit` still prepares normally and delegates to that same phase.
- A final gate after planned allocations repeats authority, source/plan lifetime, deadline and precise selected dependency validation before any Scene write. Denial cancels unpublished restoration/activity reservations. Bounded-work `busy` is not incorrectly classified as permanent staleness; definite stale plans retire and cannot revive on change-back.
- Successful mutation consumes the plan once. Different requests cannot reuse it. Same admitted canonical request replays the historical normal receipt, including after restoration or plan expiry, without mutation or new token consumption. Engine record eviction remains `outcome_unavailable`, not a new move.
- Root restoration receipts retain world/ancestor-history requirements. Exact matrices and cached representation restore once when eligible; target/required-ancestor writes, including change-back, retain permanent conflict behavior. Child local matrices and versions are not written merely because they inherit the move. Reset defaults and unselected ancestors remain untouched.
- Queued cancellation writes nothing. Post-commit cancellation is not rollback. Synchronization failure retains committed/batch metadata under current read. Committed metadata also survives Translation owner retirement without dereferencing that owner. Session close/read withdrawal no longer strands committed cleanup.
- Preview remains non-mutating and creates no batch/restore eligibility. Bounds/defaults remain 64 inputs, 8 plans/session, 32 global plans, 16 MiB aggregate charged ownership, 4096 ancestry steps, 64 selected effect entries, 120-second lifetime and 5-second operation deadline. Historical metadata and copied results keep their leases until actual record/copy release. No cap lift.

## TDD and failed attempts (not passing evidence)

1. First added a real transported preview → commit → restore scenario. Initial test compilation used a nonexistent `json::serialize`; corrected to the actual JSON interface. `06-red-build.txt`/`06-red-install.txt` then built/installed successfully; `06-red.txt` positively failed at the missing `vsr_translate_commit` tool: 1 case, 54 assertions, 1 failure. Added the shared exact commit/tool path; `06-green.txt` passed 116 assertions/1 case.
2. Added real-Scene dependency/atomicity tests. Compilation initially used incorrect Scene API spellings; corrected to `setAsObject`, `move_subtree`, `getValueRaw`, and source reattachment (node exposure is immutable registration state, not an invented setter). Initial expectations also omitted truthful `session_expired` and tried a second occupied controller. Fixed expectations and used detach/new-owner plus both host/external isolation directions. Intermediate failing logs are `06-deps-build.txt`, `06-deps.txt`, `06-boundaries-red.txt`.
3. Boundary tests positively found loss of committed metadata after Translation owner retirement and stranded synchronization after read withdrawal. Implemented value-owned receipt completion and the narrow committed-cleanup correction. `06-boundaries-green.txt` records the remaining withdrawal failure before that correction; `06-lifetime.txt` then passed 1692 assertions/7 cases.
4. Added post-allocation read-withdrawal/deadline negative controls. `06-boundary-gate-red.txt` positively failed with 15 assertions before adding the final no-write gate and reservation cleanup. `06-boundary-gate-green.txt` passed the complete selection union afterward.
5. Activity/session-close test positively failed edit classification (inspection vs edit) in both sections: `06-activity-red.txt`, 2095 assertions/9 cases with 2 failures. Added the single classification mapping. Final verification below includes this test passing.
6. Added literal affine math/exact approved-matrix/cache/default restoration checks, physical result-copy accounting, 64-root host commit, queued owner/source/session retirement, reinspection, malformed pre-admission requests, canonical request conflicts, cross-authority isolation and Forest storage relocation. Final results below are from the final source, not the intermediate counts.

One shell probe chain stopped on an absent glob during exploration (`exit=2`) before running a planned build; it was rerun as an explicit build/install/test command. No absent match or failed command is counted as successful verification.

## Final runtime freshness and commands

Existing warm Ninja Multi-Config tree reused without configuration/optimization changes. `CMAKE_HOME_DIRECTORY` and install prefix both point to this workspace. CUDA, interactive/UI targets, networking, Lua, SDL3, integration and MCP remained enabled. No foreign workspace output tree was used.

All runtime commands below ran from this workspace with:

```
export LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}"
export TMPDIR="$PWD/.scratch/viewport-integration/tmp"
```

The separate headless smoke additionally used workspace-local `XDG_RUNTIME_DIR="$PWD/.scratch/viewport-integration/runtime"` (mode 0700). Full suites were serialized. Logs are workspace-local and ignored; durable proof/outcome/ticket are explicitly tracked.

| Verification | Exact command after the exports | Final result / log |
|---|---|---|
| Enabled build | `cmake --build _out/_cmake --config RelWithDebInfo -j3` | exit 0; `logs/06-verified-build.txt` |
| Install before runtimes | `cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` | exit 0; `logs/06-verified-install.txt` |
| Formatting | `clang-format --dry-run --Werror` on all seven changed C++ headers/test files | exit 0; `logs/06-verified-format.txt` |
| Selection union | `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit],[translation-preview]'` | **3275 assertions / 18 cases**, exit 0; `logs/06-verified-selection.txt` |
| 06 alone | `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit]'` | **2142 assertions / 9 cases**, exit 0; `logs/06-commit-only.txt` |
| Cumulative 01–06 public regressions | `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[request-guidance],[view-identity],[view-info],[presented-frame],[viewer-presentation],[translation-preview],[translation-commit]'` | **10806 assertions / 58 cases**, exit 0; `logs/06-cumulative-focus.txt` |
| Full enabled CTest | `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` | **68/68 pass**, 72.64 s, exit 0; `logs/06-verified-ctest.txt` |
| Full integration | `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[integration]'` | **127727 assertions / 364 cases**, exit 0; `logs/06-verified-integration.txt` (polling assertion totals vary between runs) |
| Targeted memory | `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --log-file=.scratch/viewport-integration/logs/06-verified-valgrind.txt _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit],[translation-preview],[node-edit],[node-position]'` | **10086 assertions / 43 cases**, exit 0; tests `logs/06-verified-valgrind-tests.txt`, memory `logs/06-verified-valgrind.txt` |
| Isolated headless executable smoke | `_out/_cmake/RelWithDebInfo/vsrTutorialIntegrationHeadless --exit-after-activation` | exit 0; `logs/06-headless-smoke.txt` |

`logs/06-runtime-provenance.txt` records executable/library SHA-256, loader output and runtime paths. The VSR libraries in this configuration are linked into the freshly rebuilt test executable; it is not an installed test executable/header SDK. The installed dynamic ANARI VSR device exactly matches this workspace's build-tree device hash. Installed `_out/lib` was first in lookup before every final runtime. An absent `_out/bin` binary was not substituted or called a passing smoke.

Valgrind: **0 errors / 0 contexts**, no suppressions; **0 definitely lost and 0 indirectly lost bytes**. 2392 possibly-lost bytes/15 blocks and 4186672 reachable bytes/180 blocks remain. Total heap 127135 allocations, 126940 frees, 39187945 bytes allocated; 4189064 bytes/195 blocks at exit. Driver/ioctl warnings remain disclosed. This is targeted public headless translation/edit memory evidence, **not** clean universal/GPU/GUI instrumentation.

## Requirement mapping

| Stories / ticket criterion | Public functional evidence |
|---|---|
| 96–98; exact retained plan, no silent rebase | `Translation commits exactly the approved affine matrices and caches`: actual Scene matrices equal the approved public preview matrix element-for-element and independent literal local coordinates under rotation/shear/reflection/zero scales/fractions. `Exact selection commit validates every dependency atomically`: root/inherited child, hierarchy/reparenting, ancestor and target change-back, source reattachment/foreign identity rejection. |
| 99–101; atomicity, narrow dependencies, current authority | Same dependency case checks before/after matrices/raw value, Scene revision, node version, notifications and zero restoration records on every failed path. Valid unrelated sibling/material writes and Forest relocation succeed. Post-allocation authority/deadline denial is a tested zero-write boundary. Queued read/edit withdrawal and inherited-node removal reject. |
| 102–104; one use, replay, normal receipts/activity | MCP scenario checks same-request replay, different-request consumption denial, canonical request conflict and genuine pre-admission malformed request correction. Receipt/history test replays unchanged committed metadata after restore; physical-accounting case verifies outcome eviction never mutates. Activity case checks normal EDIT kind, eligible batch, committed activity, synchronization completion and held slots returning to zero. |
| 105; exact conditional restore | `Translation receipts preserve exact conditional restoration histories`: exact matrix/SRT restore; own/ancestor change-back permanent conflicts; ancestor enable-only/unrelated state stays narrow; subsequent one-way outcomes. Affine matrix/cache/default case repeats exact restore across five supported math configurations. MCP performs normal `vsr_restore` using the real returned Batch Reference. |
| 106; cancellation/sync/visibility | Boundary case covers cancellation between mutation and hook and during hook, sync failure with batch metadata, owner retirement, read suppression. Queued cancellation and deadline denial have no writes. Activity case covers session detach after commit, hook completion and local conditional recovery. |
| 107–108; ownership/bounds/lifetime/isolation | Existing preview ownership tests plus new `translation-memory` cases: 64 root commit, 2-second local plan expiry, both local/external owner isolation directions, queued SceneIdentity/EditService/Translation retirement, session close and removed inherited identity. Result-copy case uses a real two-record engine limit to evict preview/commit history and proves charges remain only for the held public Status copy, then return to the 64 KiB owner charge on actual release. |
| 115; transported end-to-end | `MCP translation commits the retained plan once and restores`: real loopback HTTP tools, fresh inspected handles/tokens, preview → commit → fresh public node reinspection → exact normal restore; generated reference includes routing; malformed input consumes no request ID; same request does not apply twice; read withdrawal removes protected result but preserves owner committed/outcome receipt. |
| 117–120; base, preservation, truthful evidence | Exact dispatched parent/bookmark/spec hash verified; own enabled build/install, positive matched counts and targeted memory evidence above. No other workspace/configuration/ticket/spec/ledger/executor or live user Scene/controller changes. |

Preview's existing **1133 assertions / 9 cases** remain present in the final selection union; 06 contributes **2142 assertions / 9 cases**. Full source-dispatched integration baseline was 355 cases; final is 364. Existing low-level edit/restore, identity, protocol, pipeline/Forest/Data Tree and presented-frame/UI regressions pass through full CTest/integration and the named cumulative focus.

## Limits and integration-stage evidence

- Earlier accepted frame/provenance/diagnostic source remains untouched. Cumulative focus reruns its public regressions but does not independently requalify every production GPU/device. Preserve prior accepted real barney/CUDA evidence and its limits from `04-c11-verification.md` and related 04 artifacts, plus `01-verification.md`, `02-verification.md`, `03-verification.md`, `05-verification.md`.
- No restart, camera/device change, edit or restoration on the user's live `main_scene`; no takeover of its viewer/MCP controller. All mutations here used isolated test Scenes; no permission for a live user edit is inferred.
- No new GPU-memory, Compute Sanitizer, upstream model-generation or universal parity claim. Prior GUI Valgrind external errors, unsupported OptiX instrumentation and timed-out Massif qualification are not relabeled clean by this CPU math work.
- Core lifetime/history counter wrap behavior is unchanged; no injected 64-bit counter-wrap qualification is claimed. Normal precise history/reuse/relocation/change-back behavior is exercised through public APIs. No private stamp/counter mutation was used to manufacture a result.
- Final independent whole-spec Standards/Spec review is **pending outside this worker**, including any integration fixes and evidence reconciliation. The last ticket checkbox is deliberately left for that final gate; it is not a new missing-review-route hole or a claim of author independence.

One verified code-plus-ticket commit uses change `wpopuxzqxrouqompnmyymxpxmsmpvyov`; exact base-workflow JSON is `06-outcome.json`. Before publication, the source parent/integration tip and immutable spec hash were reconfirmed. The merger, not this worker, will land that single commit and advance integration.

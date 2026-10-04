# Ticket08 — queue-rejected cold cleanup retry

Workspace: `/home/tarcila/Code/ANARI/vela/viewport-first-observation`.
Source prerequisite: `mvtwylnnszqqnqmnlsnwzpwqmrqmspyz` / `500585a2a74f10f9b7c0d41fa1c2085d26b64eb1`; integration bookmark: `992ad347c74251d50f8bef6c53ef1001f3ec4a4b`. Verified the expected owned prerequisite stack descends from that bookmark, not direct parent equality. Initial empty worker `555ff05e`, change `mwtsorrxxvnkynkpoxnzyvlrkyrvukuo`. `jj diff --from 6bc45b1d660bb3f4369a1acab08b97dfa90b2880 --to 500585a2a74f10f9b7c0d41fa1c2085d26b64eb1 --name-only src` is empty: ticket07 source survived its metadata rebase unchanged.

Immutable before/after SHA256:
- spec: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`
- addendum01: `62fae22eb49870782ffe14f9d84acfeb7672741c8e5bf96b1ff79d52323f999b`
- worker `.envrc`: `ae403994962fe2f5bd2fa0b7a9f360cc99440a34ac90066841012a7b661bd9af`
- worker `.pi/mcp.json` absent before work and remains absent; no user/other-workspace configuration touched.

## Causal RED / local correction

Read parent-authored `../session-friction/07-gpt-review.md`: report `581d17f8-c921-4b19-92ae-8a23c9227ad4` is one user-selected GPT response, not quorum/panel consensus. Parent positively verified the candidate source quote. This run independently reproduced its runtime consequence, without claiming the reviewer ran tests.

Approved seam: public Integration, real Scene, FrameService and OperationEngine with the existing real Helide/Image Pipeline/SDL host. No mocks or private fields. A cold operation enters backend-pending without an encoder job. From a genuine Scene safe point the test fills `Integration::queueCapacity()` with real accepted callbacks and positively asserts another `Integration::submit` returns false. Two pumps attempt cleanup while that queue remains full. Draining those callbacks provides no worker wake or implicit retry; only a later host pump can schedule cleanup.

Before production correction, own build/install plus `vsrIntegrationTests 'Cold cleanup retries*'` exited10: **1 case / 4357 assertions / 10 failures** (`logs/08-red.log`). Expiry, read withdrawal and session withdrawal lost cleanup: the later service ran zero executor callbacks, held one slot and retained 16936 charged bytes. Cancellation was already correct (its abandoned activation was not immediately pruned); it is retained as a negative/regression control. Successful public queue-fill/rejection assertions preceded the failures; this is not a mocked dispatch hypothesis.

Correction is ONLY a pump pruning guard in `src/vsr/integration/frame/FrameObservation.hpp`: do not erase an activating Job. Capturing a wake or successfully enqueueing it is not completed cleanup. The same bounded/charged Job and wake remain until the executor's `observe()` clears activation. Failed dispatch is retried only by a later host pump; no recursive wake, new queue, timer, callback, capacity or lease is added. OperationEngine's existing dispatched guard deduplicates repeated pumps before execution. Once cleanup clears activation, ordinary denial/expiry pruning reclaims the Job. The executor owns its original Job/Scene cleanup route throughout; no observer revival, source migration or authority bypass is introduced.

Immediate GREEN for the original four-section regression exited0 (`logs/08-green.log`). Final regression adds narrowed two-second source expiry before the 120-second admission deadline, and a second case completes/reclaims 70 cancelled activations without waiting out retained lifetimes (exceeding the 64-Job ceiling). Final **2 cases / 6418 assertions**, exit0 (`logs/08-regression-final.log`). It proves one queued executor cleanup despite two pumps, slot retention until actual execution, empty queue/no selfwake spin, exact baseline bank reclamation, no observer restart and zero render calls.

## Enabled own build and serialized verification

Every shell begins by cd to this workspace. Build/install before runtime:
`cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo`
→ exit0 (`logs/08-final-{build,install}.log`). Existing warm tree only, own source/install prefix positively checked. CUDA, interactive apps/UI, networking, Lua, SDL3, integration/MCP remain ON; no reconfigure, foreign cache deletion, shared output or feature disable. Installed lookup first: `LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}"`; absolute workspace-local `TMPDIR="$PWD/.scratch/viewport-integration/tmp"`. Tests serialized.

Final commands/results:
- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` → exit0, **68/68** (`logs/08-ctest.log`).
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests` → exit0, **372 cases / 135957 assertions** (`logs/08-integration.log`).
- Same executable `'[presented-frame],[viewer-presentation],[first-observation],[mcp]'` → exit0, **32 cases / 13982 assertions** (`logs/08-focused-final.log`). Existing admitted MCP first-operation, warm exact pin/replay, lifecycle, ownership/HTTP drain, meaningful profile/resource errors are included. Earlier focused invocation also contained unmatched `[operations]`; that unmatched filter is NOT evidence. Final matching invocation above supersedes it, and the full integration executable covers engine regressions.
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation],[first-observation]'` → exit0, **32 cases / 11903 assertions, 0 errors, 0 definite/indirect lost bytes** (`logs/08-memcheck.log`). Possible loss **29136B / 185 blocks**, reachable **23475945B / 18905 blocks**, suppressed **32B / 1 block** disclosed. Unhandled ioctl/address-range driver warnings remain. No GPU/GUI/sanitizer/whole-RSS clean claim.
- `clang-format --dry-run --Werror src/vsr/integration/frame/FrameObservation.hpp src/tests/integration/test_IntegrationPresentedFrame.cpp` → exit0 (`logs/08-format.log`).

Counts containing bounded worker polling can vary with timing; the above are actual outputs, not inherited ticket07 totals.

## Fresh actual production first-operation PNG evidence

`VSR_VIEWER_TEST_LOG_LABEL=08-final python3 .scratch/viewport-integration/07-production-viewer.py DEVICE`
uses existing Node24.21/current Pi1.0.2 MCP harness, independent diagnostic SDL framebuffer oracle and separate authenticated Xvfb/XDG runtime/config/controller. Both exit0. No second frame request rescues the first; no Scene writes or extra render requests. All 32 returned PNGs per device pass native-size, orientation/color, black/flipped negative controls, matched identity/times/revisions, resize and identical historical replay checks. Existing thresholds unchanged.

| Device | Prior successful displays | First operation presentation | PNG/cold retries | Independent sampled pixels | Maximum mean RGB error | Busy retries |
|---|---:|---:|---:|---:|---:|---:|
| barney | 18 | 22, available | 32 / 0 | 93568 | 1.300382653 | 0 |
| helide | 12 | 15, available | 32 / 0 | 93568 | 2.644557823 | 0 |

First Barney submission/observation:24109.433608247 /24109.433723960; Helide:24116.843480277 /24116.843594716. These are later actual observation safe points, not admission times. Tracked proofs: `04-08-final-{barney,helide}-first-observation-proof.json`; runner/client/viewer logs under `logs/08-production-*` / `logs/04-08-final-*`. Total: **64 PNGs / 0 cold retries / 187136 independent sampled pixels**. Prior07 proofs untouched.

## Fresh real SDL lifecycle and bounded pressure

Runner `04-live-viewer.py`, installed changed tree, isolated test instances only:
- `VSR_VIEWER_TEST_LOG_LABEL=08-lifecycle python3 .scratch/viewport-integration/04-live-viewer.py DEVICE probe`
- `VSR_VIEWER_TEST_LOG_LABEL=08-pressure VSR_VIEWER_TEST_DESTRUCTIVE=1 python3 .scratch/viewport-integration/04-live-viewer.py barney pressure-probe`
- `VSR_VIEWER_TEST_LOG_LABEL=08-entropy VSR_VIEWER_TEST_DESTRUCTIVE=1 VSR_VIEWER_TEST_ENTROPY=1 python3 .scratch/viewport-integration/04-live-viewer.py barney pressure-probe`

| Run | Cases/assertions | PNG / generic unavailable | Heartbeats / primary submissions | Independent compared pixels | Peak owned bytes |
|---|---:|---:|---:|---:|---:|
| barney lifecycle | 1 / 957 | 46 / 2 | 125 / 112 | 111288 | 24543330 |
| helide lifecycle | 1 / 966 | 47 / 1 | 115 / 112 | 113412 | 24891347 |
| barney two-source pressure | 1 / 1363 | 35 / 13 | 132 / 120 | 1083873 | 264030843 |
| barney entropy/drain pressure | 1 / 1234 | 25 / 23 | 125 / 112 | 251573 | 183099090 |

All exit0; exact SDL identity oracle remains required. Real camera/renderer/Outline/resize/source-instance retirement, cancellation/read suppression and surviving secondary source asserted. First actually qualified operation succeeds; fixture's initial incomplete binding still returns inactive_binding. Legacy `cold` counter aggregates ALL unavailable results, not cold_cache retries. Secondary continues105 submissions after retirement in pressure (97 entropy); off-owner starts/mismatched acquisitions0. Entropy positively records real stalled HTTP drain start/hold/release,6178906B maximum PNG and192980700B drain-held bank. Earlier attempts with mode `probe` plus destructive/entropy env did not enable two-source pressure (secondary/drain counts0); their passing ordinary-lifecycle logs are NOT claimed as pressure evidence. Correct `pressure-probe` runs above supply that evidence. Initial08-final Barney probe raw log was replaced by another08-only ordinary probe; final unique labels above avoid ambiguity and preserve all historical07 records.

## Limits / closure

Source epoch/device/cancel/warm pin/history/cold same-operation semantics and original policy limits unchanged:2048axis,2073600pixels,8MiBPNG,12MiBresponse,16MiBproxy,256MiBshared bank,64retained Jobs,32operation slots,120s maximum admission lifetime,one encoder. No global queue/OperationEngine/result-store policy change. Normal qualified SDL_CPU_READY_V1 HOST/managed-host upload at unit framebuffer scale only. GPU-only/unknown upload/output/non-unit scale remain truthfully unavailable. No universal barney/GPU/sanitizer/physical-monitor/UI-overlay/sample-quality/RSS/VRAM claim. Existing external GUI-driver/tool possible-loss limits are not waived.

Only08 code/tests/ticket/proof records changed; no spec/addenda/07 ticket/source proof/ledger/executor/bookmark/controller/user state or unrelated workspace mutation. No new holes/tickets. Completion fits comfortably before the55-minute stop assessment; no need to extend the working budget.

Ticket08's functional criteria are verified. **Outer GPT-only corrected frozen-package review, independent runtime acceptance, integration landing/fixes and final whole-spec review remain pending.** Author inspection is not independent review, single GPT is not panel consensus, and this correction does not complete bounds/edit/image stages or the full integration specification.

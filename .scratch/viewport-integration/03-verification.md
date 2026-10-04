# 03 — CPU presented-frame verification

**Latest outcome: resolved for the qualified Linux CPU slice only.**
The 2026-10-04 continuation below completes the prior owned-memory stop using
its preserved source. Default production exposure stays false; real viewer/
barney/GPU qualification (04) and final whole-spec independent review remain
pending. See [03-memory-proof.md](03-memory-proof.md) for allocation/lifetime
bounds, actual guarded-stack syscalls and independent heap evidence.

## Prior iteration outcome and immutable base (historical)

Outcome: **ready-for-human**, not resolved. No spec/addenda hole and no new
issue/split is filed: the remaining problem is an implementation/qualification
invariant in this same coupled CPU ownership slice, not an undecided spec.
The immutable spec SHA remains
`e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
Worker parent and untouched `viewport-integration` bookmark were reconfirmed at
`df45dba361dcedc6cf3cab20b6547f868d0bc3f8` before source changes and final recording.
Retry WIP: none. All work, build outputs, installs, TMPDIR and logs are local to
`/home/tarcila/Code/ANARI/vela/viewport-cpu-frame`.

## Implemented public CPU vertical behavior

- Rendering-local CPU snapshot ownership and shared reservation pool, independent
  of Scene/UI/integration wire types; non-copyable immutable consumer objects.
- Explicit ImagePipeline HOST storage with CUDA still enabled; default pipeline
  behavior preserved. Production generation advances after successful passes.
- Pre-production matched real binding/revision receipt, successful actual display
  callback association, generation/resize-reuse negative control, request-point
  latest pinning and deferred immutable CPU encoding. No fallback/render/reset
  is performed by observation or image retrieval.
- Public admitted `vsr_view_frame` and kind-specific `vsr_view_frame_image`,
  historical replay, current Scene/view read gates, minimal withdrawn receipts,
  cold authorized-observer activation, cancellation/drain, expiry, resize,
  close/recreate, exposure retirement and explicit oversize/exhaustion reasons.
- Existing PNG normalization/output bound and base64 primitives are reused;
  response leases survive ToolResult-to-HttpResponse serialization and socket
  drain. Exposure declarations stay separate from fresh-capture grants/policy.
- Actual Scene/RenderIndex/Helide natural production through ImagePipeline and
  SDL3 software RenderTexture/RenderPresent, with independent PNG decoding.
  A final procedural asymmetric pass is a pixel oracle, not an operation/cache
  simulator. Another case disables it and observes the genuine Scene image.
  In-pipeline Outline, native/display scale, alpha and bottom-left normalization
  are tested, including the maximum1920x1080 native image. Loopback MCP uses the
  same admitted source contract and preserves legacy independent render behavior.

## Load-bearing remaining limit

The pool covers bounded native snapshots, retained receipts/operation metadata,
PNGs, encoder scratch and response/base64/drain reservations. **It does not yet
charge the fixed std::thread stack and service/map/future bookkeeping separately.**
A reservation-counter bound and sampled peak below256MiB are not a proof of the
entire observation-owned aggregate at near-ceiling retained-buffer pressure.
Do not qualify/expose a production host based on this WIP. Finish infrastructure
accounting (including the actual host's worker stack bound), then exercise
near-ceiling held consumers across multiple active sources and actual slow HTTP
frame drains. Re-run the focused/full enabled build/install and memory evidence.
The source README and ticket explicitly preserve this limitation. No helper-only
split was made and no missing independent-review-route hole was invented.

Device/source replacement uses exposure withdrawal/requalification, including a
same-Identifier cold reset; identifier transitions are also checked. A genuine
CPU device-replacement/multiple-active-source matrix should be extended in the
remaining qualification work. Current live barney/CUDA, viewer UI composition,
GPU acquisition/drain/responsiveness and physical-monitor parity are **not**
qualified or enabled. Ticket04 still owns the real viewer/GPU adapter. Final
whole-spec independent review is pending with the outer integration orchestrator.
No running user viewer/controller, user Scene/camera/device, config, other
workspace, immutable spec/addenda, sibling/historical ticket or ledger was changed.

## TDD and exact evidence

Initial test build failed on the absent public FrameObservation module.
Deferred request-point encoding failed while work completed synchronously, then
passed with the bounded worker. Narrowed-policy reporting failed with2048/
2073600 hardcoded values, then passed reporting actual4/8 effective limits.
Ignored raw logs: `logs/03-red-build.log`, `03-deferred-red-test.log`,
`03-policy-red-test.log`; these are real failures, not absent-match evidence.

All shells cd to the explicit workspace. Runtime environment:
`LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}"` and
`TMPDIR="$PWD/.scratch/viewport-integration/tmp"`. Full tests were serialized.

1. `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`
   → exit0; warm all-target build/install before the final runtime pass.
   CUDA, networking, Lua, SDL3, MCP, apps and interactive apps remain ON.
   Logs: `03-final-build.log`, `03-final-install.log`.
2. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame]'`
   → exit0, **14 cases /999 assertions** in the focused qualification run.
   Actual native1920x1080 metrics: natural production+submission78.3193ms,
   request safe-point pin0.04202ms (explicit test bound100ms), sampled owned
   reservations157979183 bytes,55 owning-thread heartbeats,82441-byte PNG.
   These are sampled buffer-budget observations, not complete aggregate proof.
   Log: `03-qualified-test.log`.
3. `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`
   → exit0, **68/68 passed**,69.47s. Log: `03-ctest.log`.
4. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests`
   → exit0, **334 cases /119144 assertions**. Native maximum-frame metrics:
   natural production+submission63.0245ms, request pin0.041902ms,
   sampled owned157979183 bytes,53 heartbeats,82441-byte PNG.
   Log: `03-full-integration.log`.
5. `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame]'`
   → exit0, **14 cases /947 assertions**,0 errors,0 definite/indirect lost bytes.
   6136 bytes possibly lost in24 blocks;4274766 bytes in381 blocks in use at
   exit. Total heap usage46571 allocations/46190 frees,487639843 cumulative
   bytes allocated (not a peak or observation-budget figure). ioctl/address-range
   warnings remain disclosed. No GPU or sanitizer qualification is claimed.
   Instrumented natural production+submission7539.99ms, request pin0.891776ms,
  1 heartbeat; instrumentation is not normal-viewing latency evidence.
   Log: `03-valgrind.log`. Assertion counts vary with bounded polling/heartbeats.
6. `clang-format --dry-run --Werror` over all13 changed C++ files
   → exit0. Log: `03-format.log`.

## Exact committed outcome

```json
{"outcome":"ready-for-human","ticket":"03-presented-frame-cpu-path","new_tickets":[],"holes":[],"commit":"oypoyzvpwktrozuumqvmmzwnqnsvmlxo"}
```

## 2026-10-04 continuation — resolved (03 only)

Salvaged the complete preserved/rebased `oypoyzvp` source (72cf186d), rather
than restarting, dropping or prematurely landing it. Source parent is the
03-only metadata resume `wlzpqwsxnunnmszzsttoowspnvsqxovp`, commit
`9ba5a73de4db16089918a14a79d40505873158c5`. The dispatched integration tip is
`d28afd65d8cfd40a06dc868dbb4eb197b76452ae`, reconfirmed as an ancestor of the
worker's parent and unchanged at recording. Spec SHA still matches
`e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
Only03 source, its issue and local proof records changed; no bookmark move,
other workspace, spec/addenda, sibling/historical ticket, config, running user
viewer/controller, user Scene/view/camera/device or source encoder/caps changed.
No new hole, split or ticket; the earlier stop was coupled implementation work.

### Completed ownership and functional qualification

- Replaced the implicit std::thread stack with one explicit2MiB Linux pthread
  stack,64KiB PROT_NONE guard, bounded runtime allowance and join-before-unmap.
  Construction/setup refusal cannot expose a source; other platforms remain
  unsupported/unexposed rather than assume their stack bound.
- Charged budget/lease control blocks, service/TLS/tcache state, per-view source/
  map/tracker bookkeeping, per-job map/executor/Work/future/control state,
  metadata-record lifetime, vector page/header overhead and all retained results.
  Checked counter arithmetic is allocation-exception safe. Binding reservations
  precede observation/serialization; application boundaries safely unwind failed
  copying/encoding/publication without replacement renders or old-latest revival.
- Public visible Status/replay/generic lookup/cancel copies now acquire a finite
  lease before protected payload copies and transfer it to ToolResult/HTTP drain.
  Complete frame-response/RPC-ID bounds are checked before serialization. This
  is an optional ownership hook in the existing engine, not a global map/store
  migration or new authentication/cap mechanism.
- Withdrawal reclaims unheld PNG jobs promptly, while immutable encoding and
  already-published slow socket responses retain leases until safe drain.
- Extended real Scene/Helide/ImagePipeline/SDL and independent decoded-pixel
  tests to two authorized CPU producers sharing near-ceiling pressure, genuine
  same-Identifier device replacement, retained status results, high-entropy
  maximum native PNGs, two real TCP-window-stalled frame responses, a busy third
  response, successful socket drain, cancellation/read withdrawal while a frame
  response is stalled and normal production continues. All prior14 functional
  cases remain and pass; the final focused suite contains20 cases.

### TDD

Fresh build/install preceded each runtime red/green cycle. Infrastructure
accounting failed4 assertions in `03-cont-account-red-test.log` (budget/lease
baseline and worker overhead), then passed. Public retained status accounting
failed2 assertions in `03-cont-status-red-test.log` (uncharged copies/no busy),
then passed. Prompt read-withdrawal PNG reclamation failed1 assertion in
`03-cont-withdraw-red-test.log`, then passed. These are real observed behavior
failures, not absent-match or mock-cache evidence. An initial added multi-source
fixture compile error used a nonexistent Scene removal API (observed in tool
output); it was corrected using pre-activation Scene registration and fixture
lifetime, not a production registry change. The subsequent successful build is
recorded in `03-cont-sources-build.log`; that log is not claimed as red evidence.

### Exact final commands and results

All commands cd to `/home/tarcila/Code/ANARI/vela/viewport-cpu-frame`. Runtime
uses `LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}"` and
`TMPDIR="$PWD/.scratch/viewport-integration/tmp"`; tests are serialized.
The existing warm CUDA/apps/UI/network/Lua/SDL3/MCP-enabled cache is unchanged.

1. `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`
   → exit0, all targets/build/install before runtime. Logs:
   `03-cont-qualified-build.log`, `03-cont-qualified-install.log`.
2. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame]'`
   → exit0, **20 cases/5901 assertions**. Native1920x1080 natural production+
   submission62.0202ms, request pin0.053374ms (test bound100ms),65 owning-thread
   heartbeats and82441-byte PNG. Two-source sampled reservation peak265406189
   bytes; two actual slow HTTP drains held204903359 bytes for8296191-byte PNGs.
   No total-process-memory or normal-viewing percentile claim is inferred.
   Log: `03-cont-qualified-focused.log`.
3. `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`
   → exit0, **68/68 passed**,72.75s. Log: `03-cont-qualified-ctest.log`.
4. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests`
   → exit0, **340 cases/124017 assertions**. Log:
   `03-cont-qualified-integration.log`.
5. `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame]'`
   → exit0, **20 cases/3800 assertions**,**0 errors/0 definite or indirect lost
   bytes**;5304 bytes possibly lost in22 blocks,4268598 reachable bytes and32
   suppressed bytes. ioctl/address-range warnings remain. Instrumented natural
   produce+submit10624.8ms and pin0.914386ms are not normal latency evidence.
   Log: `03-cont-qualified-valgrind.log`.
6. `valgrind --tool=massif --time-unit=B --detailed-freq=1 --max-snapshots=80 --massif-out-file=.scratch/viewport-integration/logs/03-cont-massif.out _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[frame-memory]'`
   → exit0, **6 cases/2718 assertions**. `python .scratch/viewport-integration/03-memory-audit.py`
   →53 detailed samples, whole sampled heap+overhead341415416 bytes;
   conservative remaining heap126861040, plus explicit encoder stack/guard
   **129023728 bytes** after removing only named non-observation pipeline/SDL
   allocations. Sampling is not all-time proof; static ownership bounds are
   documented separately. Logs: `03-cont-qualified-massif.log`,
   `03-cont-massif.out`; tracked audit source/results.
7. `strace -f -e trace=mmap,mprotect,munmap .../vsrIntegrationTests 'CPU observation charges*'`
   → exit0, **1 case/17 assertions**, actual2162688-byte encoder map,
   successful65536-byte guard and matching unmap. Logs:
   `03-cont-stack-test.log`, `03-cont-stack-syscalls.log`.
8. `jj diff --from viewport-integration --name-only | grep -E '\\.(cpp|hpp|h)$'` into
   local file list, then `xargs clang-format --dry-run --Werror` →exit0 on all
   **14 changed C++ files** across the owned03 series. Log: `03-cont-format.log`.

Polling/heartbeat/receive assertion counts vary honestly. No sanitizer, physical
OOM injection, unsupported worker platform, current barney/GPU pixel acquisition,
monitor parity or complete whole-spec feature is claimed. Production remains
default off; the existing viewer is not newly exposed. Final independent whole-
spec review remains the outer orchestrator's post-merge gate, not author review
and not a missing-route hole.

### Exact continuation outcome

```json
{"outcome":"resolved","ticket":"03-presented-frame-cpu-path","new_tickets":[],"holes":[],"commit":"rllnznyrrkusuqoyormompzspppryppu","tests":"20 focused/5901 assertions;340 full/124017;68/68 CTest;Memcheck0 errors/5304 possibly lost bytes","limits":"Linux CPU only; default exposure off; GPU/barney, sanitizers/OOM injection and final whole-spec independent review remain unqualified/pending"}
```


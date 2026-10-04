# Ticket07 — first-observation activation verification

Workspace: `/home/tarcila/Code/ANARI/vela/viewport-first-observation`.
Dispatched base and unchanged integration bookmark: `bae93dc78d6d99118d70a054237ef0db499436eb`; empty worker change `mvtwylnnszqqnqmnlsnwzpwqmrqmspyz`. Source parent confirmed before changes and again after final verification. No reset, bookmark movement, other-workspace mutation or user viewer/controller/configuration writes.

Immutable SHA256s, checked before and after source work:
- spec: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`
- addendum01: `62fae22eb49870782ffe14f9d84acfeb7672741c8e5bf96b1ff79d52323f999b`

## Implementation and public seam

A genuine cold request now owns bounded activation state in its existing Job, not a second request/queue. It retains attachment generation, retirement epoch, Device Identifier/subtype and source-instance generation, and keeps its original admission/policy lifetime. Pump wakes only for readiness, meaningful failure, cancellation/deadline/authority withdrawal or source change. No frame identity is pinned during preparation. At the later owning-thread safe point, latest immutable pixels and their matched receipt are pinned once with actual observation/submission times and rendered/current revisions. Warm exact-latest and retained historical replay remain unchanged.

Authorized Jobs collectively keep a source active, so cancelling one pending activation does not retire another valid observer. Retired activation cannot revive after source/exposure/device change-back. Unsupported profile/acquisition/resource/oversize/binding failures remain terminal and truthful, not cold waits. Normal observer inactivity does not erase meaningful failure reasons.

The user-approved OperationEngine wake/lifecycle seam needed two narrow additions in `src/vsr/integration/operations/Operations.hpp`: a bounded abandonment/deadline query without retained credentials or status polling, and the original Scene safe-point route for backend cleanup after a view closes. No general engine/result-store/refactoring work was performed. Current authority still gates every operation/result; this route cannot follow a replacement view.

Existing public real Scene/Helide/Image Pipeline/SDL and admitted loopback MCP seams are used. No private cache/epoch test interface, simulated producer or forced observation render was added. Tool guidance/README explicitly separate cold preparation from the actual observation point.

## RED and intermediate failures (preserved, not qualification)

- Own build/install, then `vsrIntegrationTests 'First CPU observation waits*'`: exit1, 1 case / 24 assertions / 1 failure at the original terminal cold result (`logs/07-red.log`). Same operation GREEN after the fix: exit0, 1 case / 69 assertions (`logs/07-green.log`). The final test additionally proves later successful presentations may precede the first actual pin, actual observation timing, matched current revisions/staleness and independent decoded PNG pixels.
- Initial frame/control runs exposed failure-reason retirement and source-lifetime handling; fixed without turning unsupported output/profile failures into waiting. Logs `07-frame-pass1.log`, `07-controls.log` retain failures. An unmatched filter in the controls command matched no MCP case; it is NOT counted as MCP evidence. Final matching focused/full suites include the MCP scenario.
- First live attempts asserted success for the fixture's initial incomplete display binding, which correctly returns `inactive_binding`, not cold. The fixture still issues/preserves that negative-control request and now asserts success for its first actually qualified request, observed through the public binding seam. Production first-operation evidence below remains stricter: real prior successful displays, and the first admitted frame operation succeeds with PNG.
- Initial targeted Memcheck exited99 with 18 invalid-read contexts in the CPU fixture's live binding after it freed Image Pipeline storage while its old bootstrap request now legitimately remained pending. The warm teardown fixture now completes that unrelated cold operation before its warm encoder-cancellation test. A separate cold cancellation/free-storage section proves cancelled preparation calls no freed binding. Final Memcheck below is zero-error; the first failure remains in `logs/07-memcheck.log`.
- Initial entropy live fixture failed the unchanged flipped-image oracle ratio because it compared admission-time SDL pixels with a later correctly pinned cold frame. The independent diagnostic oracle now keeps at most16 SDL surfaces per view, associated with the real successful submission identity; it never reads VSR snapshots/encoder bytes. Exact matching is REQUIRED, not skipped. Pixel/orientation thresholds remain unchanged. `04-07-entropy-pressure-probe-barney-viewer.log` retains the failure; exact-oracle final passes below. Diagnostic SDL surfaces are fixture-owned memory, not observation-bank/RSS claims.

## Commands and final results

Every shell starts by cd to the owned workspace. Before each runtime verification group:
`cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo`
Both exit0 using the existing warm tree and own install prefix. Runtime environment: `LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}" TMPDIR="$PWD/.scratch/viewport-integration/tmp"`. `CMAKE_HOME_DIRECTORY` and `CMAKE_INSTALL_PREFIX` positively name this workspace; CUDA, interactive UI, networking, Lua, SDL3 and integration/MCP remain enabled. No reconfiguration, shared mutable output, feature disable, backend fallback or executable-debug configuration change.

Final serialized regression after the expiry-pruning guard (logs `07-pruning-final-{build,install}.log`, `07-ctest-final2.log`, `07-integration-final2.log`, `07-focused-final2.log`):
- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`: exit0, **68/68**, 73.08s.
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests`: exit0, **370 cases / 129547 assertions**.
- Same executable `'[request-guidance],[view-info],[view-identity],[presented-frame],[viewer-presentation],[translation-preview]'`: exit0, **53 cases / 9789 assertions**. Counts include actual bounded pump/poll iterations and can vary with worker timing.
- `clang-format --dry-run --Werror` on all six changed C++ files: exit0 (`07-format-final2.log`). Canonical clang-tools21 formatter used.

Final targeted owned CPU memory, after own build/install (`07-pruning-final-{build,install}.log`):
`valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation],[first-observation]'`
→ exit0, **30 cases / 5458 assertions, 0 errors, 0 definite/indirect lost bytes** (`07-memcheck-final2.log`). Possible loss26112B/178blocks and reachable23475945B/18905blocks are disclosed, not claimed clean. Unhandled driver ioctl/address-range warnings remain. No sanitizer/GUI/GPU/whole-RSS memory claim.

Public safety coverage includes rejected display submission, real natural first same-operation PNG/independent SDL pixels, warm exact pin across later presentation/resize, historical replay/current authority, pending sleep/stable bank/no rendering, cancellation with/without freed storage, original120s deadline and narrowed2s policy lifetime, read/session withdrawal, source attachment/instance/device replacement, exposure change-back, view closure and original-route cleanup, 32 pending operation slots/busy and surviving observer after31 cancellations, oversize SAME cold operation, existing64-job retention/expiry, two-source near-ceiling ownership, slow real HTTP drains and immutable encoder teardown.

## Actual production first operations

Diagnostic helpers were copied from the provided parent diagnosis into worker scratch before adaptation. `07-production-viewer.py` uses Node24.21.0 and `07-production-client.mjs` uses the current Pi1.0.2 native MCP tool/runtime path. Original qualification assertions remain; new strict assertions require the FIRST frame operation available, an image on that same operation, zero cold retries, and all32 PNG comparisons. No second frame request is used to recover cold.

After final source build/install:
`VSR_VIEWER_TEST_LOG_LABEL=07-drain-final python3 .scratch/viewport-integration/07-production-viewer.py DEVICE`

| Device | Exit | Prior successful SDL submissions | First frame | PNG / cold | Independent sampled pixels | Max mean RGB error | Busy retries |
|---|---|---|---|---|---|---|---|
| barney | 0 | 15 | available, presentation19 | 32 / 0 | 93568 | 1.301233 | 0 |
| helide | 0 | 15 | available, presentation19 | 32 / 0 | 93568 | 2.644558 | 0 |

First Barney submission/observation:18263.852938100 /18263.853050299; Helide:18271.193893793 /18271.194025236. These are actual later observation points, not admission-time stamps. Each run verifies actual Device Identifier, production/presentation identity, real submission timing, matched rendered/current metadata, orientation/color, independent black/flipped controls, resize and identical historical PNG replay. Total final production **64 PNGs / 0 cold retries /187136 independent sampled pixels**. Initial pre-final production runs also passed32/0 each, but are not added to final counts.

Tracked final proofs: `04-07-drain-final-{barney,helide}-first-observation-proof.json`. Earlier07/07-final-production proofs are preserved and tracked, not counted again. Isolated authenticated Xvfb/XDG fixtures and separate MCP controllers only. The historical parent diagnosis/proof (15 prior displays, first unavailable/cold,31 later PNGs) is read by pointer, not duplicated. No user source/Scene/device/camera writes or fallback; bounds/policy survey observations remain unavailable as before. No claim that32 requests measure32 progressive samples.

## Real SDL viewer lifecycle/pressure qualification

All assertions and original pixel thresholds retained. These live lifecycle/pressure runs precede the final one-condition expiry-pruning guard; the post-guard enabled full/CPU-memory suites and actual production runs above were rerun. The guard affects expired pending cleanup, not these successful-source/pressure paths. Runner: `VSR_VIEWER_TEST_LOG_LABEL=07-exact-final python3 .scratch/viewport-integration/04-live-viewer.py DEVICE probe`; pressure additionally uses `VSR_VIEWER_TEST_DESTRUCTIVE=1`, entropy additionally `VSR_VIEWER_TEST_ENTROPY=1` with label07-oracle. All dedicated fixture instances, no user viewer.

| Run | Exit | Cases/assertions | PNG / generic unavailable | Heartbeats / primary submissions | Independent compared pixels | Peak owned bytes |
|---|---|---|---|---|---|---|
| barney probe | 0 | 1 /966 | 47 /1 |124 /112 |113412 |24891347 |
| helide probe | 0 | 1 /966 | 47 /1 |115 /112 |113412 |24891347 |
| barney two-source pressure | 0 | 1 /1376 |35 /13 |133 /122 |1083873 |264030843 |
| barney entropy/drain pressure | 0 |1 /1234 |25 /23 |124 /112 |251573 |183099090 |

The fixture's historical `cold` counter means ALL unavailable results, including the deliberately incomplete initial binding and bounded pressure failures; it does NOT mean cold-cache retries. Its first qualified operation succeeds in every final run. Pressure secondary generation2 continues107 presentations after primary generation2→3 retirement (entropy97); off-owner starts/mismatched acquisitions0. Entropy positively records both stalled HTTP responses/drain release,6178906B maximum PNG and192980988B drain-held bank. Real camera/renderer/Outline/resize/device-instance retirement, cancellation/read suppression, surviving secondary source and natural rendering remain asserted. Exact SDL identity matching repairs the cold safe-point oracle, not the encoder/profile/thresholds.

## Final expiry-pruning RED/GREEN

Final author audit found that `submit()` could prune an expired activating Job before pump delivered its cleanup wake; unlike encoding, preparation has no worker to deliver a later wake. A public re-admission-after-narrowed-expiry section went RED: exit3,1case/857assertions/3failures on terminal/held-slot outcomes (`07-pruning-red.log`), after own build/install. Pruning now retains activating Jobs until pump captures/drains the wake. The original deadline/capacity is not extended. Final full/focused and30-case zero-error Memcheck above all include this regression; both actual production first-operation runs were repeated against the post-guard installed tree.

At the55-minute assessment all functional/test work was green; only final proof recording/tracking/commit remained and fit the60-minute working limit. No source/ticket WIP or holes are concealed.

## Limits and final gate

Supported normal unit-scale SDL_CPU_READY_V1 HOST/managed-host upload path only. Unknown/GPU-only interop/output/upload/non-unit-scale paths remain unavailable. No new universal GPU, GPU sanitizer, CUDA readback, quality/sample-count, physical-monitor/UI-overlay parity or RSS/VRAM claim. Prior external GUI-driver/Compute Sanitizer limitations remain unresolved and were not reclassified as passes. Current52-era pipeline/device/camera behavior is preserved.

Unchanged limits:2048axis,2073600pixels,8MiBPNG,12MiBresponse,16MiBproxy,256MiBshared owned bank,64retained frame Jobs,32scene operation slots,120s maximum admission/source lifetime,one encoder. No bounds/general editing/image attachment/policy/encoder/backend/global-store work; no spec/addenda/sibling ticket/ledger/executor/config changes, new holes, subdelegation, rawgit or remote publication.

Ticket07 functional criteria are verified. **Final whole-spec independent Standards/Spec review, integration merge and outer qualification/fixes remain pending with the orchestrator.** Author inspection is not independent review; this ticket does not declare the full integration branch complete.

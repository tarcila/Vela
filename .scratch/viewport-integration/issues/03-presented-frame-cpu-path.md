# 03: Presented-frame CPU observation through MCP

**What to build:** A qualified CPU presentation source supplies its exact request-bound presented image and provenance through admitted frame observation and image retrieval without a replacement render.
**Blocked by:** 02
**Status:** resolved
**Milestone:** 1
**Specification:** [Truthful Views, Presented Frames and Safe Selection Translation](../spec.md), stories23–58; decisions5–12.

## Current behavior

ImagePipeline exposes mutable final color-buffer storage, not an owned presented-frame observation. The viewer display path and native/display scale are separate facts. Legacy agent rendering is not presented-frame retrieval.02 provides truthful registered binding/policy/authority metadata on which this slice depends.

## Acceptance criteria

- [x] Deliver a deep Frame Observation module and real CPU ImagePipeline presentation adapter through the public headless interface and admitted vsr_view_frame/vsr_view_frame_image. Producer owns source/presentation state; consumers receive immutable bounded snapshots, not live UI/Scene/buffer pointers. Keep rendering storage independent of integration wire types.
- [x] Associate pixels with actual successful display submission. Pin the most recently presented frame at the request's owning-thread point, return that exact frame or truthful pending/unavailable, never an older completed copy labeled latest. Record frame identity/timing, observed presentation identity, rendered versus current revisions, Device Identifier, bound camera/renderer and native/display dimensions/scale/color/effects.
- [x] Include display-ready pipeline output and in-pipeline Outlines, exclude UI chrome/HUD/gizmos/desktop. Preserve orientation, alpha and color conversion without double transforms. A displayed frame older than current Scene state is returned explicitly stale; replay remains historical.
- [x] No render, reset, device creation/fallback or main accumulation/viewport change to fetch an image. Host exposure starts false; only qualified source plus authorized observer enables production. Cold cache/inactive presentation/no source/unsupported role/oversize states are explicit and never imply empty geometry.
- [x] Enforce current Scene/view read authority at admission/work/publication/retrieval, including retained refs and withdrawal. No private descriptor/authentication changes. Cancellation/expiry/teardown safely release owned work; closed/recreated/device-changed sources cannot revive latest identities.
- [x] Checked limits retain2048axis/2073600pixels/8MiBPNG/12MiBresponse/16MiBproxy and finite owned host/device budgets at most existing256MiB ceilings. Account for staging, cache, held old snapshots, PNG/base64/results and aggregate views, not just slot count. Exhaustion is bounded/truthful, without hidden scale/clamp or blocking live rendering.
- [x] Public real-pipeline CPU tests exercise produced-versus-presented negative controls, request-point identity/delayed copying, stale/current pairing, resize/scale, role selection, cold activation, permission withdrawal, close/recreate, replay/cancel, slow consumers, resource arithmetic/retention and independent decoded image patterns. No separate approximate simulator.
- [x] Build/install enabled configuration, focused/full applicable regressions, formatting and CPU snapshot/lifetime memory verification pass with actual proof. This CPU slice does **not** qualify the current live barney/CUDA source; unsupported GPU-only data stays unavailable until04.

## Out of scope

GPU viewer exposure/qualification04, fresh camera-capture08, progressive-quality/samples, user screenshot/desktop access, hidden fallback/rescale, translation, global result-store refactor or cap changes.

## Comments

Complete CPU host-to-MCP vertical behavior, not a producer-hook-only phase. Depends on02 for actual source identities/provenance/policy.04 must validate real viewer/GPU paths before the livebarney goal is claimed. Author records automated evidence; integration orchestrator supplies independent final review.

### 2026-10-04 iteration — ready-for-human
Implemented the CPU host-to-MCP vertical behavior: rendering-local immutable snapshots, actual successful SDL3 presentation association, matched pre-production provenance, request-point pins, deferred CPU encoding, admitted frame/image tools, current authority, replay/cancel/expiry/resize/retirement and bounded buffer/result/drain reservations. Tests use real Scene/RenderIndex/Helide production and actual ImagePipeline/SDL submission, with an independently decoded pixel oracle and loopback MCP. No existing viewer source is enabled; exposure remains default false.
Stop assessment found a load-bearing remaining qualification invariant: fixed encoder-thread stack and service/map/future bookkeeping are not separately charged to the shared aggregate host budget. The reservation counter and passing Valgrind do not prove all observation-owned resources stay within256MiB at near-ceiling retention. Keep this coupled ownership work in03; finish that accounting and extend multiple-active-source, genuine CPU device replacement and actual slow HTTP frame-drain pressure before resolving/exposing production. This is implementation WIP, not a spec hole or helper-only split.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` → exit0, enabled warm build/install before runtime, own installed libraries first/local TMPDIR. `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` → 68/68 passed,69.47s; full `vsrIntegrationTests` → 334 cases/119144 assertions. Focused `[presented-frame]` → 14 cases/999 assertions; max-native1920x1080 request pin0.04202ms, sampled reservations157979183 bytes,55 heartbeats,82441-byte PNG. `clang-format --dry-run --Werror` on13 changed C++ files → exit0.
Verified: `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect .../vsrIntegrationTests '[presented-frame]'` → exit0,14 cases/947 assertions,0 errors and0 definite/indirect lost bytes;6136 bytes possibly lost plus ioctl/address-range warnings. Counts vary with bounded polling. No sanitizer/GPU/current-barney qualification, complete aggregate-memory proof, or whole-spec completion is claimed. Detailed commands, counts, limitations and exact outcome JSON: [03-verification.md](../03-verification.md). Raw logs are local/ignored.
Source parent/integration bookmark were reconfirmed at dispatched `df45dba361dcedc6cf3cab20b6547f868d0bc3f8`; bookmark unchanged for the serial merger. Spec hash unchanged. Final whole-spec independent review remains outer-orchestrator-owned after merges, not a missing-review-route hole. No user's running viewer/controller/config/Scene/camera/device or another workspace/ticket/spec/ledger was changed.
Holes filed: none. New tickets: none. Retry WIP: none.

### Orchestrator continuation — same approved implementation
The recorded stop is unfinished implementation/accounting within the approved03scope, not a new product decision, inaccessible permission or Spec hole. Resume in a fresh long context using this preserved source and warm tree. Complete owned thread-stack/service/future/table reservations and multi-source/device-replacement/actual slow HTTP-drain coverage before exposing or resolving. Keep all prior guarantees and disabled production state; no helper-only split or memory-proof waiver. The earlier JSON/status records remain historical; currentStatusready-for-agent reflects this explicit continuation.

```json
{"outcome":"ready-for-human","ticket":"03-presented-frame-cpu-path","new_tickets":[],"holes":[],"commit":"oypoyzvpwktrozuumqvmmzwnqnsvmlxo"}
```

### 2026-10-04 continuation — resolved
Salvaged the full preserved/rebased03 source and completed the coupled ownership invariant: explicitly mapped/guarded/joined encoder stack, service/view/job/map/future/control/allocator overhead, checked exception-safe counters, retained record/status/replay ownership and actual HTTP drain leases. Read withdrawal promptly reclaims unheld PNGs; no fetch changes Scene, view, camera, renderer, device, dimensions or accumulation. Production remains default off; Linux synchronous CPU qualification is not viewer/barney/GPU qualification or whole-spec completion.
Real Scene/Helide/ImagePipeline/SDL public tests retain all prior14 cases and extend to20, including two authorized CPU sources near256MiB, genuine same-Identifier CPU device replacement, high-entropy maximum-native PNGs, actual TCP-window-stalled frame responses/exhaustion/drain, and cancellation/read withdrawal while natural viewing continues. Full allocation/lifetime proof plus independent sampled heap and actual stack syscalls: [03-memory-proof.md](../03-memory-proof.md); commands/counts/limits/outcome: [03-verification.md](../03-verification.md).
Verified: `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` →exit0 on own warm enabled configuration before runtime. Own installed libraries first/local TMPDIR; serialized `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` →68/68 passed,72.75s; focused `vsrIntegrationTests '[presented-frame]'` →20 cases/5901 assertions; full executable →340 cases/124017 assertions; `clang-format --dry-run --Werror` on all14 changed C++ files →exit0. Maximum native1920x1080 normal request pin0.053374ms,65 heartbeats; two-source sampled reserved peak265406189 bytes below268435456. Actual slow HTTP drains held204903359 bytes for8296191-byte PNGs; third retrieval busy and successful drains release leases.
Verified: Memcheck full focused20 cases/3800 assertions →exit0,0 errors/0 definite or indirect lost bytes;5304 bytes possibly lost,32 suppressed bytes and ioctl/address-range warnings disclosed. Massif6 cases/2718 assertions →exit0; sampled whole-process heap341415416 bytes includes excluded Scene/render/display/client memory. Conservative sampled remaining heap plus explicit encoder mapping129023728 bytes supplements, not replaces, static bounds. strace confirms2162688-byte map,65536-byte PROT_NONE guard and matching unmap. No sanitizer/GPU/current-barney/physical-OOM-injection or process-RSS ceiling claim.
Source parent reconfirmed `wlzpqwsx`/`9ba5a73de4db16089918a14a79d40505873158c5`, descended from the unchanged dispatched integration tip `d28afd65d8cfd40a06dc868dbb4eb197b76452ae`. Immutable spec SHA unchanged. One continuation code-plus-ticket commit; no bookmark move/merge, source encoder/caps, user config/viewer/controller/Scene, other workspace or sibling/historical ticket/spec/ledger edits. Final whole-spec independent review remains outer-orchestrator-owned after merges, explicitly pending.
Holes filed: none. New tickets: none. Retry WIP: salvaged `oypoyzvp`/`72cf186d` in full.

```json
{"outcome":"resolved","ticket":"03-presented-frame-cpu-path","new_tickets":[],"holes":[],"commit":"rllnznyrrkusuqoyormompzspppryppu","tests":"20 focused/5901 assertions;340 full/124017;68/68 CTest;Memcheck0 errors/5304 possibly lost bytes","limits":"Linux CPU only; default exposure off; GPU/barney, sanitizers/OOM injection and final whole-spec independent review remain unqualified/pending"}
```


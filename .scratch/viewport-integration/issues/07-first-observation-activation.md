# 07 — Complete the first displayed-frame observation after cold activation

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/frame/FrameObservation.hpp, src/vsr/integration/mcp/tools/FrameTools.hpp, src/vsr/integration/frame/README.md, src/tests/integration/test_IntegrationPresentedFrame.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationViewerLive.cpp
**Specification:** ../spec.md plus ../addenda/01-first-observation-activation.md.

## Current behavior

At source base `52e7e34b6e624e13711c39399001ffcf7d31e9de`, production barney had15successful SDL submissions before its first admitted frame request. That operation terminated unavailable/cold_cache;31later requests produced independently compared PNGs. Diagnosis and reproducible scripts are in ../../session-friction/findings.md, run-production-probe.py and production-client.mjs; actual proof ../04-friction-barney-session-probe-proof.json. Existing public MCP test explicitly expects cold instead of proving the desired first-request behavior.

FrameService::active retires cache/receipts while no observer exists. FrameService::observe installs an observer then immediately returns unavailable on the empty cache. A later natural presentation creates the first owned snapshot. The original immutable spec allows cold activation to await natural presentation; the additive contract makes preparation and the actual observation/pin point explicit.

## Desired behavior

- [x] First admitted request on an authorized exposed qualified source stays pending during genuine cold preparation and completes available after a natural successful presentation, without issuing a second frame request or forcing rendering.
- [x] Pin latest exactly once at the actual owning-thread observation safe point after preparation; report real submission/observation identity/times and matched rendered/current revisions, never admission-time fiction. Warm and already pinned requests keep existing exact/historical/replay semantics.
- [x] A bounded pending-activation state wakes through host/presentation pumping without busy-spin, unbounded queues, uncharged callbacks or encoder/wake races. Multiple pending requests and source observers remain truthful and within existing capacity/budgets.
- [x] Preserve meaningful unsupported/oversize/acquisition/resource/binding failures. Do not turn every missing/failed latest snapshot into an indefinite wait or silently fetch another identity.
- [x] Cancellation, permission/session withdrawal, deadline/no presentation, source unexposure/expiry, view close/recreate and source-instance/device replacement safely finish/retire pending activation without reviving the old observer or migrating to a replacement view/source.
- [x] TDD at approved public real-Scene/SDL FrameService and admitted MCP seams: observed first-call RED before production fix; same-operation first PNG success and independent pixels; no-presentation timeout, cancellation/read withdrawal, warm pin/replay, source replacement and near-capacity cleanup controls.
- [x] Build/install enabled own tree before runtime; actual production barney AND Helide first request succeeds, with real prior display submissions and independent SDL pixel comparison. No extra scene edits/render requests or unqualified-path exposure.
- [x] Focused and full integration/CTest, formatting and targeted owned CPU Memcheck pass with actual counts; applicable CUDA/UI/network/Lua/SDL3 stay enabled. Report external driver/tool limitations honestly.
- [x] Record verification and one source-plus-ticket commit; independent review/landing supplied by outer orchestrator.

## Out of scope

Bounds, generic editing, image file delivery, capture-policy redesign, new capture quality/resolution contracts, caps/PNG encoder changes, generic OperationEngine/result-store/global map refactors, device manager/backend redesign, immutable spec or earlier ticket changes. Preserve user `.envrc`, `.pi/mcp.json`, `.pi/skills/vsr-viewer-agent/*`, queen-pov image and unrelated workspaces. Only isolated runtime instances are needed despite permission to replace the off viewer. No remote publication/raw git/subdelegation.

## Comments

User ordered first observation → bounds → generic editing → image attachments. This ticket is the first stage only. Existing public seams were approved during prior qualification and used for diagnosis; no new private/mock seam should replace them. Task-specific scoped executor retains60-minute working/75-minute hard budget. A planning delegate was rate-limited; no source work or missing answer is attributed to it.

### 2026-10-05 iteration — resolved
Implemented bounded same-operation cold preparation, source-generation/epoch/instance retention, collective authorized observers and event-driven host pumping. The actual later safe point pins latest once with real identities/times/revisions; warm/historical replay and meaningful acquisition/profile/resource failures remain intact. Minimal changes at the explicitly dispatched OperationEngine wake/lifecycle seam add abandonment/deadline querying and original-Scene cleanup routing after view closure, not a general engine refactor.
Public real Scene/SDL regression went RED on terminal cold (1case/24assertions/1failure), then GREEN; admitted MCP now delivers request2's first PNG without request3. New controls cover cancellation/free storage, deadline/narrowed lifetime, read/session withdrawal, close/reattach/instance/device/exposure change-back and32pending slots with one surviving observer. Existing retention/encoder/HTTP/qualification assertions remain. Initial failed controls, fixture-lifetime Memcheck and admission-versus-observation SDL oracle failures are retained and explained in `../07-verification.md`; the oracle now requires exact successful-submission matching with a finite diagnostic ring and unchanged pixel thresholds.
Verified: own enabled `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo` before runtime, own installed libraries/TMPDIR first; `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` →68/68; `vsrIntegrationTests` →370cases/129547assertions; named focused suite →53cases/9789assertions; targeted owned CPU Valgrind →30cases/5458assertions,0errors,0definite/indirect loss (26112B possible/178blocks, external ioctl/address warnings disclosed); clang-format dry-run →exit0 on six C++files.
Verified: isolated actual production Barney AND Helide →first admitted frame available/PNG after15prior successful SDL submissions each,32PNGs/0cold retries each,187136total independent sampled pixels, identical historical replay, resize and matched provenance. Final real SDL viewer probes →Barney1/966,Helide1/966; two-source pressure1/1376; entropy/HTTP-drain pressure1/1234; all exit0 with first QUALIFIED operation available. The fixture deliberately preserves initial inactive-binding/pressure failures; its legacy `cold` counter aggregates ALL unavailable results, not cold-cache retries. Exact commands/counts/proofs/limits and intermediate failures: `../07-verification.md` and final production proof JSONs.
No force-render/reset/camera/device/GPUfallback/user-controller/configuration writes. Limits/profile/encoder/budgets unchanged; GPU-only/unknown/non-unit SDL scale remain unsupported, and no universal GPU/sanitizer/quality/monitor/RSS claim is made. Spec/addendum SHA and source parent/bookmark were checked unchanged. Final whole-spec independent Standards/Spec review, merger/outer qualification and any fixes remain pending; this is ticket07 completion only.
Final audit also reproduced expiry-pruning losing a pending activation's cleanup wake (RED1case/857assertions/3failures). Activating Jobs now remain until pump captures/drains the wake; final full/focused/Memcheck suites and both actual production32/0first-operation runs were rerun against the post-guard installed tree. Live lifecycle/pressure runs precede that guard; their successful-source paths are unchanged, not represented as a fresh GPU-instrumentation pass. At55minutes all functional/test work was green and only record/track/commit remained within60minutes.
Commit: mvtwylnnszqqnqmnlsnwzpwqmrqmspyz (one verified code-plus-ticket commit under dispatch override)
Holes filed: none
Outcome: `../07-outcome.json` — resolved, no new tickets/holes.

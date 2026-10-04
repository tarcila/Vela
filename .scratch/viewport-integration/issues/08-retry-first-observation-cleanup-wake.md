# 08 — Retain first-observation cleanup retries under queue pressure

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/frame/FrameObservation.hpp, src/tests/integration/test_IntegrationPresentedFrame.cpp
**Specification:** ../spec.md and ../addenda/01-first-observation-activation.md. Required review correction for ticket07; bounds/edit/image stages remain later.

## Current behavior

Ticket07 source change `mvtwylnnszqq` completes the first qualified frame request after bounded natural presentation. GPT-only independent review report `581d17f8-c921-4b19-92ae-8a23c9227ad4` raised one high-severity correctness candidate. Parent manually verified the quoted source and the Integration queue-rejection path; no reviewer runtime rerun is claimed. Review interpretation: ../../session-friction/07-gpt-review.md.

FrameService::pump collects a wake for an expired/authority-withdrawn activating Job, then erases that Job from m_jobs before invoking the wake. If OperationEngine::dispatch cannot submit cleanup because Integration's bounded queue is full, BACKEND resources and the slot intentionally remain until another wake. Cold preparation has no encoder worker to provide one, and subsequent pumps no longer find the erased Job. The retained executor owns memory but does not schedule cleanup. The submit-side activating pruning guard does not cover this pump-side loss.

## Desired behavior

- [x] First write a real public real-Scene/FrameService/OperationEngine/Integration regression that puts a cold request into backend-pending, triggers expiry or authority withdrawal, forces actual Integration::submit cleanup rejection with bounded queue pressure, then drains pressure and proves subsequent pump safely finishes cleanup and releases its operation slot/reservations. Observe RED before production correction; no private-field/mocked dispatch test.
- [x] Retain a bounded retry source until cold cleanup actually progresses, without duplicate execution, selfwake spin, unbounded queues/callbacks, indefinitely retained completed Jobs or uncharged metadata. Successful dispatch is not itself completed cleanup; reason about ownership until executor clears preparation state.
- [x] Preserve cancellation, session/read withdrawal, deadlines, source/view teardown, no observer revival, same-operation first natural PNG, warm exact pin/history, expiry/re-admission controls and the original single-encoder/shared budget/capacity limits.
- [x] Keep correction local to FrameService job/wake lifecycle unless a load-bearing contract hole is proved. Do not change global Integration queue semantics or widen OperationEngine/result-store scope just to avoid enqueue failures.
- [x] Build/install own enabled warm tree; focused queue-pressure/first-observation/MCP/frame and full integration/CTest tests, targeted CPU Memcheck and format pass with actual counts. Production barney/Helide first-operation proof must remain valid; rerun affected acquisition/lifecycle paths rather than claiming old evidence covers changed source.
- [x] Record actual RED/GREEN and verification, then one code-plus-ticket commit. GPT-only corrected-artifact review and independent runtime acceptance remain outer gates, not author approval.

## Out of scope

Bounds, editing, image delivery, capture policy redesign, cap/encoder/device/backend/global-store refactors, frozen spec/addendum or older ticket mutation, user .envrc/.pi configs/skills/queen image/live session and unrelated workspaces. No raw git/push/remote publication/subdelegation/bookmark moves.

## Comments

User authorized GPT-based independent review. Single responder is not panel consensus; the panel pipeline warned below-quorum/invalid citation. Parent located the exact quoted condition in complete source, inspected OperationEngine::dispatch retaining BACKEND resources on failure and Integration::submit explicitly rejecting queue saturation. This is a falsifiable required correction, not an optional smell. Use existing qualified public test seams and the established scoped60-minute working/75-minute hard executor budget. Keep ticket07 source and proof intact as the prerequisite stack; don't restart/drop it.

### 2026-10-05 iteration — resolved
Reproduced the positively source-verified GPT candidate through public Integration/real Scene/FrameService/OperationEngine: full bounded queue, actual submit false, cold backend preparation, failed cleanup wake, pressure drain and later pump. RED:1case/4357assertions/10failures across expiry/read/session withdrawal (cancellation already correctly retried). No mock/private fields. Local pump-only pruning guard retains charged activating Jobs until observe clears preparation, not merely until wake capture/enqueue; existing engine dedup prevents duplicate queued cleanup.
Final public regressions:2cases/6418assertions include original120s and narrowed2s expiry, read/session withdrawal/cancel, slots held until actual executor cleanup, zero selfwake/render/observer revival, baseline lease reclamation and70sequential cancellations exceeding64Job capacity without retained completed storage. All07 source/proofs retained; original6bc→500src diff empty; expected owned500parent ancestry from unchanged992integration bookmark checked. Spec/addendum/envrc hashes unchanged; worker.pi/mcp.json absent and untouched.
Verified: own enabled warm `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo` →exit0, own installed libs first/absolute localTMPDIR. Serialized `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` →68/68; full `vsrIntegrationTests` →372cases/135957assertions; matching focused `'[presented-frame],[viewer-presentation],[first-observation],[mcp]'` →32cases/13982assertions; targeted CPU Valgrind →32cases/11903assertions,0errors/0definite-indirect loss,29136Bpossible/185blocks and external ioctl/address warnings disclosed; clang-format dry-run →exit0 on both changedC++files.
Verified: fresh isolated production Barney AND Helide first admitted operation available/PNG after18/12prior SDL submissions respectively,32PNGs/0cold retries each,187136total independent sampled pixels, matched provenance/times/resize/historical replay. Fresh real SDL lifecycle Barney1/957,Helide1/966; actual two-source pressure1/1363; entropy/realHTTPdrain1/1234; all exit0, exact unchanged SDL oracle. Early unmatched operations filter and ordinary probes mistakenly not enabling pressure are explicitly not counted; final correctly matched commands supersede them. Detailed exact commands/logs/counts/remaining limits: `../08-verification.md`; new native proofs `../04-08-final-{barney,helide}-first-observation-proof.json`.
No global queue/engine/results/caps/profile redesign or unsupported/GPU blanket qualification. No source/spec/07ticket/ledger/executor/controller/userconfig/otherworkspace mutations. No new holes; final outcome below recorded in the same verified code-plus-ticket commit under dispatch override. Completion before55-minute assessment with no budget extension. Outer corrected frozen GPT-only review, independent runtime acceptance, integration landing/fixes and final whole-spec review remain pending, not a missing-review-route hole or author independent approval. Full integration/bounds/edit/image stages are not declared complete.
Commit: mwtsorrxxvnkynkpoxnzyvlrkyrvukuo
Holes filed: none
Outcome: `../08-outcome.json` — resolved, no new tickets/holes.

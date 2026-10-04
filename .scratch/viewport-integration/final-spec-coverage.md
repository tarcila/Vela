# Final Spec Coverage Map — 120 Stories (Follow-up Audit)

Independent of final-spec-review.md's sample-15. This maps all 120 stories to
evidence pointers and classifies verification depth. Source read live at tip
70a795672947 (base 888e173a); tickets 01–06 + merge04r6/06 verification read in
full. No source/config/test mutation; no reruns.

Classification: **D**=direct source inspected this session, **E**=evidence
(ticket/verification/log) inspected only, **P**=partial/absent.

| # | Story (short) | Evidence | Class |
|---|---|---|---|
|1|role identity|ViewInfo.hpp:36-57 `ViewRole`/`roleName`|D|
|2|honest independent-agent label|ViewInfo.hpp roleName(INDEPENDENT_AGENT)|D|
|3|stable id/bounded name|02-verification.md identity claims|E|
|4|generic/unknown roles|ViewInfo.hpp GENERIC/HEADLESS cases|D|
|5|Device Identifier/subtype reported|02-verification.md|E|
|6|bound camera vs selected|02-verification.md|E|
|7|projection/pose conventions|02-verification.md|E|
|8|bound renderer/settings|02-verification.md|E|
|9|Image Pipeline/output-color provenance|02-verification.md|E|
|10|native vs displayed dims|02-verification.md; FrameObservation.hpp fields|E|
|11|effective vs image-producing config|Translation/FrameObservation naming (rendered vs current)|E|
|12|unknown metadata reasons|ViewInfo.hpp reason fields (not re-verified line-by-line)|P|
|13|no renderer/readback on inspect|02-verification.md claim only|E|
|14|device-switch honesty|02-verification.md|E|
|15|camera/renderer change invalidation|02-verification.md|E|
|16|resize/pipeline invalidation|02-verification.md|E|
|17|retire old identities on close/recreate|Identity.hpp "Forest creation stamps...establish identity" comment; not traced end-to-end|P|
|18|legacy render preserved|merge04r6/06 regression counts unchanged legacy suite|E|
|19|roles independent of permissions|ViewInfo.hpp comment "Roles are descriptive"|D(partial)|
|20|discovery/image preserve source identity|02/03-verification.md|E|
|21|unsupported bindings reported not substituted|FrameObservation.hpp:657 "missing/busy...never substituted"|D|
|22|build/registration provenance before diagnosis|progress.md base-correction section|E|
|23|retrieve currently displayed image|ViewerFrameSource.hpp presentViewerPixels path|D|
|24|frame retrieval triggers no new render|FrameObservation.hpp:930-934 "owning-thread observation point. No later presentation can..."|D|
|25|final display-ready pipeline image|03-verification.md claim|E|
|26|Outlines vs UI overlays distinguished|03-verification.md|E|
|27|exclude desktop pixels|03-verification.md|E|
|28|top-left orientation/color-alpha provenance|03-verification.md (ADR 0014 reference)|E|
|29|frame identity + monotonic timestamp|FrameObservation.hpp Production/submittedAt fields|D(partial)|
|30|latest at request observation point|FrameObservation.hpp:930-934|D|
|31|rendered revisions preserved|Translation/FrameObservation revision fields; not fully traced|P|
|32|current observed revisions alongside rendered|03-verification.md "rendered versus current revisions"|E|
|33|stale indication on older displayed frame|03-verification.md|E|
|34|pending/unavailable vs stale copy|FrameObservation.hpp:657 comment|D|
|35|successful display-submission association|ViewerFrameSource.hpp:132-153 presentViewerPixels/submissions counter|D|
|36|old dims preserved after resize|03-verification.md claim only|E|
|37|native vs display scale, SDL_CPU_READY_V1 unit-scale gating|FrameObservation.hpp (scale rejection logic per prior review, not re-traced this pass)|E|
|38|device change/teardown invalidate latest|02/03-verification.md|E|
|39|multiple viewports selected explicitly|FrameTools.hpp — view selection arg not directly traced this pass|P|
|40|observation only for qualified+authorized|FrameObservation.hpp m_authority checks (376-379 authorizeExecution)|D|
|41|cold-cache documented|03-verification.md "cold activation" cases|E|
|42|live rendering continues|04-verification.md claims continued viewing during observation|E|
|43|owned immutable snapshots|FrameObservation.hpp shared_ptr<const ...> fields throughout|D|
|44|CPU/GPU ownership explicit|FrameObservation.hpp CPU-resident vs GPU-only comments; 04-c10/c11-verification.md SDL_CPU_READY_V1|D|
|45|bounded async readback/cleanup|Operations.hpp timeout/cancelRecord machinery|D(partial)|
|46|staging+retained bounded together|CapturePolicy.hpp retained_result_capacity + 03-verification "account for staging...aggregate"|D(partial)|
|47|oversized rejected with actual dims/limits|CapturePolicy.hpp:70-75 bound checks|D|
|48|current limits retained (2048/2073600/8MiB/12MiB)|CapturePolicy.hpp:24,70,75 constants match|D|
|49|PNG/base64/response overhead checked|CapturePolicy.hpp comment line 94; arithmetic not independently re-derived|P|
|50|admitted ops/polling via established workflow|Operations.hpp generic OperationEngine reused by Frame/Translation|D|
|51|retained replay stays historical|Operations.hpp replayed flag (240,925)|D|
|52|read withdrawal checked at work+retrieval|FrameObservation.hpp:1105 authorize(...READ)|D(partial)|
|53|cancellation/teardown drains safely|FrameObservation.hpp pump()/m_stopping|D(partial)|
|54|unavailable reasons distinguish missing/unqualified/pending/expired|FrameObservation.hpp status comments; enum not enumerated this pass|P|
|55|empty/incomplete frame ≠ empty Scene|03-verification.md claim|E|
|56|observation separate from fresh capture|ticket 03 "Out of scope: fresh capture08"|D(doc)|
|57|real viewport comparisons required|merge04r6-verification.md PNG RGB-error comparisons (progress.md notes)|E|
|58|future larger rendering policy hook|no source hook found; prior review flagged narrative-only — concur|D(negative)|
|59|minimal kind-specific examples|01-verification.md claim|E|
|60|unused filters omitted|01-verification.md claim|E|
|61|empty name vs no filter|01-verification.md claim|E|
|62|filter intersection explicit|01-verification.md claim|E|
|63|handles from fresh inspection only|01-verification.md "no zero-handle shortcuts"|E|
|64|invalid args by field+reason|RequestDiagnostics.hpp MAX_ENTRIES=16 struct|D|
|65|kind-inapplicable filters rejected|01-verification.md claim|E|
|66|bounded privacy-preserving diagnostics|RequestDiagnostics.hpp:83 MAX_ENTRIES=16, byte cap not re-measured|D(partial)|
|67|syntax errors rejected pre-admission, no identity consumed|01-verification.md claim|E|
|68|live/foreign checks respect authority+safe points|01-verification.md claim|E|
|69|existing valid behavior retained|01-verification.md regression counts|E|
|70|actual client-arg transport tested|01-verification.md Queen_B/pawn live-client proof|E|
|71|transform inheritance explained in-tool|01-verification.md example text|E|
|72|world position = node origin|01-verification.md claim; Translation.hpp origin semantics (18)|E|
|73|parent+child example|01-verification.md worked example claim|E|
|74|narrower surfaces only with evidence|01-verification.md "no adapter change; no demonstrated loss"|E|
|75|preview world-space translation|Translation.hpp preview path (TranslationService)|D|
|76|preview/commit share real prep|Translation.hpp commit() reuses same plan/validate path (756-832)|D|
|77|one Scene + fresh dependency expectations|Translation.hpp TranslationRequest fields (65)|D(partial)|
|78|ancestors subsume descendants|Translation.hpp:811-826 inherited/root logic|D|
|79|only topmost selected roots normalized|Translation.hpp root-normalization comments; not independently re-derived algorithm|E|
|80|unselected siblings move independently|05-verification.md sibling test claim|E|
|81|inherited selected nodes reported|Translation.hpp:1051-1055 "inherited_selected" payload|D|
|82|unselected descendant effects disclosed|Translation.hpp:964-985 comment on inheritance/effect warnings|D|
|83|incomplete enumeration labeled|Translation.hpp effectEntries bound (31) + 05-verification truncation claim|D(partial)|
|84|duplicate selection handled deterministically|Translation.hpp:922-934 duplicates counter/collapse|D|
|85|only eligible scalar-transform roots move|Translation.hpp:964-979 "Non-transform...identity local placement. Scalar..."|D|
|86|preserve rotation/scale/reflection/shear|05-verification.md negative-mutation-control claim; not independently re-derived math|E|
|87|world deltas via parent transform|05-verification.md + Translation.hpp affine-conversion comment|D(partial)|
|88|local zero-scale retained where valid|05-verification.md claim|E|
|89|singular parents reject w/o repair|05-verification.md "no pseudoinverse" claim|E|
|90|nonfinite/overflow rejected atomically|05-verification.md claim|E|
|91|fractional inputs use existing rounding|05-verification.md claim|E|
|92|current/predicted origins+matrices returned|Translation.hpp payload fields (origins/matrices in result)|D(partial)|
|93|preview creates no write/restore eligibility|Translation.hpp preview path has no commit()/consume() call until actual commit|D|
|94|previews bounded count/bytes/lifetime|Translation.hpp:355-362 limits validation (globalCount<=64, aggregateBytes<=64MiB, maxSelection<=64, lifetimeSeconds finite)|D|
|95|preview governed by current permissions|Translation.hpp authority checks referenced at admission (not re-traced exhaustively)|E|
|96|commit references exact prepared preview|Translation.hpp commit(preview,...) signature (151,756)|D|
|97|identity/topology/ancestry rechecked before commit|Translation.hpp:789-826 revalidate-before-mutate block|D|
|98|stale previews rejected not rebased|Translation.hpp:804-807 "bounded-work exhaustion is not definite staleness" / fail(status)|D|
|99|all-or-nothing commit|06-verification.md claim + commit() single-pass structure|E|
|100|relevant dependencies only, no blanket conflict|06-verification.md "narrow dependency" claim|E|
|101|exposure/permissions re-checked at commit|Translation.hpp commit() authority recheck (789 block)|D(partial)|
|102|successful previews consumed once|Translation.hpp consume() at 469, called once per success path (823,832,888)|D|
|103|retry replay returns same historical outcome|Translation.hpp comment:546 "recognize historical same-ID replay"|D|
|104|existing receipts/Batch References|06-verification.md claim|E|
|105|exact conditional restoration|06-verification.md + ticket06 checkbox (restore machinery reused)|E|
|106|cancellation boundaries preserved|06-verification.md claim|E|
|107|prepared edits outside auth module|Translation.hpp module itself (edit/Translation.hpp, not session/)|D|
|108|expired/evicted/foreign previews rejected|Translation.hpp limits.lifetimeSeconds finite (358) + 06-verification claim|D(partial)|
|109|public headless tests, real Scenes/pipelines|final-review-files.txt test_Integration*.cpp list; not individually read|E|
|110|genuine viewer presentation tests|test_IntegrationViewerPresentation.cpp (file present, not read)|P|
|111|camera/device/renderer/resize/pipeline lifecycle coverage|merge04r6-verification.md claim|E|
|112|source lifetime/slow-consumer memory tests|merge04r6-verification.md Memcheck claim|E|
|113|real CPU/GPU responsiveness qualification|04-c10/c11-verification.md SDL_CPU_READY_V1 qualification narrative|E|
|114|native omission-preserving smoke|01-verification.md live-client proof|E|
|115|preview/commit/reinspect/restore tested e2e|06-verification.md claim|E|
|116|live edit smoke gated by permission|merge06 logs (destructive-on/off) per progress.md|E|
|117|intended revision/build established before coding|progress.md base-correction section|E|
|118|local config/workspace preserved|progress.md "OriginalM.envrc/A.pi/mcp.json were not carried..."|E|
|119|exact test/memory/coverage evidence|merge06-ctest.md, merge06-valgrind.md (not independently reran)|E|
|120|additive, no hidden fallback/scope expansion|final-spec-review.md grep sweep; this session's own spot source checks (21,85,93)|D(partial)|

## Honest gaps in this follow-up

- **All 120 rows present**, but the majority (≈70) rest on **E** (ticket/verification-log evidence only), not this session's own source read. This session directly re-inspected roughly 35 stories against live source (D), concentrated on the high-risk areas requested: latest-request pin (24/30/34/40), presentation/provenance (21/23/35), ownership (43/44/45/46), unsupported-profile (37/44 partially — scale-gating logic itself not re-read this pass, relying on prior review's cited line range), preview/commit atomicity (76/93/96/97/98/101/102/103), topmost/inherited/duplicate selection (78/81/84/85).
- **Not independently re-derived**: the affine world-delta math (87/18), the rotation/shear/reflection preservation proof (86), the PNG/base64 byte-arithmetic bound (49), and the enum of unavailable reasons (54) — these were taken from verification-log narrative, consistent with the prior review's sampling, not newly checked against source line-by-line.
- **test_IntegrationViewerPresentation.cpp and most of the ~364-case integration suite were not read**, only listed as touched files (109/110/111 rest on log claims).
- No build/test/sanitizer rerun performed; all counts are as-reported in prior verification docs, flagged here as such per task constraint.
- No contradictions or scope-creep found beyond what final-spec-review.md already flagged (story 58 narrative-only, consistent, not a new defect).

Reviewer: code-reviewer (n/a, anthropic)

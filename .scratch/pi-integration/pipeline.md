# Pipeline: pi-integration

Milestone: 1
Phase: done
Review round: 0
Paused: none
Frozen: 2026-09-28
Base: wovnpxsxnswn
Review base: ytzypzvtlwlw
Last review: post-review:1:6 13e0f14c-01b1-42e6-b0ea-2e5b6c0460b4 pause
Partition: none (round 2 unpartitioned); prior post-review:1:1 4 packets authorized 2026-09-28
Iterations used: 51

## Run configuration

- Spec dir: `.scratch/pi-integration/`
- Spec: `.scratch/pi-integration/spec.md`
- Milestones: 1 (no explicit milestone sections)
- Implementer iteration budget: 55 (human raised 30→50, then 50→55 on 2026-09-28)
- Post-review cap milestone 1: 3→4→5→6 (human, 2026-09-28)
- Review cap: 7 for initial spec-review (human-authorized extensions 3→5 on 2026-09-21, 5→6 and 6→7 on 2026-09-22, 7→8, 8→9, 9→10 and 10→11 on 2026-09-28); 3 for other review points
- Wave cap: 3
- Ticket-number reservation: 6 per wave member
- Backend: Pi `panel_review`, Standard selection implicit; no external reviewers
- Local-only bookkeeping; no remote publication authorized

## Log

| Date | Milestone | Phase | Round | Result |
|------|-----------|-------|-------|--------|
| 2026-09-18 | 1 | spec-review | 0 | Initialized from explicitly supplied spec; no prior pipeline, issues, or addenda. |
| 2026-09-18 | 1 | spec-review | 1 | Dispatched exactly once with the frozen inline spec and Standard panel. |
| 2026-09-18 | 1 | spec-review | 1 | Outcome pause: invalid-or-incomplete-review; coordinator synthesis-failed (model output limit reached). All original candidates and coverage inspected; scope hash matches. |
| 2026-09-18 | 1 | paused | 1 | Stopped before spec fixes, ticket cutting, or implementation. Outcome was committed before this phase transition. Human recovery required; no automatic retry. |
| 2026-09-18 | 1 | paused | 1 | Human accepted clarification round 1 (atomicity, cancellation, local authority), then round 2 (credentials, defaults, identity, session scope, gate checklist, spec precedence). Applied to unfrozen spec and glossary; original review remains pause. No replacement generation authorized at that point. |
| 2026-09-18 | 1 | spec-review | 2 | Human explicitly authorized round 2 against revised scope. Round 1 remains consumed; round-2 scope and pending outcome committed before generation. |
| 2026-09-18 | 1 | spec-review | 2 | Outcome pause: invalid-or-incomplete-review. Claude reviewer and coordinator both hit model output limits. All 12 retained candidates and coverage inspected; spec unchanged. |
| 2026-09-18 | 1 | paused | 2 | Outcome committed before transition. Stopped without spec fixes, tickets, implementation, configuration changes, or another generation. |
| 2026-09-18 | 1 | paused | 2 | Human accepted round-2 clarification Q1–Q10 in three interview rounds, plus queue-accounting/full-spec acceptance clarifications. Applied to unfrozen spec/glossary; all 12 retained sources mapped. Original incomplete gate remains pause; no new review or panel-budget changes authorized at that point. |
| 2026-09-21 | 1 | spec-review | 3 | User reports increased caps and explicitly requests resume. Verified deployed 8000/16000 limits and validator; round-3 revised scope/pending outcome committed before generation. |
| 2026-09-21 | 1 | spec-review | 3 | Complete Standard review and synthesis. Gate pause: citation-unverified, unresolved-significant; fix sources 0, pause sources 15. All original candidates/assessments/coverage inspected; scope unchanged. |
| 2026-09-21 | 1 | paused | 3 | Outcome committed before phase transition. Initial review cap exhausted; stopped before spec fixes, tickets, or implementation. Human decision required; no automatic round 4. |
| 2026-09-21 | 1 | paused | 3 | User authorized two additional rounds: initial spec-review cap raised from 3 to 5, with prior attempts still counted. Finding/citation pause preserved; no generation or source-level override inferred. |
| 2026-09-21 | 1 | paused | 3 | User requested one Standard advisory consultation on ownership, bounded inspection, and descriptor publication; complete report d5f80eae-ade9-49ad-b612-5a8bc1133c2b retained. User then approved presented contracts/limits, now applied to the unfrozen spec with corresponding tests. Review count remains 3/5; other findings/citations still open. |
| 2026-09-21 | 1 | paused | 3 | Second advisory af923f2a-ac5f-4c73-b397-34972a5958c1 was degraded (Grok schema-invalid); valid perspectives and numeric correction retained. Human approved the parent's five-contract package, applied to spec/glossary/tests. No review round consumed; original evidence gate remains paused. |
| 2026-09-21 | 1 | paused | 3 | User invoked to-spec. Consolidated all approved decisions into the existing local ready-for-agent artifact with 72 stories, reused the confirmed headless test seam, and preserved gate/round counts. No review or implementation run. |
| 2026-09-21 | 1 | spec-review | 4 | User explicitly requested panel review of consolidated spec. Captured complete bytes/hash and exact citation line map; pending round recorded before generation. Original reports/gates preserved. |
| 2026-09-21 | 1 | spec-review | 4 | Complete Standard review/synthesis; pause for citation-unverified and contested-merge. Two medium findings, ten questions, one suggestion; 9 matched and 4 mismatched citations. All sources/assessments inspected; corrected quote locations recorded without changing report or spec. |
| 2026-09-21 | 1 | paused | 4 | Outcome committed before phase transition. Stopped without modifying spec, cutting tickets, implementing, or spending round 5. |
| 2026-09-22 | 1 | paused | 4 | Human approved Q1–Q9 clarifications and Q10 unified operation identity. Applied all ten decisions with test requirements and glossary update; original report/citation/merge outcome retained. One review remains; no generation or implementation. |
| 2026-09-22 | 1 | spec-review | 5 | User explicitly authorized final panel review. Pinned complete revised scope and verified supported unique-section citation format before recording pending dispatch. |
| 2026-09-22 | 1 | spec-review | 5 | Tool review/synthesis complete with pause, but parent verification FAILED: inline attachment added agent plus space to story 40, producing a different hash. Intended file unchanged; complete variant report/findings and both identities retained. |
| 2026-09-22 | 1 | paused | 5 | Outcome committed before transition. Stopped for scope integrity and substantive gate; five attempts consumed. No source correction, unauthorized retry, ticket cutting or implementation. |
| 2026-09-22 | 1 | paused | 5 | Four subsequent human decisions applied: Batch References, four capabilities, scene-wide synchronization guards, and shared local/external scene capacity. Original classifications, scope mismatch, and gates retained. |
| 2026-09-22 | 1 | spec-review | 6 | Human explicitly authorized one additional Standard review, increasing allowance to six and replacing manual inline attachment with the tool's exact file-backed text attachment. All five earlier attempts remain consumed; round-six scope/pending outcome recorded before dispatch. |
| 2026-09-22 | 1 | spec-review | 6 | Complete Standard review/synthesis; exact file-backed artifact hash verified. Tool pause: all 12 section locators unknown-section and unresolved-significant claims. All candidates/coverage/assessments/cleanup inspected; six attempts consumed, no spec changes. |
| 2026-09-22 | 1 | paused | 6 | Verified outcome committed as e5a02d3d before transition. Stopped for citation/assessment gate and exhausted allowance; no locator repair, spec edit, ticket cutting, implementation or unauthorized retry. |
| 2026-09-22 | 1 | paused | 6 | Eight later human clarifications applied to the unfrozen spec and acceptance requirements in 54f564ad, with dispositions for all 12 original sources; original reports/gates and six spent attempts preserved. |
| 2026-09-22 | 1 | spec-review | 7 | User explicitly Authorized round 7 of 7 with Standard selection, file-backed/hash-verified scope, title-only citations, and no automatic retry or implementation afterward. Pinned complete scope and pending outcome before generation. |
| 2026-09-22 | 1 | spec-review | 7 | Complete Standard review/synthesis; exact scope and all ten citations verified. Tool FIX: two surviving original-medium sources, no pause blockers. All findings/coverage/assessments/cleanup inspected; seven attempts consumed, spec unchanged. |
| 2026-09-22 | 1 | paused | 7 | Verified FIX outcome committed as e69bc416 before transition. Stopped at 7/7 cap and scoped review boundary with two medium fix sources; no spec edits, tickets, implementation or automatic retry. |
| 2026-09-28 | 1 | paused | 7 | Agent-skills configuration committed (lyrmmwsx): docs/agents/issue-tracker.md now supplies the full ticket contract. Ticket-format prerequisite cleared. |
| 2026-09-28 | 1 | spec-review | 8 | User explicitly approved round 8 (cap 7→8). Pinned revised spec da40b8dc… and pending outcome before generation; all seven prior attempts remain counted. |
| 2026-09-28 | 1 | spec-review | 8 | Complete Standard review/synthesis, family coverage complete, exact scope hash verified, all 8 citations matched. Gate PAUSE: unresolved-significant (gpt/finding-1 medium). Fix sources 1 (grok descriptor collision, medium), pause sources 1. Spec unchanged. |
| 2026-09-28 | 1 | paused | 8 | Outcome committed before transition. 8/8 cap consumed; stopped without spec edits, tickets or implementation. Human decision required. |
| 2026-09-28 | 1 | paused | 8 | User chose option (b): approved the five proposed round-8 resolutions and a ninth round. Applied to spec in pzvpspsn (new hash 676571f4…). Original round-8 report/gate preserved. |
| 2026-09-28 | 1 | spec-review | 9 | Cap 8→9. Pinned revised spec and pending outcome before generation; eight prior attempts remain counted. |
| 2026-09-28 | 1 | spec-review | 9 | Complete Standard review/synthesis, family coverage complete, exact scope hash verified, all 9 citations matched. Gate PAUSE: unresolved-significant (claude/finding-1 medium). Fix sources 2 (gpt/finding-1, grok/finding-1), pause sources 1. Spec unchanged. |
| 2026-09-28 | 1 | paused | 9 | Outcome committed before transition. 9/9 cap consumed; stopped without spec edits, tickets or implementation. Human decision required. |
| 2026-09-28 | 1 | paused | 9 | User chose (b): approved the eight proposed round-9 resolutions and a tenth round. Applied in nuooorsm (hash 65e3f2a2…). Original round-9 report/gate preserved. |
| 2026-09-28 | 1 | spec-review | 10 | Cap 9→10. Pinned revised spec and pending outcome before generation; nine prior attempts remain counted. |
| 2026-09-28 | 1 | spec-review | 10 | Complete Standard review/synthesis, family coverage complete, exact scope hash verified, all 8 citations matched. Gate FIX: 1 medium fix source (gpt/finding-1 alpha quantization), 0 pause sources. Claude needed a tool-internal second attempt after schema-invalid first response. Spec unchanged. |
| 2026-09-28 | 1 | spec-review | 10 | Fix applied per §spec-review step 3 in yvtmrrnp: alpha clamps then quantizes with RGB's round-to-nearest (×255, exact halves up), tie rule made explicit for RGB too; acceptance item 10 updated. Spec hash 9043bd7d…, 88,274 bytes. Review round 10. |
| 2026-09-28 | 1 | paused | 10 | Review round == cap (10). Stopped: revised bytes unreviewed; open questions gpt/finding-2, grok/q-auth-outcome-codes, grok/q-progress-vocabulary, grok/s-referenced-docs remain for human decision. No tickets or implementation. |
| 2026-09-28 | 1 | paused | 10 | User chose (b): approved the proposed answers to the four open round-10 items plus the undisclosed-attempt hook concern, and an eleventh round. Applied in wmorsxyw (hash 3a713fd5…). |
| 2026-09-28 | 1 | spec-review | 11 | Cap 10→11. Pinned revised spec and pending outcome before generation; ten prior attempts remain counted. |
| 2026-09-28 | 1 | spec-review | 11 | Report degraded: Family coverage REDUCED — claude lost (two invalid attempts). Quorum held. Gate FIX: 1 medium fix source (gpt/finding-1 view registered after sync), 0 pause. Exact scope verified; all 5 citations matched. Spec unchanged. |
| 2026-09-28 | 1 | spec-review | 11 | Fix applied in mysnupyp (hash bc225c9f…, 91,801 bytes): new-view confirmation before renderable (gpt/finding-1); plus editorial/question resolutions — frozen state names in prose (grok/finding-1), per-capability hook pairing (grok/finding-2), identical replay bypasses pool capacity (gpt/finding-2), enumerated pass-through source formats (grok/finding-3). Acceptance items 6 and 8 updated. Review round 11. |
| 2026-09-28 | 1 | paused | 11 | Review round == cap (11). Stopped: revised bytes unreviewed; no tickets or implementation. |
| 2026-09-28 | 1 | spec-review | 11 | HUMAN OVERRIDE: user chose (a) — accept spec `bc225c9f05604dbf37311d04a48d065d153d285098a702c1541170bddd68755e` (change mysnupyp) without further review. Binds round spec-review:1:11, report f6df75c1-… (fix, reduced coverage: claude lost), whose fix source gpt/finding-1 and all other sources were resolved in mysnupyp. Rationale: steadily narrowing findings over rounds 8–11; required fix applied. Original gate/report retained; no clean outcome claimed. Review round reset to 0; Phase cut. |
| 2026-09-28 | 1 | cut | 0 | Cut milestone 1 into 18 tickets (01–18) in wovnpxsx per docs/agents/issue-tracker.md. Cut-step-1 re-review skipped: M=1 and no addenda. Spec frozen (bc225c9f…). Base wovnpxsx. Phase drain. |
| 2026-09-28 | 1 | drain | 0 | work-tickets pass (waves, no-finish), 27 iterations. Resolved: 01–14, 16, 17, 103–105, 119–122. Split (wontfix): 15→103/104/105, 80→119/120/121. Failed once then resolved: 09 (semantic conflict with 08 after wave landing; WIP ylxzvxzyrskm retained). Orchestrator re-pointed 18's blocker from split 15 to 103–105. Integration suite 137 cases / 5450 assertions green incl. real helide render and loopback MCP. |
| 2026-09-28 | 1 | paused | 0 | Holes: 23 filed. One CONTRADICTS: 137 (restore conflict permanently marks batch CONFLICTING and releases payload; spec §11 says conflict failure changes neither values, versions nor original eligibility). Implementer labelled it extends; orchestrator reclassifies as contradicts. Pause per drive-spec §drain step 5; extends-hole addenda not yet written. Human-blocked: 18 (needs 135, 136, 118), 135 (viewer registers no MCP tools/view), 136 (Pi gateway cannot attach/renew; adapter shape decision). |
| 2026-09-28 | 1 | drain | 0 | HUMAN: 137 => (a) keep implemented permanent-conflict behavior; 136 adapter => C++ stdio proxy in-repo; budget 30→50. Wrote addenda 01–05 covering all 23 extends/contradicts holes; resolved hole tickets; cut follow-ups 138–145; unblocked 18 (Blocked by 143, 144, 145). Resume drain. |
| 2026-09-28 | 1 | drain | 0 | Second drain pass: 15 iterations (total 42/50). Resolved 138–145, 152, 153, 18, 201, 202, 213; 145 failed once (WIP lzttynzolroo) then resolved on retry. Addenda 06 (discovery activity targets, name signal), 07 (viewer agent view), 08 (agent-view framing) written for extends holes; 176 folded into 139, 183 ratified by 07. Pi end-to-end via vsrMcpProxy passed on headless host and vsrViewer (images verified). Frontier empty; no ready-for-human/needs-info. Full build + ctest 34/34 green. Phase post-review. |
| 2026-09-28 | 1 | post-review | 1 | Code scope pinned: Base wovnpxsx (074d99d8) .. tip b699ddd9; 138 diff entries, 74 excluded (all under .scratch/pi-integration/, none crossing), 64 files kept, all regular UTF-8 text on both sides. Patch --context 30: 767,886 bytes / 23,746 lines, sha256 175e3b6d…; saved .scratch/pi-integration/review/post-review-1-1.diff (+ .files sha f34aeb41…). Pending before dispatch. |
| 2026-09-28 | 1 | post-review | 1 | panel_review report ff4c2150-b90a-498e-b41a-68b9b7ca5f95: status rejected, error `artifact exceeds byte budget`, no participants, no attempts, gate pause (invalid-or-incomplete-review). Tool did not name the admissible budget. No reviewer consumed; spec-compliance pass not run. Per §Partition, no smaller re-capture. Measured composition: 64 files; src/vsr/integration headers+cpp ~397 KB, integration tests ~265 KB, apps (viewer/proxy/tutorial) ~61 KB, docs/READMEs ~29 KB, scene/ui/CMake ~27 KB (per-file --context 30 sizes; sum 760,917 bytes vs whole-capture 767,886). Paused for human scope decision. |
| 2026-09-28 | 1 | post-review | 1 | HUMAN: `Split authorized` — the proposed 4-packet partition of post-review:1:1 (same round key, one round). The rejected ff4c2150 attempt reached no reviewer; it is superseded by this authorized partitioned dispatch, not a regeneration of a completed round. Packets pinned below before any dispatch. DEPARTURE: milestone reviewed in packets. |
| 2026-09-28 | 1 | post-review | 1 | Partitioned round dispatched once per packet: p1 58e8416b (synthesis-failed, pause), p2 16ca739a (pause, reduced), p3 d75894e5 (pause, reduced), p4 d232cbec (pause, complete). Combined PAUSE. ~39 retained candidates incl. several concrete high/medium (stod terminate, proxy unbounded read, service() re-entrancy/concurrency, lock-order risk, edit canonicalization, render pending-OK re-guard, WARN-skip security tests). |
| 2026-09-28 | 1 | paused | 1 | Outcome committed before transition. Stopped for human decision; no tickets cut, no compliance pass, no retry. |
| 2026-09-28 | 1 | post-review | 1 | HUMAN DECISION (option a) on round post-review:1:1 (reports 58e8416b, 16ca739a, d75894e5, d232cbec; packet hashes 191dbb80/459dd1a8/409dd9ff/61a70e8f): accept the listed candidates as fix findings — p3 gpt/finding-3+grok/json-stod-throw, gpt/finding-1+grok/proxy-unbounded-http-read, gpt/finding-2, gpt/finding-4, coordinator notes (slow-drip header DoS, stale-descriptor credential exposure, fd reuse in stop(), stod locale); p1 gpt/finding-1, gpt/finding-2, gpt/finding-3, grok/nested-service-clears-servicing-thread, grok/deadlock-engine-integration, grok/dispatch-fail-stuck-slot, grok/timing-data-race; p2 gpt/finding-1, gpt/finding-2, gpt/finding-3, coordinator scratch-leak note; p4 gpt/finding-1..4, claude/finding-1+grok/ipv6-loopback-warn-skip, claude/finding-2+grok/descriptor-acl-warn-skip, grok/mcp-proxy-wallclock-flake. Coordinator dismissals (png-iend-null-insert, restoration-expired-ignores-launch, scratch-release-underflow as stated, stdio line-limit as stated, readLine parse) accepted as dismissed. Original gate/reports retained; override recorded here only. Proceed: compliance pass, cut, drain. |
| 2026-09-28 | 1 | post-review | 1 | Spec-compliance pass (code-review skill, fixed point Base 074d99d8, tip b699ddd9, ~.scratch excluded, spec + addenda 01–08): Spec (a) 2 — zero-scene session-wide discovery not admissible (→ medium, ticket 216); docs/pi-attachment.md 213 stale (→ medium, ticket 218). (b) 0. (c) 0 confirmed (1 clarity note on session-wide FORBIDDEN masking). Standards: REQUEST CHANGES — STYLEGUIDE §5 in-class method bodies, §3 include order/labels, §15 std::map vs FlatMap, duplicated result-store shape; per rule filed as needs-triage only on the clean round — retained here. |
| 2026-09-28 | 1 | post-review | 1 | Cut 214–218 (chore(tickets): milestone 1 review round 1, lsyqvqzp). All human-accepted fix findings and compliance mediums covered by ready-for-agent tickets. Review base lsyqvqzp. Review round 1. Phase drain. |
| 2026-09-28 | 1 | drain | 1 | Fix drain: 5 iterations (47/50). Resolved 214–218; holes 225 (→ addendum 09 proxy local codes) and 237 (→ addendum 10 edit value content) ratified. Full build + ctest 34/34; integration 171 cases green. Frontier empty. Phase post-review. |
| 2026-09-28 | 1 | post-review | 2 | Code scope pinned: Review base lsyqvqzp (aaa0836c) .. tip f3266ab0; 36 entries, 10 excluded (.scratch only), 26 files kept, all regular UTF-8 text. Patch --context 30: 195,843 bytes, sha256 1808521fdafb45135ff0389dd1e6f4bc5f7e7254fecb47f9adcd6cb25f87483d; saved review/post-review-1-2.diff. Pending before dispatch. |

| 2026-09-28 | 1 | post-review | 2 | Report e48ef8cb: degraded, gate PAUSE (citation-unverified, unresolved-significant), coverage REDUCED (claude incomplete: output limit). Fix 0, pause 2. gpt/finding-1 MED McpServer SO_SNDTIMEO is per-send; sendAll loop lets a slow reader hold a thread past sendTimeoutMs (coordinator: mechanism sound, severity likely below medium, needs large authenticated responses). gpt/finding-2 LOW new concurrency test in test_IntegrationOperations.cpp spins on slotsInUse() without deadline (coordinator: same in submitter BUSY loop and test_Registration 'while (ran < N) yield'). grok: no findings. Coordinator spot-checked /proc/net ownership compare, render re-guard, service() serialization: no defect. Freshness: tree differs from f3266ab0 only under .scratch. Compliance pass not run (gate pause). |
| 2026-09-28 | 1 | paused | 2 | Outcome committed before transition. Review round 2/3. Claude lost/incomplete in 3 consecutive review points: backend reliability issue. Human decision required. |
| 2026-09-28 | 1 | post-review | 2 | HUMAN DECISION (option a) on post-review:1:2 report e48ef8cb (patch sha 1808521f): accept gpt/finding-1 (medium, original severity kept) and gpt/finding-2 (low) plus coordinator test-spin notes as fixes. Compliance pass for round 2 not separately rerun: spec-compliance is Base-wide and was run in round 1 on the full milestone; round-2 changes are fixes of accepted findings (compliance will rerun on the next clean round). Cut 219. |
| 2026-09-28 | 1 | drain | 2 | Fix drain: 1 iteration (48/50). Resolved 219 (total response send deadline; bounded test spins). Build + ctest 34/34 green. Frontier empty. Phase post-review. |
| 2026-09-28 | 1 | post-review | 3 | Code scope pinned: Review base qlvkstrk (305c7866) .. tip b8944984; 8 entries, 4 excluded (.scratch only), 4 files kept (McpServer.hpp + 3 tests), regular UTF-8. Patch 23,943 bytes, sha256 ee49604089081054d5f9f12299fb519316962529900b7ddbb16adacc61663690; saved review/post-review-1-3.diff. Pending before dispatch. Final round (R=3). |
| 2026-09-28 | 1 | post-review | 3 | Report 52301fcb: degraded, gate PAUSE (citation-unverified, unresolved-significant, significant-dismissal); coverage REDUCED (claude invalid: location anchor). Fix 0, pause 3. gpt/finding-1 MED deadlock test still unbounded at host.join() if host stuck in service() (unresolved; residual adequacy gap). gpt/finding-2 LOW slow-reader test 503 assertion can flake if test thread stalls ~deadline (unresolved). grok/finding-1 HIGH 503 sendAll under m_connMutex (REJECTED: ~80-byte write to empty send buffer returns immediately; coordinator notes lock-across-I/O is stylistic). grok/finding-2 LOW deadline measured from sendAll entry vs Config comment 'from first byte' (substantiated; doc mismatch, behavior stricter). Coordinator: core fix correct (steady_clock, poll+MSG_DONTWAIT, EINTR/EAGAIN, close on expiry). Freshness: tree differs from b8944984 only under .scratch. |
| 2026-09-28 | 1 | paused | 3 | Outcome committed. Review round 3 == R: post-review cap hit on milestone 1. No tickets cut. Human decision required (dismissal of a high claim; residual test-adequacy and doc findings; Standards findings pending triage). |
| 2026-09-28 | 1 | post-review | 3 | HUMAN DECISION (option b) on post-review:1:3 report 52301fcb (patch sha ee496040): allow a 4th post-review round (cap 3→4) and raise budget 50→55; accept gpt/finding-1 (medium), gpt/finding-2 (low), grok/finding-2 (low) as fixes; confirm coordinator dismissal of grok/finding-1 (high: 503 send under lock). Cut 220. |
| 2026-09-28 | 1 | drain | 3 | Fix drain: 1 iteration (49/55). Resolved 220. Build + ctest 34/34 green. Phase post-review. |
| 2026-09-28 | 1 | post-review | 4 | Code scope pinned: Review base olrxsltx (cc140155) .. tip 9b156b2b; 3 files (McpServer.hpp + 2 tests), 2 .scratch excluded, regular UTF-8. Patch 17,668 bytes, sha256 95634e4260fae9968699a8e3125795f2cbadf4e3a55b45369be84080f303e62e; saved review/post-review-1-4.diff. Pending. Final round (cap 4). |
| 2026-09-28 | 1 | post-review | 4 | Report 67c70edb: degraded, gate PAUSE (reason: citation-unverified only); coverage REDUCED (claude invalid anchor, 5th consecutive review point). Fix 0, pause 1. gpt/finding-1 MED 500 ms deadline test may not prove deadline-driven release (coordinator REJECTED: timing starts within ~5 ms of send start, 32 MiB at 1 B/10 ms cannot finish in 3 s window, 10 s sendTimeout default; residual gap covered by sibling 503 test). grok/q-peer-close-120s QUESTION (unresolved in report) — parent measured: occupied-slot scenario with 120 s deadline completes in 0.24 s (3 runs), all 3 slow-* scenarios pass 5/5: peer close frees slot promptly. Coordinator confirmed deadlock-join fix sound. Freshness OK. |
| 2026-09-28 | 1 | paused | 4 | Outcome committed. Cap 4/4 reached. No accepted high/medium defect remains open; closing requires human decision (gate never returned clean). |
| 2026-09-28 | 1 | post-review | 5 | HUMAN: option (b) after round 4 — another round (cap 4→5). Round 4 cut no tickets, so scope is unchanged: Review base olrxsltx (cc140155) .. tip 3e43d806 (differs from 9b156b2b only under .scratch); same 3 files; patch sha256 95634e42… byte-identical to review/post-review-1-4.diff. Objective adds explicit output-format rules (one location anchor per finding, bare JSON, class-appropriate fields) to recover claude coverage; no reviewer/profile change. Pending before dispatch. |
| 2026-09-28 | 1 | post-review | 5 | Report ccbaa444: complete, family coverage COMPLETE (claude succeeded first time since spec round 9 — explicit output-format rules in objective worked). Gate PAUSE (citation-unverified, unresolved-significant). Fix 0, pause 6. gpt/finding-1 MED 500 ms scenario lacks any held-slot/lower-bound check before polling for 200 (unresolved; coordinator: real coverage gap, partly mitigated by 120 s scenario, likely deliberate anti-flake trade-off). claude/finding-2 Q 10 s first-byte headroom (unresolved; pre-existing). REJECTED: claude/finding-1 MED std::abort kills process (no safe alternative; intended); claude/finding-3 LOW 503 sync (send cannot finish before immediate ping); grok/finding-1 MED drain loop reuses 30 s deadline (unchanged context; only fails when submitters already failed); grok/finding-2 Q fd on reuse (no reuse). Freshness: tree differs from 3e43d806 only under .scratch. |
| 2026-09-28 | 1 | paused | 5 | Outcome committed. Cap 5/5 reached. No production defect open; one test-coverage medium unresolved. Human decision required. |
| 2026-09-28 | 1 | post-review | 5 | HUMAN DECISION (option b) on post-review:1:5 report ccbaa444 (patch sha 95634e42): accept gpt/finding-1 (medium) as fix, fold claude/finding-2 question; confirm coordinator rejections of claude/finding-1, claude/finding-3, grok/finding-1, grok/finding-2. Cap 5→6. Cut 221. |
| 2026-09-28 | 1 | drain | 5 | Fix drain: 2 iterations (51/55). Resolved 221 (500 ms lower-bound check; first-byte wait documented); hole 222 (slot freed after close → back-to-back 503 race) → addendum 11, follow-up 223 resolved (done set with close under lock; 50-reconnect regression; restored back-to-back check). Build + ctest 34/34 green. |
| 2026-09-28 | 1 | post-review | 6 | Code scope pinned: Review base ytzypzvt (71f32b55) .. tip 9e40e8d6; 2 files (McpServer.hpp, transport test), regular UTF-8. Patch 10,434 bytes, sha256 5cf95614f13475ced294d126cbed5a1b6780f19e38bbda2b7ad325631ac5e542; saved review/post-review-1-6.diff. Pending. Final round (cap 6). Objective carries output-format rules. |
| 2026-09-28 | 1 | post-review | 6 | Report 13e0f14c: complete, coverage COMPLETE (claude ok again with format rules). Gate PAUSE (citation-unverified, unresolved-significant). Fix 0, pause 5. Coordinator: server done-with-close fix sound. gpt/finding-1 MED early-probe REQUIRE(503) can fail on a correct server — parent verified: server response deadline starts at sendAll (McpServer.hpp:667) before client firstByte, and start() tolerates up to 10 s first-byte delay, so measured-from-sendStart underestimates server elapsed; comment reasoning inverted. Test-only flake risk. claude/finding-1 LOW + grok/sug-1 (merged) skip/WARN paths erode coverage (mitigated by deterministic 120 s scenario). claude/finding-2 LOW close under lock (REJECTED: unchanged). grok/q-1 answered by parent: Fixture::call sends Connection: close (test file line 169). Freshness OK. |
| 2026-09-28 | 1 | paused | 6 | Outcome committed. Cap 6/6. No product defect open. Remaining: one test-only flake risk in the 500 ms probe and its coverage trade-off. Human decision required. |
| 2026-09-28 | 1 | post-review | 6 | HUMAN DECISION (option a) on post-review:1:6 report 13e0f14c (patch sha 5cf95614): close milestone 1 despite non-clean gate. Accept gpt/finding-1 (test flake) with the orchestrator-applied remedy: remove the timing-dependent held-slot probe from the 500 ms scenario (commit nkxvvwsv, test-only, no further review round by decision); held-slot coverage remains in the deterministic 120 s scenario. Verified: slow+sequential scenarios 30/30, [integration] 174 cases, ctest 34/34. Confirmed rejection of claude/finding-2. Coverage-erosion finding (claude/finding-1 + grok/sug-1) moot after probe removal. |
| 2026-09-28 | 1 | done | 0 | Milestone 1 closed by human override (no recorded clean gate at any post-review round; original gate outcomes retained above). Filed needs-triage: 224 (STYLEGUIDE §5 in-class bodies), 226 (§3 includes, §15 FlatMap, duplicated result store), 227 (watcher join on throw; <cctype>), 228 (SlowReader 10 s first-byte headroom). Last milestone → Phase done. Iterations used 51/55. |
## Post-milestone follow-up cleanup (2026-10-02)

User approved the follow-up recommendation after exercising Pi attachment on
the conflict-resolved branch. This is bounded maintenance, not a reopened
milestone or a new panel-review round; the original human override and all
review gates above remain unchanged.

- 227 resolved: exception-safe RAII watcher cleanup and direct `<cctype>`.
  Public-headless regression reproduced SIGABRT before the fix and passed after.
- 224 resolved: nonempty in-class bodies moved to qualified inline definitions
  in the five ticketed headers; token comparison confirms bodies are unchanged.
- 226 partially addressed: mechanical include groups/order and unused includes.
  FlatMap decisions and result-store extraction remain `needs-triage`.
- `docs/pi-attachment.md` refreshed for current Pi built-in MCP support, with
  generated-descriptor explanation, exact path selection, project trust,
  reload, smoke-test steps, and reconnect guidance. Historical observations
  preserved; no new lease/security acceptance run claimed.
- Verification: full CPU build, install, 68/68 CTest entries, targeted clangd
  diagnostics, formatting checks, and headless activation smoke test. CUDA not
  tested. Dedicated reviewer backend was rate-limited; two fresh-context
  test-engineer checks supplied separate correctness and standards reviews.
  Correctness had no findings; the standards class-comment finding was fixed.
- User-local `.pi/mcp.json` and `checkmatte.png` were not changed or included in
  maintenance commits. No publication authorized or performed.

## Partition post-review:1:1

- Authorized: 2026-09-28, human `Split authorized` after oversize rejection ff4c2150 (budget unnamed by tool).
- Pinned range: 074d99d8dbf5959963c932f3bcb3a0239b1a68bd..b699ddd984f1915b6fe87f66956c9a41fb4131be, `jj diff --git --context 30`, literal root-file filesets, `.scratch/pi-integration/` excluded.
- Totality: 64 discovered files = 14 + 8 + 22 + 20; disjoint and union-equal verified by path set; each packet has one `diff --git` per file; packet bytes sum 767,886 = whole capture.
- Boundaries follow dependency structure: (1) core engine contracts consumed by everything, (2) scene operations built on (1), (3) wire/transport/apps consuming (1)+(2), (4) tests exercising all.

| Packet | Content | Files | Bytes | sha256 | Manifest |
|---|---|---|---|---|---|
| p1 | core engine: Integration.*, session/, operations/, identity/, values/, activity/, scene Object/UpdateDelegate, CMake | 14 | 146,074 | 191dbb80bc5b33f3f680623ccf1e3a4af91feb1d1a5369f3cea895f8219f3ba7 | review/post-review-1-1.p1.files |
| p2 | scene operations: inspection/, edit/, restore/, sync/, render/, image/, context/ | 8 | 112,183 | 459dd1a88d0421abab1333f695e331d21097a2cc5eff7f800a30eebf125e335b | review/post-review-1-1.p2.files |
| p3 | wire + apps: descriptor/, mcp/ (server, tools, README), apps/tools proxy, vsrViewer, ImGui window, tutorial host, docs | 22 | 203,540 | 409dd9ff2edc6ac0dac00d6ea34946ca68a4bc5e565049428a71fabacd837f53 | review/post-review-1-1.p3.files |
| p4 | tests: src/tests/integration/*, src/tests/CMakeLists.txt | 20 | 306,089 | 61a70e8f0c3fdb87e7b9ef0528dcd6c076048daafb2d795607fef2dddabefeae | review/post-review-1-1.p4.files |

### Packet outcomes

- **p1** report `58e8416b-a83d-44ad-a7d6-a6c40170fe56`: synthesis-failed (coordinator 503 upstream connect error), gate **pause** (invalid-or-incomplete-review), family coverage unknown. gpt success, grok success, claude invalid (JSON parse: fenced output). Retained unadjudicated candidates: gpt/finding-1 HIGH concurrent service() calls run scene work simultaneously (Integration.cpp); gpt/finding-2 MED throwing SceneWork abandons batch and leaves servicing thread set; gpt/finding-3 MED Integration::submit queue unbounded; grok/deadlock-engine-integration HIGH lock-order inversion engine m_mutex (ActivitySink/sceneOfView) vs Integration m_mutex in service()→runStep; grok/nested-service-clears-servicing-thread HIGH; grok/timing-data-race MED timing() unsynchronized ref; grok/dispatch-fail-stuck-slot MED failed submit after admit leaks slot; grok/api-caller-assumptions QUESTION. Citations all mismatched/n.a. (diff-anchor line claims). Not confirmed; human decision required.
- **p2** report `16ca739a-ad64-47fa-873c-024877df5132`: degraded, gate **pause** (citation-unverified, unresolved-significant, significant-dismissal); family coverage REDUCED (claude lost: invalid location anchor). Fix sources 0, pause sources 8. Candidates: gpt/finding-1 HIGH edit canonicalArguments collapses non-number/array values (unresolved); gpt/finding-2 HIGH render pending-OK success skips re-guard after delayed release (unresolved); gpt/finding-3 MED PngEncode leaks zlib buffer on bad_alloc (unresolved, coordinator: low); grok/png-iend-null-insert HIGH (REJECTED: [nullptr,nullptr) valid empty range); grok/restoration-expired-ignores-launch MED (REJECTED: caller gates on launchId); grok/scratch-release-underflow MED (REJECTED as stated; coordinator raises opposite risk: scratch leak if COMPLETE never observed); grok/discovery-dense-ids QUESTION (sparse IDs after unregister may hide views; unresolved); grok/p1-api-assumptions QUESTION. Coordinator-raised unsourced notes: HeadlessServiceLoop watcher thread joinable on throw; Inspection.hpp missing <cctype>. Rejected high claim = significant-dismissal → human decision.
- **p3** report `d75894e5-7006-4851-a5f4-6b2e63c01bc4`: degraded, gate **pause** (citation-unverified, contested-merge, unresolved-significant); family coverage REDUCED (claude execution-failed: content_filter). Fix 0, pause 10. Candidates: gpt/finding-3 + grok/json-stod-throw HIGH (merged) Json.hpp std::stod throws on 1e9999 → std::terminate from connection thread/proxy; gpt/finding-1 HIGH + grok/proxy-unbounded-http-read MED (merged, severity dispute) proxy post() unbounded read, no deadline; gpt/finding-2 MED proxy forwards after failed reattach; gpt/finding-4 MED McpServer no SO_SNDTIMEO; gpt/finding-5 MED stdio line limit (REJECTED as stated; real quirk: tail of overlong line dispatched as separate message); grok/mcp-cl-gt-max-as-400 LOW; gpt/finding-6 QUESTION render result() authz; grok/p1-p2-seam QUESTION. Coordinator-raised unsourced: unauthenticated slow-drip header DoS (16 slots, 10 s recv timeout); stale descriptor after crash → proxy may send launch credential to a rebinding listener; worker closes c.fd without -1 → stop() may shutdown a reused fd; std::stod locale sensitivity.
- **p4** report `d232cbec-caf1-4739-b118-c84424f07c4f`: complete, gate **pause** (citation-unverified, unresolved-significant, contested-merge); family coverage complete. Fix 0, pause 12. Candidates: gpt/finding-1 HIGH identity test uses MaterialRef after its Scene reset (possible UAF; unresolved, needs p1 ownership); gpt/finding-2 MED context test unbounded wait; gpt/finding-3 MED MCP test harness recv no deadline; gpt/finding-4 MED global logging callback captures local by ref without RAII; claude/finding-1 MED + grok/ipv6-loopback-warn-skip LOW IPv6 test WARN-skip passes; claude/finding-2 LOW + grok/descriptor-acl-warn-skip MED ACL test WARN-skip passes; grok/mcp-proxy-wallclock-flake MED + claude/finding-3 Q proxy test uses wall clock; claude/finding-4 Q (REJECTED: request() REQUIREs parse); grok/mcp-ifdef-compiles-to-nothing Q; grok/seam-impl-unverified Q.

### Combined outcome (conjunctive)

- Round `post-review:1:1` = **pause** (p1 pause: synthesis-failed; p2 pause; p3 pause; p4 pause). One round consumed (Review round 0→1).
- Family coverage: p1 unknown (coordinator 503; claude invalid), p2 reduced (claude lost), p3 reduced (claude lost: content_filter), p4 complete. Claude lost in 3 of 4 packets this round (and in spec rounds 10–11): backend reliability issue with the claude reviewer, not a code signal.
- Freshness: working tree differs from tip b699ddd9 only under .scratch/pi-integration/.
- Spec-compliance pass (§post-review step 3) NOT run: gate pause stops the phase before cutting tickets.
- No tickets cut from this round. Candidates above are unadjudicated; human decision required per §Outcome.

## Review spec-review:1:11

- Authorization: user `b` after round 10 (apply proposed answers, run round 11). Cap 10→11; one generation, Standard implicit, no override.
- Pre-round resolutions applied (wmorsxyw): gpt/finding-2 → discovery items return effective capabilities; capability change invalidates cursors (§4, item 9). grok/q-auth-outcome-codes → frozen `unauthenticated`, `invalid_session_proof`, `forbidden`, returned directly by headless API (§9, item 2). grok/q-progress-vocabulary → frozen progress `queued/running/yielded/synchronizing/awaiting_cleanup` and sync-state names `ready/needs_sync/synchronizing/failed` (distinct from `sync_required` outcome) (§9, item 2). grok/s-referenced-docs → Further Notes states completeness without referenced files. Claude attempt-1 (invalid, unadjudicated) hook concern → activation rejects edit-exposed scenes without normal-sync hook and resynchronize-exposed scenes without recovery callback (§10, item 1).
- Mode: spec; manifest exactly `.scratch/pi-integration/spec.md`, file-backed.
- Pinned SHA-256 `3a713fd5eb36d8619307e87493bcff3850bc809b2a443229d270d20a9b40c20b`; 90,551 bytes; 429 lines; 72 stories; unique titles; no control bytes.
- Source change `wmorsxywqyllynoksopooynlvotvxyxs`; commit `ff8de576c422915c87a1fed8c65068c9a8dfcce8`. `.envrc` unchanged.
- Report ID: `f6df75c1-8850-44c7-a8ac-3dbe8cf69262` (status degraded). Exactly one generation call.
- Pipeline gate: **fix**; reasons none; notes `reduced-coverage`. Fix sources 1, pause sources 0.
- **Family coverage: reduced. Lost family: anthropic (claude).** Attempt 1 invalid (`JSON Parse error: Unrecognized token '`'` — fenced output); attempt 2 invalid (`findings[1].preconditions` on a question). Neither validated; claude silence is not agreement. Unvalidated claude texts raised only questions/suggestions (batch_id entropy, fixture, activity vs restorable caps, operator role, attachment procedure, ID lexical shape, elevation example, disable-edits path, stalled slots) plus nothing classified high/medium in attempt 1; recorded for awareness, not adjudicated. Round 10 also needed a claude retry — not two consecutive reduced rounds.
- Scope integrity PASS: audit SHA-256 `3a713fd5…c20b`, 90,551 bytes, 429 lines = pin = immutable source = live. `.envrc` unchanged.
- Packet hashes: reviewer `cf42fce7a72dbb85c8f30a057b8a62b86fe80296626ad53ec5e13c87428bfe14`; claude attempt 2 `924517617eee8e079670be64af39e4666ad0d5a734c9c2125126f4615b202526`; coordinator `4eb227f05e7147fcabe42ab48d56f532bcfe67ffb8d4553ae582ac570d9038f1`. All processes exit 0, cleanupConfirmed.
- Inspection: formatted report in full; raw pages at 112000 and 135145 (all attempts, checks, groups, assessments). No export.

### Round-11 original sources and assessments

| Group / source | Original classification | Section | Claim | Effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 10. Agent Edit Batches and downstream synchronization | View registered after last sync is not covered by readiness; render could use unconfirmed state. | substantiated. **Fix source.** |
| group-2: gpt/finding-2 | question | 9. Operation identity, admission, and outcomes | Must identical retained-ID replay work when the pool is full? | unresolved. |
| group-3: grok/finding-1 | defect/medium | 10. Agent Edit Batches and downstream synchronization | Prose "synchronization required" vs frozen `needs_sync`. | rejected (editorial). Original medium noted; not a fix source per gate. |
| group-4: grok/finding-2 | defect/medium | 10. Agent Edit Batches and downstream synchronization | Hook-activation sentence readable as requiring both hooks. | rejected (editorial). Not a fix source per gate. |
| group-5: grok/finding-3 | question | 12. Revision-pinned rendering | Which source formats pass through vs quantize alpha. | unresolved. |

## Review spec-review:1:10

- Authorization: user `b` after round 9. Cap 9→10; one generation, Standard implicit, no override, no automatic retry.
- Mode: spec; manifest exactly `.scratch/pi-integration/spec.md`, file-backed, ID spec, kind text.
- Pinned SHA-256 `65e3f2a271f7760715b576ebd56f9ca1417b8fa9aec2b76bab560b59418a9646`; 87,977 bytes; 428 lines; UTF-8 without control bytes; unique titles.
- Source change `nuooorsmvwtxovnkontrttkkuyxqpukq`; commit `11c29d3c7acb7f42b9f42192603b3c672393276a`. Base/Review base none. `.envrc` unchanged.
- Report ID: `5c084530-d5e4-46e0-94dd-1b5c5d632aad`. Status complete; exactly one generation call.
- Pipeline gate: **fix**; reasons none; notes none. Fix sources 1, pause sources 0. Family coverage: complete.
- Scope integrity PASS: audit artifact file origin, SHA-256 `65e3f2a2…9646`, 87,977 bytes, 428 lines = pin = immutable source = live file after review. `.envrc` unchanged.
- Reviewer packet hash `c1a4e678c7d45c0bba7c73572014a688be66fe663d9002732f3ed3df4459d338` (claude attempt 2 packet `bf24def04534a3b88ba7d54ed0cf833f23981245caa7785297d8eb94e5b7013e`); coordinator packet `a7d62a8cc5a555284bc509a3560c39b6a7e2277a11564ed67dc0e9e420daca8b`.
- Participants same models as rounds 8–9. gpt 1 attempt (1466 out); grok 1 attempt (3713); claude attempt 1 `invalid-response` (`response.findings[1].claim: required field`), tool-internal attempt 2 success (2964); coordinator 1 attempt. All exit 0, cleanupConfirmed. All 8 citations matched; warnings empty; 8 singleton groups; no merges; no dissent.
- Disclosure: claude's invalid attempt-1 text contained a medium claim (no behavior defined when a host registers an editable scene without a normal-synchronization hook). It is NOT part of the validated response and was not adjudicated; recorded for human awareness only.
- Inspection: formatted report in full; raw page at 119500 (audit/attempts/raw texts). No export, no retry by parent.

### Round-10 original sources and assessments

| Group / source | Original classification | Section | Claim | Effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 12. Revision-pinned rendering | No rounding rule for fractional alpha → 8-bit. | substantiated. Real gap; impact softened by tolerant oracle. **Fix source.** |
| group-2: gpt/finding-2 | question | 6. Identity and revision semantics | Do effective-capability changes invalidate discovery cursors? | unresolved. Largely answered (membership/returned fields); capabilities not explicitly a returned field. |
| group-3: claude/finding-1 | defect/medium | 12. Revision-pinned rendering | Width/height wording ambiguous re request fields. | rejected. No per-request override; constraints apply to view dimensions. |
| group-4: claude/finding-2 | question | 8. Editable values and numeric conversion | Double-rounding fixture unverified. | rejected. Verified again. |
| group-5: claude/finding-3 | defect/low | 5. Execution, admission, and resource accounting | Indefinitely held slots can exhaust 32-slot pool. | rejected. Deliberate, exposed, tested tradeoff. |
| group-6: grok/q-auth-outcome-codes | question | 9. Operation identity, admission, and outcomes | Stable codes for auth/proof/capability rejects at headless API? | unresolved. |
| group-7: grok/q-progress-vocabulary | question | 9. Operation identity, admission, and outcomes | Non-terminal progress states not frozen. | unresolved. |
| group-8: grok/s-referenced-docs | suggestion | Further Notes | Inline needed vocabulary or state spec completeness. | candidate. Editorial. |

## Review spec-review:1:9

- Authorization: user `b` (approve round-8 resolutions and a ninth round). Cap 8→9; one generation, Standard implicit, no override, no automatic retry.
- Mode: spec; manifest exactly `.scratch/pi-integration/spec.md`, file-backed, ID spec, kind text. No addenda/glossary/notes/history.
- Pinned SHA-256: `676571f4a7f4d7db5e817c099a871c40c39c0cbe192f6e8f2d7873e7facaffd8`; 86,455 bytes; 428 lines; UTF-8 without control bytes; unique titles.
- Source change `pzvpspsnxvqxrqsylrxlwzmvzzuwyovm`; commit `301ffe0f9e408fe3ff0aa5f1f07ccf71f38592c9`. Base/Review base none. `.envrc` checksum unchanged.
- Citation convention: title-only sections, short exact evidence.
- Report ID: `5d260e2c-86a4-4a11-a13e-1553e6739b6d`. Status complete; exactly one generation.
- Pipeline gate: **pause**; reasons `unresolved-significant`; notes none. Fix sources 2, pause sources 1. Family coverage: complete.
- Scope integrity PASS: audit artifact file origin, SHA-256 `676571f4…affd8`, 86,455 bytes, 428 lines = pin = immutable source = live file after review. `.envrc` unchanged.
- Reviewer packet hash `8547f3042cc74ace33e321b3dd960b5d5367013d26082988488e827bb37774d3`; coordinator packet hash `db938afebc4307500c3607fba691e06ebda02590aed6a0cead7b0ac5c036b4a0`.
- Participants same models as round 8; outputs gpt 1499, claude 7101, grok 2505, coordinator 4028. All one attempt, normal stop, exit 0, cleanupConfirmed, toolCount 0. All 9 citations matched; warnings empty; nine singleton groups, no merges, no dissent.
- Inspection: formatted report in full; raw page at 118000 (artifact audit, all attempts, reviewer raw texts). No export, no retry.

### Round-9 original sources and assessments

| Group / source | Original classification | Section | Claim | Effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 12. Revision-pinned rendering | NaN alpha conversion undefined (RGB has NaN→0, alpha does not). | substantiated. Real gap; likely intent NaN→0. **Fix source.** |
| group-2: gpt/finding-2 | question | 12. Revision-pinned rendering | Premultiplied 8-bit sRGB source: passthrough vs unpremultiply precedence and color space. | unresolved. Needs author clarification. |
| group-3: gpt/finding-3 | question | 6. Identity and revision semantics | Do returned parameter-value changes invalidate inspection cursors? | unresolved. Author intent needed. |
| group-4: claude/finding-1 | defect/medium | 9. Operation identity, admission, and outcomes | No frozen code for reused request_id with conflicting content; `conflict` is scoped to precondition failures. | unresolved. Adapter-mapping clause may cover it, but headless-seam code unstated. **Pause source.** |
| group-5: claude/finding-2 | question | 5. Execution, admission, and resource accounting | "necessary view resources" undefined. | unresolved. |
| group-6: claude/finding-3 | question | 10. Agent Edit Batches and downstream synchronization | batch_id entropy/bit length unstated. | rejected. Grants no authority; format/uniqueness testable; counter allowed. |
| group-7: claude/finding-4 | question | 5. Execution, admission, and resource accounting | Scope of 8 MiB aggregate scratch cap. | unresolved. Independence already stated; scope unconfirmed. |
| group-8: grok/finding-1 | defect/medium | 10. Agent Edit Batches and downstream synchronization | Outcome/state when failed scene's later edit hook confirms readiness. | substantiated. Likely intent: remain failed, applied_with_sync_failure. **Fix source.** |
| group-9: grok/finding-2 | question | 5. Execution, admission, and resource accounting | Which budget retained image bytes count against. | unresolved. |

### Human decision after round 9 — option (b) approved

- Human response: `b` (approve proposed resolutions and one tenth round). Applies to `spec-review:1:9`, report `5d260e2c-…`, scope `676571f4…`. Not a gate override.

| Round-9 source | Approved resolution (applied) |
|---|---|
| claude/finding-1 (defect/medium, pause source) | §9: new frozen code `request_id_conflict`; conflicting reuse never executes, retained record unchanged. Acceptance item 8. |
| gpt/finding-1 (defect/medium, fix) | §12: NaN alpha maps to 0. Acceptance item 10. |
| grok/finding-1 (defect/medium, fix) | §10: hook still runs once; even on confirmed readiness, a failed scene's edit/restore returns applied_with_sync_failure and stays failed until Resynchronize. Acceptance item 6. |
| gpt/finding-2 (question) | §12: passthrough only for straight-alpha 8-bit sRGB; premultiplied sRGB decoded to linear, unpremultiplied, re-encoded. Acceptance item 10. |
| gpt/finding-3 (question) | §6: returned parameter values included in registry/inspection revisions; any returned-field change invalidates cursors. Acceptance item 9. |
| claude/finding-2 (question) | §5: necessary view resources = target view's backend rendering resources held by canceled/timed-out work awaiting safe release. |
| claude/finding-4 (question) | §5 table: 8 MiB scratch is global across concurrently admitted discovery/inspection work. |
| grok/finding-2 (question) | §5 table: retained image bytes count only under 32 MiB retained images, excluded from 16 MiB activity budget. |
| claude/finding-3 (question, rejected) | No change. |

- Revised spec SHA-256 `65e3f2a271f7760715b576ebd56f9ca1417b8fa9aec2b76bab560b59418a9646`, 87,977 bytes, 428 lines, 72 stories, unique titles. Further Notes updated.

### Round-9 stop/recovery requirements

- Gate pause blocks automatic progress. 9/9 attempts consumed; further review needs explicit authorization. Human decisions must reference `spec-review:1:9`, scope `676571f4…`, and source IDs. No spec edits, tickets or implementation followed.

## Review spec-review:1:8

- Authorization: user `8th review round approved.` Raises initial spec-review cap 7→8; one generation, Standard selection implicit, no profile/reviewer override, no automatic retry. Prior seven attempts/outcomes remain counted and preserved.
- Mode: spec; Milestone 1 is the full document. Manifest: exactly `.scratch/pi-integration/spec.md`; no addenda (none exist), glossary, design notes, pipeline history or repository context.
- Attachment: ID spec, label/path `.scratch/pi-integration/spec.md`, kind text, file-backed (same exception as round 7).
- Pinned SHA-256: `da40b8dc62ee54486803f8c4cd3804c6275ce32ead0bd5bb7c58040883737680`; 84,405 bytes; 427 lines. Regular UTF-8, no binary control bytes, unique section titles.
- Source change ID: `lmpvymmnrsvtutuprpxyowtpqkrlpzop`; source commit ID: `ae2ec99c75329b5fed5deddf934bae29c0024ddd` (immutable bytes match pin).
- Base: none; Review base: none. `.envrc` checksum `df28ab50e13cef0cd853095c9257244e0d635c5311a20dc00915c20b00670fce` unchanged.
- Citation convention: title-only section text (no Markdown `#` markers), short exact contiguous evidence; no guessed line numbers.
- Report ID: `a8469604-ce96-4090-a18a-a226f6c84b19`; created `2026-09-28T19:37:44.003Z`. Status complete; exactly one generation.
- Pipeline gate: **pause**; reasons `unresolved-significant`; notes none. Source classification evaluated; fix sources 1, pause sources 1. Family coverage: complete (no lost families).
- Scope integrity PASS: audit artifact file origin `.scratch/pi-integration/spec.md`, SHA-256 `da40b8dc…7680`, 84,405 bytes, 427 lines = pin = immutable source commit = live file after review. `.envrc` unchanged. No working-copy changes since the pin commit.
- Task packet hash `2493943bee2bc89d09c49c2d13d410462a91ba3ebfe142fb822383a615bf6ba7`; coordinator packet hash `39d75282f7b88f2253f74735e0575c9e8953fecbacf35c2c52b59203410821c1`.
- Participants: gpt `openai/openai/eccn-gpt-6-sol` (output 1423), claude `us/azure/anthropic/eccn-claude-sonnet-5` (4069), grok `xai/xai/eccn-grok-4.6` (3885); coordinator `azure/anthropic/eccn-claude-opus-5-5` (4337; not a vote). Note: gpt and coordinator models differ from round 7. All one attempt, normal stop, exit 0, cleanupConfirmed, toolCount 0, generationLimit 1. Budgets 12000/20000, ample headroom.
- All reviewed the complete sole artifact only. All 8 citations matched (quote-matched); warnings empty. Eight singleton groups, no cross-group merges, no dissent.
- Inspection: formatted report in full; raw pages at 0, 23346, 112000, 130000 (remaining raw interval is artifact body text). No export, no retry.

### Round-8 original sources and assessments

| Group / source | Original classification | Section | Claim | Proposed → effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 12. Revision-pinned rendering | PNG/8-bit sRGB output required, but no transfer/exposure/tone-mapping or out-of-range policy for linear/HDR source buffers; adapters may produce different valid PNGs. | unresolved → unresolved. Gap textually real; the registered Image Pipeline (part of view revision) may already produce display-referred output. Needs its output contract; medium justified if not display-referred [0,1]. **Pause source.** |
| group-2: gpt/finding-2 | question | 10. Agent Edit Batches and downstream synchronization | Which views must sync acknowledgement cover when views register/unregister during the hook? | unresolved → unresolved. Legitimate question; registration lifecycle vs safe-point serialization unstated. |
| group-3: claude/finding-1 | defect/low | 9. Operation identity, admission, and outcomes | Frozen code `cancelled` vs prose "canceled". | substantiated → substantiated. Frozen literal authoritative; copy-error risk only. |
| group-4: claude/finding-2 | question | 8. Editable values and numeric conversion | Double-rounding fixture literal unverifiable by inspection. | rejected → rejected. Coordinator verified: 1+2^-24+2^-80 → binary64 1+2^-24 → tie → 1.0; direct gives 1+2^-23. |
| group-5: claude/finding-3 | question | 8. Editable values and numeric conversion | Why elevation [0,360] rather than half-range? | rejected → rejected. Explicitly intentional; testable. |
| group-6: grok/finding-descriptor-name-collision | defect/medium | 2. Private launch descriptor and publication | Mandated name `vsr-mcp-<launch_id>.json` vs "choose another fresh name or fail closed"; no alternate naming form. | substantiated → substantiated. Coordinator dissent: low practical impact (128-bit random id, same-account out of scope). Original medium retained → **fix source**. |
| group-7: grok/finding-resync-during-synchronizing | question | 10. Agent Edit Batches and downstream synchronization | May Resynchronize be admitted while synchronizing? | rejected → rejected. Shared host-serialized path; admitted if capacity allows, runs after hook. |
| group-8: grok/finding-multi-target-records | question | 9. Operation identity, admission, and outcomes | Which retained kinds are multi-target records? | unresolved → unresolved. Plausibly renders (view+scene) and discovery pages; per-kind target set not enumerated. |

### Human decision after round 8 — option (b) approved

- Human response: `b` to the presented options, i.e. approve the proposed resolutions and one ninth review round. Applies to `spec-review:1:8`, report `a8469604-ce96-4090-a18a-a226f6c84b19`, scope `da40b8dc…`. Not a gate override; original classifications/assessments retained.

| Round-8 source | Approved resolution (applied) |
|---|---|
| gpt/finding-1 (defect/medium, pause source) | Section 12: the view's Image Pipeline owns exposure/tone mapping and produces display-referred linear color; integration adds no tone mapping in v1. Linear channels clamp to [0,1] (NaN→0), sRGB transfer, round to nearest 8-bit; 8-bit sRGB input passes through; alpha clamps; premultiplied→straight, RGB 0 at alpha 0. Acceptance item 10 tests these. |
| grok/finding-descriptor-name-collision (defect/medium, fix source) | Section 2: pre-existing `vsr-mcp-<launch_id>.json` fails activation closed; no alternate name. Acceptance item 13 updated. |
| gpt/finding-2 (question) | Section 10: acknowledgement covers the scene's views registered at hook invocation; view (un)registration is host scene work serialized at safe points. Acceptance item 6 tests it. |
| grok/finding-multi-target-records (question) | Section 9: covered targets fixed per kind (inspect/edit/restore/resync → scene; render → view + its scene; discovery page → listed targets; lookup → looked-up record's targets). |
| claude/finding-1 (defect/low) | Section 9: `cancelled` marked as exact spelling despite prose "canceled". |
| claude/finding-2, claude/finding-3, grok/finding-resync-during-synchronizing | Rejected by coordinator; no change. |

- Revised spec: SHA-256 `676571f4a7f4d7db5e817c099a871c40c39c0cbe192f6e8f2d7873e7facaffd8`, 86,455 bytes, 428 lines, 72 stories, unique titles. Further Notes updated. CONTEXT.md unchanged.

### Round-8 stop/recovery requirements

- Gate pause blocks automatic progress. 8/8 attempts consumed; any further review needs explicit authorization.
- Human decisions needed, referencing `spec-review:1:8`, scope `da40b8dc…`, and source IDs: gpt/finding-1 (image color mapping policy, pause source), grok/finding-descriptor-name-collision (medium fix source), plus optional handling of gpt/finding-2, grok/finding-multi-target-records (unresolved questions) and claude/finding-1 (low).
- No spec edits, tickets, addenda or implementation followed this outcome.

## Review spec-review:1:7

- Authorization: user `Authorized` to round 7 of 7, default Standard panel, exact file-backed spec with hash verification, title-only citation locators excluding Markdown markers, and no automatic retry or implementation afterward. This permits the file-backed exception to the inline-attachment skill rule for this round. All six previous attempts/outcomes remain counted and preserved.
- Status: complete Standard review/synthesis from exactly one generation; tool gate fix. This consumes round 7 of 7. No configuration change, profile escalation, external reviewer, scope reduction, raw export, automatic retry or implementation.
- Mode: spec; Focus Milestone 1 is the full document. Manifest: exactly `.scratch/pi-integration/spec.md`. No addenda, glossary, design notes, pipeline history, repository exploration or ambient context attached.
- Attachment: ID spec, label/path `.scratch/pi-integration/spec.md`, kind text, workspace-relative file-backed input. Native tool freezes the complete named file. Do not copy/reconstruct its body manually.
- Pinned SHA-256: `5646e6df8fde031c2cc63e7f747e5220bd9a3102f8cde5b89d457529b0f39834`; 78,945 bytes; 424 lines; 72 user stories.
- Source change ID: `wrksoswuyoxzwxkzkovkullotyuxlpqu`
- Source commit ID: `54f564ad29aa1c9f5eea800faa561c1baeee21a1`
- Base: none; Review base: none. Regular UTF-8 file without binary controls verified identical to immutable source commit; all actual Markdown titles unique. No source mutation during review. Existing .envrc checksum `df28ab50e13cef0cd853095c9257244e0d635c5311a20dc00915c20b00670fce` preserved independently.
- Citation convention: section is the EXACT TITLE TEXT, never the Markdown heading line. Example valid location: artifactId spec, section `10. Agent Edit Batches and downstream synchronization`. Invalid section: `### 10. Agent Edit Batches and downstream synchronization`. Further Notes is `Further Notes`, not `## Further Notes`. Supply title-only metadata extracted from this file and short exact contiguous evidence within the selected section; no guessed line numbers, generic scope, path/diffSide locators, or invented missing text. This instructs valid input/output; it does not change the checker or repair old reports.
- Objective: completeness, ambiguity, contradictions, testability and scope boundaries of the full self-contained contract. Technical-validation/testing sections describe requirements, not performed evidence. Preserve original source IDs/classifications/severities, uncertainty and dissent; keep distinct locations/classifications/severities separate rather than cross-group merging. Questions/suggestions remain distinct from defects. Concise complete output within budgets.
- Selection: Standard implicit, no profile/reviewer override. Inspect native effective settings, all participants/coverage/cleanup, all candidates and proposed/effective assessments. Verify audited artifact hash against this pin and live/immutable bytes before using the result.
- Report ID: `9a579efb-8662-4949-84c8-81e914002a23`; created `2026-09-22T16:59:55.428Z`.
- Tool status complete; pipeline gate fix; reasons none; notes none. Source classification evaluated; fix sources 2, pause sources 0. Ten original retained findings, not ten confirmed defects.
- Scope integrity PASS: native audit artifact spec has file origin `.scratch/pi-integration/spec.md`, SHA-256 `5646e6df8fde031c2cc63e7f747e5220bd9a3102f8cde5b89d457529b0f39834`, 78,945 bytes, 424 lines. Matches the pin, immutable source commit and unchanged live file. .envrc checksum unchanged.
- Reviewer task packet hash: `bff40b8592ec1257cc7c779ff26256519f89026bd6af46856253bf2a947f97c8`.
- Coordinator packet hash: `a619c08f3ef8b537dbe3666211faf6f25a400ac31b3e4b20ecd3b59d9f22ba72`.
- Inspection: entire formatted report at offsets 0 and 23392; raw pages at 0, 123000 and 146392. All original sources/coverage, exact groups, coordinator proposed/effective assessments, artifact audit/settings, and all four cleanup records inspected without another generation/export.

### Round-7 coverage, citations, and severity handling

- Standard gpt/claude/grok each succeeded in one attempt; coordinator succeeded in one attempt. Models unchanged: GPT `us/openai/openai/eccn-gpt-5.6-sol`, Claude `us/azure/anthropic/eccn-claude-sonnet-5`, Grok `xai/xai/eccn-grok-4.6`, coordinator `us/azure/anthropic/eccn-claude-opus-5` (not another vote).
- Output budgets 12000 per reviewer, 20000 coordinator. Output usage gpt 2299, claude 10215, grok 1673, coordinator 7467. Normal stops; all four exit code zero, cleanupConfirmed true, toolCount zero, resourceMode none, generationLimit one. No cleanup/served-identity warnings. Coordinator headroom preflight 204785, actual 870431; no input/output-budget failure.
- All reviewed the complete sole artifact; no repository/referenced documents, previous reports, implementation or actual tests were consulted. Feasibility/acceptance text remains unproven requirements.
- All ten citation checks matched/quote-matched, warnings empty. The title-only examples resolved the earlier locator-format failure without changing the checker or rewriting historical reports. A match locates evidence, not proof of a claim.
- Ten originals: five defects (three medium, two low), four questions, one suggestion. Ten singleton groups and separate claims; no cross-group merges. Fresh-inspection defect and related question remain distinct because classification, section and asserted impact differ.
- Proposed/effective assessments agree: three substantiated (two defects and one suggestion), five rejected, two unresolved questions. No gate-level pause blocker. Coordinator argues the surviving defects have lower practical severity than their original medium ratings; preserve this dissent, but the pipeline uses original source severities and therefore still has TWO medium fix sources. Do not silently downgrade them to bypass fix.

### Round-7 original sources and dispositions

All rows concern the exact reviewed spec SHA-256 `5646e6df8fde031c2cc63e7f747e5220bd9a3102f8cde5b89d457529b0f39834`; all citations matched. Locations are title-only sections. Rejection/confirmation below is coordinator judgment, not new human authorization.

| Group / source | Original classification | Section | Claim | Proposed → effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 11. One-way Batch Restoration | Fresh-observation requirement has no defined server-enforced proof/token or explicit client-guidance interpretation, especially across sessions. | substantiated → substantiated. Binding wording conflicts with absent observable enforcement while handles/versions are not session-bound; independent version checks reduce practical impact but do not settle the requirement. Coordinator calls impact below medium; original medium remains actionable. |
| group-2: gpt/finding-2 | defect/medium | 12. Revision-pinned rendering | No terminal code for revisions changing after rendering starts. | rejected → rejected. Acceptance item 10 explicitly says relevant mid-render changes supersede, while success requires matching tokens and existing cancellation/deadline terminal rules apply. Minor coincident-event nuance does not sustain the finding as stated. |
| group-3: claude/finding-1 | defect/low | 9. Operation identity, admission, and outcomes | Admission serialization is allegedly ambiguous between send order and waiting for acknowledgement. | rejected → rejected. Adjacent instructions require waiting for terminal response without early acknowledgement and reject reliance on send order across connections. At most editorial tightening. |
| group-4: claude/finding-2 | defect/low | 10. Agent Edit Batches and downstream synchronization | batch_id entropy/generation algorithm and unguessability unspecified. | rejected → rejected. Unique-within-launch/no-reuse is testable; the reference grants no authority and current permissions/eligibility guard access. Algorithm is intentionally delegated, unlike authority-bearing credentials. |
| group-5: claude/finding-3 | question | 2. Private launch descriptor and publication | Criteria for the validated owner-controlled private runtime directory are not enumerated. | unresolved → unresolved. Modes, no-follow/exclusive creation, unsafe-parent rejection, opened-file checks and unsupported-platform behavior partly answer it; directory selection/ownership criteria versus deliberate platform-validation delegation remain author intent. Keep as question, not a fix defect. |
| group-6: claude/finding-4 | defect/medium | 3. Authentication, ownership, and transport | Sensitive status/results is undefined relative to minimal owner receipts, leaving session-proof scope unclear. | substantiated → substantiated. Undefined sensitive qualifier survives; coordinator rejects the stronger allegation that proof and scene-permission tiers conflict because receipts already belong to the authenticated owner. Coordinator proposes low impact; preserve original medium for fix. |
| group-7: claude/finding-5 | question | Confirmed primary seam | Behavioral seam allegedly conflicts with separate transport/gateway/technical-validation gates. | rejected → rejected. The spec explicitly composes headless business tests, distinct interoperability acceptance, thin non-duplicating carrier checks and viewer smoke tests. |
| group-8: grok/dir-equiv-vs-nocanon | question | 8. Editable values and numeric conversion | Are equivalent 0/360 representations distinct, and may out-of-range aliases pass? | rejected → rejected. Closed domain validation, no wrapping/canonicalization and explicit version changes for stored aliases already answer both; out-of-range rejects, 0 and 360 remain distinct admissible stored values. |
| group-9: grok/numeric-two-vs-four | suggestion | 8. Editable values and numeric conversion | Two-stage heading precedes four numbered rules; name the two conversion stages. | substantiated → substantiated. Editorial clarity only, not a promoted defect or changed numerical oracle. |
| group-10: grok/fresh-inspect-vs-cross-session | question | 4. Capabilities and logical-session lifetime | How fresh inspection interacts with old-session lookup/restoration and remembered observation tokens. | unresolved → unresolved. Explicit historical lookup/restoration largely resolve allowed operations, but token reuse after reinspection needs intent. Related to gpt/finding-1, not merged or promoted. |

### Resolution pass after round 7 — user-directed specification fixes

- Authorization: user `Please resolve the findings.` This permits resolving the unfrozen specification while the pipeline remains paused; it does not authorize another generation, ticket cutting, or implementation. The concrete resolutions below are agent decisions under that request, not separately solicited human approvals or panel votes.
- Source report: `9a579efb-8662-4949-84c8-81e914002a23`, exact reviewed hash `5646e6df8fde031c2cc63e7f747e5220bd9a3102f8cde5b89d457529b0f39834`. Preserve original five-defect/four-question/one-suggestion classifications, all matched citations, coordinator judgments and lower-severity dissent. The original gate remains fix with two medium sources.
- Current revised spec SHA-256: `da40b8dc62ee54486803f8c4cd3804c6275ce32ead0bd5bb7c58040883737680`; 84,405 bytes; 427 lines; 72 user stories. This revision has not been reviewed. Historical citation locations and hashes do not migrate to these new bytes.
- Fresh inspection: keep it as a required supported-client workflow for new version-pinned edits/renders, not a new server observation-proof mechanism. New-session clients invalidate cached observations as current and inspect live targets. Server checks remain current proof/capabilities, valid identity, version/revision matching and restoration eligibility; unchanged launch-scoped tokens may recur and remain valid across session changes. Historical reads and journal-based restoration need no preceding inspection operation. This preserves stale-state protection and existing cross-session recovery without inventing session-bound observation tokens or per-client scene-observation storage.
- Status authentication: all operation-status/result requests require launch authentication plus current session proof, including minimal receipts and historical lookups. A minimal receipt is a narrowly defined own-work payload exemption from scene read permission, NOT an authentication exemption. Its allowlist covers operation correlation/kind, state/code, commit information and timing/cleanup/capacity metadata; scene-bearing fields, object/batch references, version/revision tokens, images, summaries and free-form diagnostics remain read-gated; the allowlisted own Operation Reference remains available. Foreign-session records retain all-target read checks. Coarse occupied attachment remains an attachment response, not a status API loophole.
- Directory question: state the minimum ownership/privacy/ancestry invariants on the opened actual directory. Require ownership by the effective OS account, sufficient owner access, no untrusted-account access through bits/ACLs, safe traversal and no substitutions/unsafe ancestry. A per-user OS location or explicit host path must pass the same checks; environment/path labels alone confer no trust, and unsafe existing paths cannot be repaired or bypassed via unchecked fallback. Exact names/platform APIs remain implementation choices under the existing validation gate; no platform safety experiment is claimed.
- Numeric suggestion: name the two representation conversions (JSON→binary64, validated binary64→binary32), with four parsing/validation/rounding/zero rules. Preserve all numerical rules, angle semantics and the verified double-rounding oracle byte-for-byte apart from that introductory sentence.

| Round-7 source | Resolution/disposition in this pass |
|---|---|
| gpt/finding-1 — defect/medium | Resolved in sections 4/9/11 plus public-API and client acceptance checks: explicit client refresh versus server validation, no additional observation-proof field. |
| gpt/finding-2 — defect/medium, rejected | No behavioral change. Retain coordinator rejection: acceptance item 10 already assigns superseded to relevant mid-render changes and existing terminal rules apply. |
| claude/finding-1 — defect/low, rejected | No change. Adjacent wait-for-ack/terminal and non-FIFO-across-connections rules already establish admission serialization. |
| claude/finding-2 — defect/low, rejected | No change. Unique-within-launch/no-reuse and no-authority requirements remain; generation algorithm is deliberately unconstrained, not an authorization secret. |
| claude/finding-3 — question | Clarified opened-directory ownership, privacy, ancestry and fail-closed invariants in section 2 and platform tests; exact names/APIs remain validated implementation choices. |
| claude/finding-4 — defect/medium | Resolved in sections 3/4 and authority tests: proof always required for operation-status/results; minimal own receipts have an explicit field boundary and only a read-capability exemption. |
| claude/finding-5 — question, rejected | No change. Primary headless business seam and thin interoperability/platform layers remain explicitly complementary. |
| grok/dir-equiv-vs-nocanon — question, rejected | No change to numerical domains, validation, no wrapping or stored-angle version behavior. |
| grok/numeric-two-vs-four — suggestion | Editorial clarification only: two conversions, four governing rules; original suggestion classification preserved. |
| grok/fresh-inspect-vs-cross-session — question | Resolved alongside, but not merged with, gpt/finding-1: old result lookup/restoration remain authorized by current rules; no prior-session observation provenance check, and supported-client refresh applies to newly formulated version-pinned work. |

- Validation passed: 72 unchanged user stories, template/14 sections, local links, unchanged unrelated contracts and numeric rules/oracle, explicit client/server freshness and proof/receipt boundaries, directory criteria, all ten source dispositions, original report/prior evidence preserved, hash/size/lines, 7/7 pause and unchanged .envrc. Both diffs inspected; tightened acceptance wording to exclude object/batch references and version tokens without excluding the allowed own Operation Reference.
- Updated only spec.md and this pipeline. CONTEXT.md and all production files remain unchanged. No native build/runtime/gateway/render tests, tickets, addenda, panel call, budget reset or clean-gate claim accompanies these edits.

### Round-7 stop/recovery requirements

- Exact scope and citations are now verified. The latest tool result is FIX, not pause and not clean. All prior reports remain unchanged; this valid review does not retroactively repair them.
- At the review stop, two surviving original medium sources required resolution: gpt/finding-1 and claude/finding-4. The later user-directed resolution pass is recorded above; its revised bytes have not been reviewed. Original associated questions/suggestion and classifications remain preserved rather than promoted or retroactively marked clean.
- The original scoped authorization was one review with no automatic retry or implementation afterward. No spec edit, ticket cutting, implementation, raw export, configuration change or further generation occurred in that review/outcome-recording phase. The later separate request to resolve findings authorizes the documentation changes recorded above, not a new review or implementation. At seven consumed attempts, any subsequent review requires explicit additional authorization; no cap reset or round 8 is inferred.
- Validation passed before the outcome commit: unchanged live/immutable-source spec and audited hash/size/lines; all ten original source mappings and original-severity FIX handling; round count/cap; byte-identical prior-review evidence; unchanged .envrc; no tickets/addenda. Documentation/provenance checks only, no runtime validation.
- Preserve original classifications and coordinator lower-severity dissent. Human decisions should reference this round, exact scope and source IDs. Ticket-format setup remains a later prerequisite, not permission to skip the fix gate.

## Review spec-review:1:6

- Authorization: user `yes` to one additional Standard review, increasing the initial allowance to six and using an exact file-backed attachment with hash verification instead of manual inline text. This explicitly permits the attachment-method exception to drive-spec; it does not authorize extra generations, configuration changes, or automatic external reviewers.
- Prior five attempts remain consumed, including invalid-scope round 5. Original intended hash `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0` and actual variant hash `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76` and its report/gates remain preserved below. A fresh valid outcome on the revised scope, not retroactive repair, is required for progress.
- Status: complete Standard review/synthesis from exactly one generation. Parent scope integrity PASS; tool gate pause. This consumes attempt 6 of 6, with no automatic replacement.
- Mode: spec; milestone 1 is the entire specification. Manifest: exactly `.scratch/pi-integration/spec.md`; no addenda, pipeline history, glossary, design notes, repository files or ambient context attached. Referenced feasibility checks are requirements, not performed validation.
- Attachment: ID `spec`, label `.scratch/pi-integration/spec.md`, kind `text`, path `.scratch/pi-integration/spec.md` (workspace-relative). No inline copy or manually reconstructed text. The tool freezes the named file; verify its audited hash against this pin and rehash the unchanged source after generation.
- Pinned SHA-256: `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`; 70,266 bytes; 417 lines; 72 stories.
- Source change ID: `xrmpvvxptsmwvowlrwssmtsutxruwotr`
- Source commit ID: `bfc2fb3d0392af401bcdbd463c05441b0dcd1975`
- Base: none; Review base: none. Preflight read-only verification: regular UTF-8 file without binary controls, bytes identical to the immutable source commit, all Markdown ATX titles unique. No source edits during this review.
- Citation instructions: use artifactId spec and a dedicated section locator containing the exact ATX title (including its numeric prefix where present), with short contiguous verbatim evidence located inside that section. Do not use generic scope, invented text, guessed line numbers or diff locators. Matching citations establish location, not correctness of a claim.
- Objective: full-spec completeness, ambiguity, contradictions, testability and scope boundaries. Preserve every original source ID/classification/severity/uncertainty. Distinct claims with different classifications, severities or locations remain distinct; related observations can be cross-referenced without cross-group merging or inventing agreement. Request concise complete output, not truncated findings.
- Selection: Standard implicit; omit profile and reviewers. No model/budget/configuration changes. Effective run settings and cleanup will be inspected from the tool audit.
- Report ID: `f3f7591c-395d-45e2-9da0-c772024b6ac8`; created `2026-09-22T13:53:13.789Z`.
- Tool status: complete. Pipeline gate: pause; reasons citation-unverified and unresolved-significant; notes none. Source classification evaluated; tool reports fix sources 0 and pause sources 12. These policy counts are not a tally of confirmed defects.
- Scope integrity: PASS. Audited artifact spec has file origin/path `.scratch/pi-integration/spec.md`, 70,266 bytes, 417 lines, SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`. It exactly matches the pinned immutable source and unchanged live file. No transcription variant or scope omission in this round.
- Reviewer task packet hash (same for all three): `262ad7d08fc56a986c08c904eb3149606380314668d51b9f5e320b218a950c27`.
- Coordinator packet hash: `e62c4ceea62036e8b8875caeb6e74a8c239edd09d73241786778c7579dd310c4`.
- Inspection: complete formatted report at offsets 0 and 23350, plus raw pages at 0, 57000, 100000, 123350 and 146700. All original findings, classifications, coverage, groups, proposed/effective assessments, audited artifact identity, generation settings and all four cleanup records inspected. No raw export or replacement generation.

### Round-6 participants, coverage, and citations

- Standard gpt/claude/grok, each successful in one attempt; coordinator also successful in one attempt. Models: GPT `us/openai/openai/eccn-gpt-5.6-sol`, Claude `us/azure/anthropic/eccn-claude-sonnet-5`, Grok `xai/xai/eccn-grok-4.6`, coordinator `us/azure/anthropic/eccn-claude-opus-5` (coordinator is not another vote).
- Audited output budgets: reviewers 12000 each, coordinator 20000. Output usage gpt 2673, claude 7707, grok 3592, coordinator 9705. Normal stop reasons; all four exit codes zero, cleanupConfirmed true, toolCount zero, resourceMode none, generationLimit one. No cleanup or served-identity warning reported. Coordinator headroom preflight 214898, actual 875855; no context/output-limit failure.
- All three report inspecting the complete sole artifact. No implementation, repository, glossary, design notes, pipeline history, test/gateway/rendering evidence was supplied or inspected. The tests and validation sections were treated as requirements, not passed checks. No implicit context expansion.
- Twelve original sources: seven defects (three medium, four low), four questions, one suggestion. Twelve singleton exact groups and twelve separately assessed claims; no cross-group merges. The two image-size claims concern related but different mechanisms at the same table row and remain separate.
- All 12 citation checks returned mismatched/unknown-section. Every submitted section string included Markdown heading markers (`### ...` or `## Further Notes`) instead of the title-only locator expected by the checker. This is a citation-locator format failure, NOT an attachment hash/scope failure. The prompt requested exact ATX titles but did not explicitly give title-only examples excluding the markers; do not silently alter the resulting report.
- The coordinator reports finding all quoted fragments verbatim, but that judgment does not change the checker results. Parent read-only checks independently located a distinctive quoted anchor from EACH source inside the intended title-only section; the table below records those anchor lines, not a repaired automated citation or independent proof of each full claim.
- Proposed assessments: four substantiated, six unresolved, two rejected. Effective: ten unresolved, two rejected; the four proposed-substantiated claims were downgraded because their citations were unverified. Original source classifications/severities remain unchanged. The coordinator's prose says none establishes a blocking hole; this does NOT override the policy pause.

### Round-6 original sources and assessments

All rows refer only to artifact spec, SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`. All original locators were mismatched/unknown-section. Section names below omit their submitted Markdown markers for human navigation only; anchor lines apply exclusively to this hash.

| Group / source | Original classification | Intended section / parent-located anchor line | Claim | Coordinator proposed → effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 9. Operation identity, admission, and outcomes / 289 | Pre-admission no-ID-consumption allegedly conflicts with inability to reuse a lower rejected ID after a higher admission. | rejected → rejected. Same-section high-water, gaps and serialized-admission rules determine outcome_unavailable once high-water passes the ID; rejection itself does not advance it. Possible editorial clarification, not a demonstrated contradiction. Original medium classification and uncertainty preserved. |
| group-2: gpt/finding-2 | defect/medium | 10. Agent Edit Batches and downstream synchronization / 306 | Ordinary edit/restore synchronization lacks explicit initiation, synchronizing-entry, confirmation and synchronous-versus-deferred outcome rules; only explicit recovery has a named host callback. | unresolved → unresolved. Some normal attempt is implied by mutation sync-failure outcomes, but the host's normal-path completion contract and whether synchronizing can outlive a successful mutation response are undecided in the supplied text. |
| group-3: gpt/finding-3 | defect/low | 5. Execution, admission, and resource accounting / 219 | Requested dimensions lack request-field/default/override rules and relationship to view-revision matching and canonical duplicates. | substantiated → unresolved (citation). Table calls dimensions requested while rendering names only view/tokens and dimensions belong to view revision. Coordinator considers this a real low contract gap; distinct from minimum-size validation. |
| group-4: claude/finding-1 | defect/low | 7. Bounded discovery and inspection / 249 | Unclear whether cursor continuation reuses request_id/slot across wire round trips or is fresh admission. | rejected → rejected. Intra-request safe-point slices are not page round trips; changed cursor arguments require a new ID and admitted IDs never execute again. Existing text determines behavior; explicitness may improve. |
| group-5: claude/finding-2 | defect/low | 3. Authentication, ownership, and transport / 169 | The phrase descriptor's session fields refers to fields the strict descriptor schema forbids. | substantiated → unresolved (citation). Coordinator treats it as editorial inconsistency; the no-session-state/schema rule remains stronger. Do not adopt source wording that treats the public session ID as secret or accidentally forbids the required launch credential in the descriptor. |
| group-6: claude/finding-3 | defect/low | 5. Execution, admission, and resource accounting / 219 | Requested dimensions have no positive floor/reject rule. | unresolved → unresolved. Coordinator conditions this on whether dimensions are client inputs or view-derived; render-request semantics are missing. Related to gpt/finding-3 but separate original mechanism. |
| group-7: claude/finding-4 | question | Further Notes / 414 | How does disclosed round-5 scope failure affect this ready-for-agent artifact? | unresolved → unresolved. Earlier reports and pipeline were excluded; coordinator notes ready-for-agent explicitly does not assert a clean gate. Do not turn this procedural context question into a new feature defect. |
| group-8: claude/finding-5 | question | 10. Agent Edit Batches and downstream synchronization / 306 | Coarse scene-wide blocking is deliberate and self-consistent; its usability is a product judgment. | substantiated → unresolved (citation). Kept as question/design observation, not defect. Parent already has explicit human approval in round-5 clarification 3; that outside-packet fact does not rewrite this report. |
| group-9: grok/finding-1 | defect/medium | 4. Capabilities and logical-session lifetime / 177 | Initial effective per-target grants/default read and their derivation from launch ceilings are unspecified. | unresolved → unresolved. Both starting at configured ceiling and starting empty/read-only can be read into the text; no explicit initial-mask rule disambiguates them. Distinct from the now-defined capability-to-operation matrix. |
| group-10: grok/finding-2 | question | 9. Operation identity, admission, and outcomes / 288 | Can a new session read status/results addressed by an older session's complete Operation Reference under current read permission? | unresolved → unresolved. Batch restoration across sessions is explicit, reinterpreting request numbers is prohibited, but foreign-session result lookup is not determined. |
| group-11: grok/finding-3 | question | 3. Authentication, ownership, and transport / 170 | Are IPv4-mapped loopback Host/bind forms rejected, including dual-stack listener behavior? | unresolved → unresolved. Exact authority/alternate-spelling rules plausibly cover Host; coordinator asks for explicit socket-level semantics. No live socket/platform validation occurred and its listener hypothesis is not verified evidence. |
| group-12: grok/finding-4 | suggestion | 9. Operation identity, admission, and outcomes / 295 | Add machine-readable finer conflict reasons distinguishing stale edits from restore mismatch. | substantiated → unresolved (citation). Optional refinement, not defect; caller knows operation kind and the contract already distinguishes retained restoration reasons while allowing additional result structure. |

### Human clarification 1 after round 6 — ordinary synchronization approved

- Human response: `yes` to the proposed normal-synchronization contract, resolving the design question in `gpt/finding-2` (defect/medium, original effective unresolved) from report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, reviewed spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`.
- After the complete atomic scene commit, record committed state and mark the scene synchronizing. Invoke the host's registered normal-synchronization hook once at the safe execution point. This hook owns the ordinary downstream notification/update; it is not a second notification or edit replay.
- Require synchronous host acknowledgement before terminal edit/restore success: confirmation that downstream state for every registered view is ready to render the committed scene. No image rendering or GPU convergence wait is required. A failure, exception or inability to confirm readiness leaves scene changes committed, marks synchronization failed, and returns applied_with_sync_failure. No terminal success-now/synchronize-later outcome in v1.
- While the hook runs, status may expose committed/synchronizing as progress, not terminal success. Cancellation/disconnect cannot turn an already-committed change into a reported non-commit; retain existing safe-completion rules.
- Ordinary synchronization is part of authorized edit/restore and does not require the separate resynchronize capability. Explicit recovery still does. A later incremental edit cannot silently clear an existing synchronization failure; explicit Resynchronize is required to establish whole-scene coherence again.
- Record approval now; apply with the subsequent agreed clarifications. The original report/classification/citation result remains unchanged; no gate waiver, new generation, or reset of the six spent attempts is authorized.

### Human clarification 2 after round 6 — initial grants and persistent host policy approved

- Human response: `Approved` to explicit per-target grants and persistent application-owned restrictions, resolving the design question in `grok/finding-1` (defect/medium, original effective unresolved), report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, reviewed spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`.
- The host explicitly configures each target's read/render/edit/resynchronize grants; omitted capabilities are denied, including read. Those grants are the ceiling and, without further host restrictions, become effective immediately on attachment. Attachment neither invents an automatic read-only profile nor starts an independently empty permission set.
- Current host restrictions narrow configured grants; unsupported/unexposed operations remain unavailable and the existing read prerequisites apply. A target configured read+render permits inspection/rendering immediately, not edit/resynchronize.
- Restrictions belong to the application, not the logical session. Disable agent edits persists across detach, expiry and replacement attachment; reconnecting cannot restore revoked permissions. Only the trusted host/local operator may explicitly relax restrictions within the ceiling, never an external client granting itself rights.
- Current policy is checked throughout the session at admission/execution, not cached permanently at attachment. Discovery exposes effective capabilities only for readable targets. A read+edit target narrowed by Disable agent edits stays read-only for the next attached client as well.
- Record approval now for subsequent spec/test updates. Original source classifications, citation failures, report gate and six spent attempts remain unchanged; this does not authorize a new review.

### Human clarification 3 after round 6 — view-owned render dimensions approved

- Human response: `Approved` to view-owned sizing with explicit refusal rather than automatic downsampling. Sources: `gpt/finding-3` and `claude/finding-3` (both defect/low) in report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`. Preserve original proposed/effective assessments and citation failures.
- V1 render requests have no per-request width/height override. Use the registered view's current dimensions, already covered by its view-revision token. Width/height must be positive integers within the configured integration limit, initially 1 through 1024 each.
- A resize changes the view revision; an old requested token returns superseded. Invalid or oversized dimensions reject before backend rendering. No silent clamp, downsampling or application-view resizing. A successful PNG has exactly the validated view dimensions.
- Rename Requested image dimensions to Integration render dimensions. The explicit usability trade-off is accepted: a larger native viewport cannot be captured through this API under the default limit; the host may expose a separate bounded capture view or the operator may resize the existing view.
- Record approval for subsequent normative/testing updates. No new review, gate override or implementation is authorized by this decision.

### Human clarification 4 after round 6 — cross-session read-only result lookup approved

- Human response: `Approved` to retained cross-session lookup under current read authority. Source: `grok/finding-2` (question, original effective unresolved), report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`.
- A currently authenticated session may supply an older complete Operation Reference to read its retained status/result, using current session proof rather than an old secret. Preserve original provenance; historical images are not fresh captures.
- Require current read permission for every target covered by that record. The minimal-status exception for a session's own work does not authorize access to another session's work. Multi-target records are all-or-nothing: deny without permission for the whole record rather than silently returning a modified partial result.
- Lookup grants no cancellation, replay or execution authority over old work. New scene work uses the current session's request-ID namespace; old numbers are never reinterpreted as new submissions. This read permission does not revive old ownership.
- Missing/evicted records return outcome_unavailable. Existing bounded retention and launch lifetime remain; no persistence or extra retention guarantee was approved.
- Record for subsequent spec/test updates. Original report/citations/classifications and six spent attempts remain unchanged; no new generation or gate waiver authorized.

### Human clarification 5 after round 6 — strict single-family transport approved

- Human response: `Yes` to explicit single-family listener/address rules. Source: `grok/finding-3` (question, original effective unresolved), report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`.
- Descriptor 127.0.0.1 selects an IPv4-only listener bound to that address. Descriptor ::1 selects an IPv6-only listener bound to that address, with IPv6-only socket behavior enforced before binding. No wildcard bind or automatic second-family listener.
- Reject IPv4-mapped forms such as ::ffff:127.0.0.1 in descriptors and Host authorities; they are not permitted aliases. If a platform cannot enforce and validate these guarantees, reject integration activation before descriptor publication.
- This is an explicit security contract, not evidence that binding ::1 necessarily accepts mapped IPv4 connections. No socket/platform experiment or implementation claim is made.
- Record for normative/acceptance updates. Preserve original classifications/citations/gate and six spent attempts; no review or configuration change is authorized.

### Human clarification 6 after round 6 — descriptor wording approved

- Human response: `Yes` to the exact secret-handling replacement, an editorial clarification without authentication/schema changes. Source: `claude/finding-2` (defect/low), report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`.
- Approved replacement: The launch credential appears in the private launch descriptor defined above; the session secret does not. The descriptor contains no session fields. Neither secret appears in URLs, model-visible arguments/results, activity, or ordinary logs. The public session ID is correlation data, not a secret.
- Preserve required launch_credential in the strict descriptor schema and the private trusted session-bootstrap path. Do not adopt reviewer wording that mistakenly treats the public session ID as secret or excludes the launch credential from its intended descriptor.
- Record for subsequent spec updates; original source/citation assessment and six spent review attempts remain unchanged. No new review or gate override authorized.

### Human clarification 7 after round 6 — retain conflict code with operation kind

- Human response: `Approved` to keeping one conflict code while explicitly identifying operation kind in retained status/results. Source: `grok/finding-4` (suggestion), report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, spec SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`. Do not promote this suggestion to a defect.
- Keep conflict as the stable v1 code for edit or restoration precondition conflicts; both indicate no scene mutation occurred. Retained status/results identify operation kind so clients, including authorized historical readers, can distinguish these cases without parsing English.
- Retain the existing restoration eligibility distinctions for conflicting, consumed/restored and expired records. Finer structured reasons may be added, but no new mandatory subcode taxonomy is required for clients.
- Record for subsequent spec/test updates. Preserve original report/citation results and six spent attempts; no gate waiver or further generation authorized.

### Human clarification 8 after round 6 — make existing ID/page rules explicit

- Human response: `Yes` to two clarifying statements, not a design change. Sources: `gpt/finding-1` (defect/medium, coordinator rejected) and `claude/finding-1` (defect/low, coordinator rejected), report `f3f7591c-395d-45e2-9da0-c772024b6ac8`, reviewed SHA-256 `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`.
- A pre-admission rejection does not advance high-water; the ID remains usable only while above it. Explicit example: 5 gets busy, 6 admits, retrying 5 returns outcome_unavailable and never executes. This preserves the existing admission/high-water contract rather than overriding it.
- Each continuation page is a fresh request with a new Operation Reference and admission slot. Intra-page cooperative slices retain that page's ID/slot. An identical same-ID retry replays the retained page result; it does not fetch another page. No continuation cursor alone holds a scene slot across client round trips.
- The last clarification also identified the remaining coarse-scene-scope and ready-for-agent observations as already answered by recorded decisions and the label-versus-gate distinction. No new feature change or fresh review authorization is implied.

### Applied revision after eight round-6 clarifications

- Applied the eight explicit approvals to spec.md and behavioral/thin-adapter acceptance requirements. No new domain term was needed, so CONTEXT.md is unchanged. This pipeline preserves all original sources and human decisions rather than rewriting coordinator/citation outcomes.
- Current spec SHA-256: `5646e6df8fde031c2cc63e7f747e5220bd9a3102f8cde5b89d457529b0f39834`; 78,945 bytes; 424 lines; 72 user stories. Predecessor/reviewed scope: `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88` in commit `bfc2fb3d0392af401bcdbd463c05441b0dcd1975`. No panel has reviewed the new revision; earlier locators/hashes stay bound to their historical artifacts.
- Normal edit/restore now has one host-owned ordinary synchronization hook and synchronous acknowledgement before terminal success, with no GPU convergence/image requirement, no duplicate notification, and no separate resynchronize grant. Failure/inability to confirm returns applied_with_sync_failure; committed progress stays truthful under cancellation. An earlier failure remains until explicit recovery.
- Current host policy derives from explicit per-target grants, deny-by-omission including read, and persistent application-owned restrictions. Attachment cannot reset Disable agent edits. Trusted host/operator changes alone can relax restrictions within the ceiling; discovery describes effective capabilities only for readable targets.
- Render dimensions belong to the revision-pinned view, not a request override. Require positive integers within configured bounds (initially 1..1024 per axis), exact PNG dimensions, and explicit rejection rather than resizing/downsampling. Cross-session retained lookup uses current proof and all-target read permission, original provenance, and no old mutation/cancellation authority or added retention guarantee.
- Transport explicitly binds one family and literal loopback address, rejects mapped Host/descriptor forms, and fails activation if guarantees cannot be enforced. Descriptor wording preserves its required launch credential while excluding all session fields/secrets. Retained results identify operation kind, with one conflict code and no mandatory subcode expansion. ID/page clarification examples preserve existing semantics.

| Round-6 original source | Human disposition / contract location |
|---|---|
| gpt/finding-1 | Clarification 8: existing high-water semantics made explicit in section 9 and tests; original medium/rejected record preserved. |
| gpt/finding-2 | Clarification 1: ordinary synchronization completion in section 10 and tests. |
| gpt/finding-3 | Clarification 3: view-owned size/schema/revision rules in sections 5/12 and tests. |
| claude/finding-1 | Clarification 8: fresh page admissions versus intra-page slices in section 7 and tests; original low/rejected record preserved. |
| claude/finding-2 | Clarification 6: exact secret/descriptor wording in section 3 and tests. |
| claude/finding-3 | Clarification 3: positive bounded view dimensions and pre-render refusal in sections 5/12 and tests. |
| claude/finding-4 | Existing Further Notes distinguish ready-for-agent from implementation/validation/review clearance; pipeline preserves historical scope failure and verified round-6 identity. Final clarification acknowledged this distinction; no new runtime requirement or gate waiver. |
| claude/finding-5 | Existing explicit human approval: round-5 clarification 3, reaffirmed as settled in final round-6 clarification. Scene-wide v1 blocking remains unchanged; no new usability decision inferred from the isolated review. |
| grok/finding-1 | Clarification 2: explicit grants and persistent current host policy in section 4 and tests. |
| grok/finding-2 | Clarification 4: read-only historical lookup in section 9 and tests. |
| grok/finding-3 | Clarification 5: single-family listener and mapped-address rules in section 3 and platform tests. |
| grok/finding-4 | Clarification 7: preserve suggestion classification; operation-kind-qualified conflict in section 9 and tests, no mandatory finer taxonomy. |

- Validation passed: 72 byte-identical user stories, specification template and 14 implementation sections, unrelated normative sections unchanged, implementation-section formatting restrictions, local links, all eight approvals and twelve source mappings, current hash/size/line count, original finding/prior-review records preserved, pause and 6/6 count unchanged, and .envrc unchanged. Spec and pipeline diffs inspected. No build/runtime/gateway/render acceptance or new panel generation ran; no tickets/addenda were created.
- No additional design question from this report is silently deferred; the independent review/citation recovery gate remains paused. These approvals and edits do not constitute a new clean review, retroactive citation repair, budget reset, ticket authorization, or implementation evidence. Frozen remains no; all six attempts remain consumed.

### Round-6 recovery state

- Parent integrity check passes for the exact current spec; round-5's different-packet failure remains historical and is not repeated here. No spec change was made during this review or its outcome recording; the later separately approved revision is recorded above. Existing .envrc also retains pre-dispatch SHA-256 `df28ab50e13cef0cd853095c9257244e0d635c5311a20dc00915c20b00670fce`.
- Tool pause is authoritative despite complete generation, located manual anchors, coordinator prose, and zero fix-source count. Do not cut tickets, implement, repair original locators, reclassify questions, or infer human approval of new/rejected claims.
- Validation passed before the outcome commit: unchanged file/immutable-source bytes and audited hash/size/lines; unchanged .envrc; twelve original source rows; recorded report identity/packet hashes/count/gate; byte-identical prior-review records; no tickets/addenda. These bookkeeping checks are not runtime acceptance evidence.
- Six attempts are consumed. No round 7, profile escalation, external reviewers, model/configuration changes or automatic rerun authorized. Read-only inspection remains possible without generation.
- Human recovery must address the source-linked citation/assessment issues and any actual contract decisions separately. The already-approved coarse scene scope need not be silently reopened merely because the isolated panel could not inspect its approval record. The missing tracker-format setup remains a later prerequisite if a clean eligible outcome is eventually obtained.

## Review spec-review:1:5

- User authorization: `Let's go` in response to the proposed final panel review after explicit approval and application of Q1–Q10.
- One Standard generation as the final permitted round 5 of 5. Four prior rounds remain consumed. No reviewer/profile override, scope reduction, gate weakening, or implementation authorization.
- Status: panel generation/synthesis complete, but INVALID for the pinned intended artifact because the parent accidentally altered the inline attachment. Tool gate also returned pause. This attempt consumes round 5; no automatic replacement.
- Mode: spec; milestone 1 is the full document. Manifest: exactly `.scratch/pi-integration/spec.md`; no addenda, pipeline history, repository files, or ambient instructions attached.
- Intended attachment: ID `spec`, label `.scratch/pi-integration/spec.md`, kind `text`, complete pinned bytes. The dispatched bytes did NOT match this intended scope; see the integrity failure below.
- Intended SHA-256: `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`
- Intended size: 60,673 bytes; 398 lines.
- Source change ID: `wtyssnnwlznzponzxppntvqypyuwtlmw`
- Source commit ID: `31eb3d3600d3abb40741fbf2a4612b6fe44370f4`
- Base: none; Review base: none.
- Deployed Standard roster/caps verified: gpt/claude/grok, 12000 generation tokens each; coordinator 20000. No configuration changes.
- Citation preparation: deployed contracts.ts explicitly accepts exactly one line/section/scope locator; checkText validates a dedicated section field against a unique ATX title. All actual section titles in this frozen spec were checked unique. Request location {artifactId: spec, section: exact title} with short contiguous exact evidence. Do not use generic scope, guessed line numbers, text-artifact path, or diffSide. This uses supported validation, not a checker change or a retroactive repair.
- Objective: completeness, contradictions, ambiguity, testability and scope boundaries of the supplied self-contained contract; feasibility gates are unproven requirements. Compact complete findings/synthesis with every original source/classification/severity/uncertainty retained. Keep differently classified/severity/location candidates separate rather than hide disagreement in a merged claim; related observations can be cross-referenced without another vote.
- Section-title metadata will be copied from the actual headings only; it adds no material beyond the single frozen artifact. Historical approvals/background are intentionally not review inputs or extra normative requirements.
- Report ID: `5f84707f-ceeb-4425-ae45-d276c8adccd1`
- Report created: `2026-09-22T11:12:13.096Z`
- Tool result status: `complete`; all participants and coordinator succeeded. This does not establish completion of the intended-scope review.
- Tool pipeline gate: `pause`; reasons `citation-unverified`, `unresolved-significant`; notes none. Source classification evaluated; fix sources 3, pause sources 2.
- Parent integrity gate: `pause — review-scope-mismatch`. Do not use this report as a review of the pinned file, despite unchanged substantive implementation sections.
- Actual audited attachment SHA-256: `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76`; 60,679 bytes; 398 lines; origin inline.
- Task packet hash: `0f24c3e105d6316d7bd6123bfb63e551165feba3b3dc557710777114886990dd`
- Coordinator packet hash: `f657bdbe505ac6d857b9818e5b2889a736492749d26df720db07e039c50fafe1`
- Effective audit generation caps: reviewers 12000, coordinator 20000. Coordinator preflight headroom 222751; actual headroom 890238. No token/input budget failure.
- Original candidates on the transmitted variant: 8 — 1 high defect, 4 medium defects, 3 questions. No suggestions originally reported. Keep original classifications even when the coordinator describes a rejected defect as an optional improvement.
- Inspection: full rendered pages at offsets 0 and 23352; targeted raw pages at offsets 0, 100000 and 123352. All original sources, coverage, original/proposed/effective assessments, audit hashes/caps and cleanup read. No replacement generation, automatic export, or source-file correction.

### Round-5 scope-integrity failure — parent transcription error

- Intended/pinned and current on-disk spec both remain SHA-256 `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`. Pinned commit `31eb3d3600d3abb40741fbf2a4612b6fe44370f4` was rehashed independently after the report and matches the live file.
- The parent accidentally inserted the six ASCII bytes `agent ` into user story 40 while manually assembling the inline tool argument. Intended phrase: `I want conflicting stale edits rejected`; transmitted phrase: `I want conflicting stale agent edits rejected`.
- A read-only in-memory reconstruction making exactly that one replacement from the live file hashes to the tool-audited `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76` and is exactly 60,679 bytes. This confirms the entire difference, not merely a suspected typo. No reconstruction file was written and the actual report was not exported.
- Implementation, testing, and all remaining sections are byte-identical to the pinned file. That observation may aid HUMAN interpretation, but is not permission to waive exact-scope requirements, relabel this a clean intended-file review, or change the file after the fact to fit the report.
- GPT and Grok explicitly noticed the objective/attachment mismatch; the coordinator correctly scoped its assessment to the supplied inline text. The error belongs to the parent packet construction, not those reviewers.
- The operator was informed immediately after confirmation. Do not silently retry, refund/reset a round, alter the reviewed file, or hide the failed integrity check.

### Round-5 participants, coverage, and assessments

- Standard gpt/claude/grok, one successful attempt each; coordinator successful in one attempt. Models unchanged: GPT `us/openai/openai/eccn-gpt-5.6-sol`, Claude `us/azure/anthropic/eccn-claude-sonnet-5`, Grok `xai/xai/eccn-grok-4.6`, coordinator `us/azure/anthropic/eccn-claude-opus-5`.
- Output usage: gpt 2437, claude 7637, grok 2317, coordinator 5946. All stop reasons normal. All four process audits report cleanup confirmed; no cleanup block/warning or served-identity note reported.
- All reviewed the full SUPPLIED variant, not an independently read repository file. No repository, background docs, previous reports, tests or gateway behavior were supplied. Validation gates remained requirements, not evidence. GPT/Grok coverage and coordinator synthesis explicitly disclose the mismatched artifact identity.
- Dedicated section citations worked for 7 of 8 sources. Claude/finding-1 selected section 5, but its exact quote is in section 4 (live/pinned line 180, unchanged in the variant). A read-only substring check confirmed that location. Original mismatch remains preserved and is not a claim dismissal.
- Eight exact singleton groups and eight separate coordinator claims; no cross-group merges. Effective assessments: 3 substantiated, 3 unresolved, 2 rejected. Rejection reasons and uncertainties are retained below; quote matches do not prove defects.

### Round-5 original findings — transmitted variant only

Every source below refers to actual attachment `spec`, SHA-256 `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76`, NOT the intended hash in the objective. The cited implementation text happens to be unchanged; no automated transfer of the gate to the pinned file is allowed.

| Group / source | Original classification | Section / citation | Original claim | Coordinator proposed → effective / reason |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/high | 11. One-way Batch Restoration / matched | No request-visible selector names the original edit batch to restore, distinct from the new restoration operation's own Operation Reference; cross-session and multiple-record selection may diverge. | substantiated → substantiated. Per-record eligibility/history requires a semantic selector. The committing edit's Operation Reference could be used, but the supplied text does not state that relation. Coordinator retains uncertainty about high severity, not the claim's existence. |
| group-2: gpt/finding-2 | defect/medium | 4. Capabilities and logical-session lifetime / matched | Capability semantics beyond explicit edit permission are not enumerated, leaving per-operation/per-target authorization and narrowing unclear. | unresolved → unresolved. Could instead intentionally mean one required edit axis plus host-selected exposure. Coordinator requests explicit enumeration or a statement of the sole required axis; no interpretation inferred. |
| group-3: gpt/finding-3 | defect/medium | 10. Agent Edit Batches and downstream synchronization / matched | The affected scene/view/downstream-resource set whose renders are blocked after sync failure is not defined for multi-view/scene hosts. | substantiated → substantiated. No normative dependency mapping fixes which renders must remain invalid versus eligible; distinct from mechanism/code questions. |
| group-4: claude/finding-1 | defect/medium | 5. Execution, admission, and resource accounting / mismatched | Control serviceability under load allegedly lacks a quantitative falsifiable latency/scheduling bound. | rejected → rejected, citation-unverified. Coordinator cites configured lease/cadence, no-starvation rules and renewal-under-saturation tests as objective criteria; another numeric scheduling bound is not required by the given text. Original medium classification and uncertainty preserved; its quote actually belongs to section 4. |
| group-5: claude/finding-2 | question | 4. Capabilities and logical-session lifetime / matched | Do multiple local actors share the 32 scene slots or have separate capacity, and is their concurrency bounded? | unresolved → unresolved. Shared execution ordering and no external session slot are explicit, but coordinator requests explicit slot-accounting scope. Remains a question, not a promoted defect. |
| group-6: claude/finding-3 | question | 9. Operation identity, admission, and outcomes / matched | Can intentionally skipped IDs below high-water later be submitted despite the out-of-order rule? | rejected → rejected. Existing categorical rules say unretained IDs at/below high-water never execute and 9 cannot admit after 10, already answering this question. |
| group-7: grok/finding-1 | defect/medium | 10. Agent Edit Batches and downstream synchronization / matched | Rendering's token-match rules do not explicitly enforce a synchronization-state admission/outcome gate; reject/delay/invalid-image behavior is unstated. | substantiated → substantiated. Coordinator sees the rendering section's lack of linkage/outcome as distinct from the affected-target scope. Whether existing busy/superseded or a new code is intended is unknown. |
| group-8: grok/finding-2 | question | 9. Operation identity, admission, and outcomes / matched | Which frozen outcome applies to render/inspection refused while sync is required/failed? | unresolved → unresolved. The current code list lacks a dedicated refusal code and mutation applied_with_sync_failure is not automatically a non-mutation outcome. Preserve question classification. |

### Human clarification 1 after round 5 — Batch Reference approved

- Human response: `yes` to the dedicated application-owned Batch Reference proposal.
- Related source: `gpt/finding-1` (defect/high) in variant report `5f84707f-ceeb-4425-ae45-d276c8adccd1`, actual reviewed SHA-256 `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76`. The unchanged intended working spec remains `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`.
- Decision: each committed Agent Edit Batch has an application-owned Batch Reference containing launch_id and a server-issued 32-character lowercase-hex batch_id unique within that launch and not reused after eviction. Publish it with the committed result/activity, including applied-with-synchronization-failure. Preserve the originating edit's Operation Reference as provenance rather than overload it as restoration-record identity.
- A restore carries its own current operation_ref plus target_batch_ref. Local viewer and external clients select the same retained record; local invocation still uses host-local operation correlation. The Batch Reference survives client disconnect while retained, not application restart, and grants no authority: current permission/eligibility checks apply. No originating session secret is required for authorized recovery under a new session.
- Unknown/expired/wrong-launch references report unavailable without selecting another record; a known consumed record reports already_restored. This avoids implicit most-recent-batch selection and keeps record identity independent of operation-result retention or origin.
- Record the decision now; apply together with the subsequent capability and synchronization decisions when agreed. This does not accept the altered review packet as the pinned artifact, clear the original gate, reset the five spent attempts, or authorize another generation.

### Human clarification 2 after round 5 — four capabilities approved

- Human response: `Yes` to four explicit capabilities applied to registered scenes/views.
- Related source: `gpt/finding-2` (defect/medium, effective unresolved) in report `5f84707f-ceeb-4425-ae45-d276c8adccd1`, actual reviewed variant SHA-256 `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76`; intended working spec remains `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`.
- Matrix: read permits target discovery/inspection and target activity/history/result payloads including retained images; render permits new renders; edit permits Agent Edit Batches and Batch Restorations; resynchronize permits rebuilding exposed downstream state.
- Render, edit and resynchronize require read on the relevant target. Enabling them never silently grants read; inconsistent launch configuration rejects. Registration/exposure and capability checks both apply. Discovery lists only readable targets. Withdrawing read makes dependent actions unauthorized and cancels newly unauthorized work under existing cancellation/commit rules.
- Owner controls remain available: renewal, cancellation of its own work, detach, and minimal operation-status receipts. Such receipts contain control/status information, not scene values or image payloads; payload access still requires read. Do not make loss of read authority prevent releasing control or learning whether an owned operation committed.
- Local viewer actions use host authority and the same target safety checks, not the external session's temporary capability mask. No additional external session or caller-controlled local privilege is introduced.
- Rationale: a small explicit operation-to-permission contract supports least privilege and the existing narrowing behavior without granting rendering, editing, or resynchronization implicitly from observation access.
- This records a human design decision only; it neither repairs the altered review packet nor changes its classifications, gate, spent attempts, or authorization for further generation. Apply with the remaining synchronization decision once agreed.

### Human clarification 3 after round 5 — scene-wide synchronization guard approved

- Human response: `Approve` to conservative scene-wide synchronization scope and explicit sync_required refusal.
- Sources: `gpt/finding-3` (defect/medium), `grok/finding-1` (defect/medium), and `grok/finding-2` (question) from report `5f84707f-ceeb-4425-ae45-d276c8adccd1`. Preserve original classifications and effective assessments; agreement does not rewrite the report.
- Source scope remains actual variant `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76`, not originally pinned `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`. Relevant implementation text was identical, but that is not exact-artifact review clearance.
- V1 registration binds each view to one registered scene. Track synchronization health per scene. Post-commit synchronization failure blocks all registered views of that scene, including otherwise-healthy sibling renderers/indices. Other scenes remain usable unless the host identifies their state as affected too. Per-index dependency graphs and finer partial-view recovery are deferred.
- Require readiness before starting backend rendering and before publishing a successful render outcome. Matching revision tokens are insufficient; non-ready scenes return sync_required with no image, no implicit recovery wait, automatic resynchronization/retry, or PNG merely labeled invalid. Existing revision/cancellation/deadline terminal rules remain in force.
- Committed-scene inspection, permitted history/status, and otherwise-authorized edits/restorations remain available. Resynchronize targets the scene and requires its read/resynchronize permissions; the host confirms downstream rendering state for all registered views matches the current scene before reporting ready. No edit replay or revision of the original commit outcome.
- This approval does not waive the scope mismatch/citation gate or authorize another review.

### Applied revision after the three human clarifications

- Updated spec.md (normative behavior and acceptance requirements), CONTEXT.md (Batch Reference vocabulary), and this decision/evidence record.
- Spec SHA-256 at this revision: `e03330d5fa1f2dcc2c702662cc45606f1ad9e0d614f81aae0e9785cea7d5a859`; 68,784 bytes; 415 lines; 72 user stories. Supersedes the on-disk pre-clarification hash `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0` for future work only. No review has run on this revision. Earlier current/on-disk statements and citation line maps describe their historical pre-revision snapshots, not this revised file.
- User stories remain byte-identical, including story 40 without the accidental extra agent word. This does not conform the source file to the wrong review attachment; substantive changes implement separate explicit approvals.
- Approved sources addressed: gpt/finding-1 (Batch Reference/selector), gpt/finding-2 (four capabilities), gpt/finding-3 (scene-wide scope), grok/finding-1 (render guard), and grok/finding-2 (sync_required; inspection stays available).
- Acceptance requirements now cover cross-session/local selectors, result-eviction independence, no reference reuse, permission narrowing and payload checks, unrelated-scene/healthy-sibling behavior, matching-token refusal, pre-publication invalidation, and scene-wide recovery. Existing rules remain consistent: cached completed images are historical, not new renders; logical failure does not permit unsafe backend destruction.
- At this revision, `claude/finding-2` remained an unresolved QUESTION about local integration actions sharing the 32-slot capacity. The subsequent explicit human decision is recorded below; original report classification/assessment is unchanged.
- `claude/finding-1` remains an originally medium defect rejected by the coordinator with its original citation mismatch. `claude/finding-3` remains a question rejected as answered by the high-water rules. These three approvals do not imply new endorsement of those dispositions or a gate override.
- Validation passed: specification template, all 72 user stories byte-identical to pinned source, 14 implementation sections, implementation-section formatting restrictions, local links, vocabulary, current hash/size/line count, all eight original source IDs and three human approvals, preserved pause/allowance and outstanding question, no tickets/addenda. The complete local diff was inspected. These are documentation checks, not implementation or behavioral evidence.
- Frozen remains no, phase paused, iterations zero, and five attempts spent. No ticket, addendum, implementation, native-parser/gateway experiment, rendering test, raw-report export or new panel generation accompanies this revision.

### Human clarification 4 after round 5 — shared scene-work capacity approved

- Human response: `Yes` to one shared 32-slot pool for all integration-submitted scene operations, including the stated local busy/cleanup trade-off.
- Source: `claude/finding-2` (question, effective unresolved) in report `5f84707f-ceeb-4425-ae45-d276c8adccd1`, actual reviewed variant SHA-256 `9802b6c49dc02e5a828c2de4af9d7db592912329297e71566c14da8e35998b76`. The review intended `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`; neither its scope failure nor original classification is rewritten.
- External clients, viewer windows and other in-process callers share the same admission capacity. Local Restore/Resynchronize consume slots; queued/running/yielded and cleanup-held work count. There is no separate or unbounded local queue and no local-authority capacity bypass.
- Ordinary application work outside the integration API is not charged to this pool, but safe scene/renderer access coordination remains required. Local controls, especially Disconnect/revoke, consume no scene slots and remain serviceable.
- At saturation, local Restore/Resynchronize may return busy before admission, including when cleanup holds every slot. Disconnect cannot force unsafe release or promise immediate recovery capacity. No guarantee of local reserved slots or forced backend progress was approved.
- Updated normative section 5, the resource table, acceptance coverage 11, and the specification's approval note. This is a human design clarification, not a new review or implementation evidence.
- Current spec SHA-256: `7ddd599bbf68da77960ef2f720a1657caae2b08148952fbb884192b7cbc45b88`; 70,266 bytes; 417 lines; 72 unchanged user stories. Predecessor: `e03330d5fa1f2dcc2c702662cc45606f1ad9e0d614f81aae0e9785cea7d5a859` in commit `540dbeb1`.
- All six non-rejected original sources now have explicit human design decisions: gpt/finding-1, gpt/finding-2, gpt/finding-3, grok/finding-1, grok/finding-2, and claude/finding-2. The two coordinator-rejected sources and original citation/scope gates remain preserved; no human gate override or new endorsement of those rejections is inferred.
- Validation passed: 72 unchanged stories, unchanged unrelated normative sections, template/links, shared-capacity rules and acceptance requirements, exact hash/size/line count, all four approvals/eight original sources, preserved pause/count, unchanged .envrc, and no tickets/addenda. Local diff inspected; documentation-only checks, not runtime/build/gateway/render evidence.
- Phase remains paused, Frozen no, five attempts spent. Another generation and the proposed file-backed attachment method still require explicit authorization. No tickets, addenda, production changes, or additional panel calls.

### Round-5 recovery requirements

- Parent scope-integrity failure independently prevents using this report as clearance. The tool also returned pause for citation-unverified and unresolved-significant. No spec fixes or ticket cutting are authorized by this result.
- Five allowed initial review attempts have been consumed, including this failed-scope attempt. No automatic round 6, counter reset, silent replacement, profile escalation or external reviewer substitution.
- Human recovery must explicitly address the packet mismatch and original gate/findings, naming intended and actual hashes and the original source IDs/rationale. Do not declare the six-byte difference harmless as a way to bypass scope verification.
- A safer future packet method would be the tool's explicitly scoped file-backed attachment plus pinned hash verification, avoiding manual transcription. The current drive-spec contract requires inline attachments, so adopting that method needs explicit user authorization rather than an unannounced workaround; another generation also needs an explicit allowance/replacement decision.
- At the review-outcome pause, no spec change, variant file, raw-report export, addendum, ticket, or implementation had been created. The later separately approved specification revision is recorded above and does not retroactively repair that outcome. Existing .envrc work remains untouched. Full ticket-format setup is still an independent later prerequisite.

## Review spec-review:1:4

- User authorization: `Good. Let's panel review the spec.` after consolidation of the approved design. One fresh Standard artifact-review generation; not a reassessment or override of old report bytes.
- Round 4 of the human-authorized 5-round initial spec-review allowance. Three rounds remain counted before this dispatch; no counter reset, reviewer override, or automatic implementation.
- Status: complete Standard review and synthesis. Gate pause takes precedence over the two fix sources. This consumes round 4; no automatic replacement generation.
- Scope mode: spec; milestone 1 is the full document.
- Scope manifest: exactly `.scratch/pi-integration/spec.md`; no addenda, pipeline state, repository context, or other files supplied to reviewers.
- Attachment: ID `spec`, label `.scratch/pi-integration/spec.md`, kind `text`, complete inline bytes.
- SHA-256: `5f6f8af1bc1e47527735c4237a9754a3e1a9688c2963584f98f43773f4e63924`
- Size: 53,545 bytes; 390 lines.
- Source change ID: `qkwtkzwpkzrmypvqmryrwozqtvpuvxnt`
- Source commit ID: `ee756f27cdfa1348561c7e8b5262b496d56914e4`
- Base: none; Review base: none.
- Deployed Standard roster checked: gpt/claude/grok; configured generation caps now 12000 per reviewer / 20000 coordinator. Current budget implementation independently bounds response bytes rather than scaling reservations indefinitely with token caps. No configuration changes made by this task.
- Citation preparation: read the deployed checkText locator logic. Text findings can use lineStart/lineEnd or a dedicated section field. A generic scope locator must itself be a unique literal substring included in evidence; the old heading-as-scope reports did not meet that condition. This packet requests lineStart/lineEnd plus short exact evidence, without path/diffSide/scope-only locators. No checker or gate policy is changed, and old reports retain their original unverified statuses.
- Objective: completeness, ambiguity, contradictions, testability, and scope boundaries of the consolidated spec. Pending technical gates are requirements, not proven behavior. Preserve original classifications/severities, uncertainty and dissent. Compact but complete findings/synthesis. Supply the exact section map below only as locator metadata, not additional review scope; if precise bullet lines are uncertain, use the enclosing range rather than guess.
- Report ID: `658eb1c7-5440-470f-8e8f-3ea19c41bc9f`
- Report created: `2026-09-21T22:35:10.342Z`
- Result: `complete`; all participants and coordinator succeeded. No generation error.
- Pipeline gate: `pause`; reasons `citation-unverified`, `contested-merge`; notes none.
- Gate source classification: evaluated; fix sources 2; pause sources 6. These are gate counters, not six confirmed defects.
- Original retained candidates: 13 — 2 medium defects, 10 questions, 1 suggestion. No high defect or risk was reported. Do not promote questions/suggestions into defects.
- Task packet hash: `7d6de7280929a8cf274223cb8a4a45b0a4845ff2d3caf9003d7c0cc2519b3c5e`
- Coordinator packet hash: `4917a7f5d5af4c88684f0bd93dd08d2975660f1dd0357c7f366c8e5ddddae6c9`
- Audited inline artifact: 53,545 bytes, 390 lines, SHA-256 exactly matches the frozen scope above.
- Effective output budgets confirmed in audit: 12000 for each reviewer; 20000 coordinator. Preflight coordinator headroom 230307; actual headroom 896298. No evidence trimming.
- Freshness: live spec and pinned source commit rehashed after review and both match the original `5f6f8af1bc1e47527735c4237a9754a3e1a9688c2963584f98f43773f4e63924`. Spec unchanged throughout the round.
- Inspection: complete rendered pages at offsets 0 and 23358; targeted raw pages at offsets 0, 100000, 123358, 146716. All original sources, coverage, coordinator reasons, effective assessments, audit identity/budgets, and cleanup inspected. Skipped raw spans duplicate source/answer text already read. No additional generation or raw export.

### Round-4 participants, citations, and assessments

- Standard gpt/claude/grok roster, one successful attempt each; coordinator successful in one attempt. Requested models: gpt `us/openai/openai/eccn-gpt-5.6-sol`, claude `us/azure/anthropic/eccn-claude-sonnet-5`, grok `xai/xai/eccn-grok-4.6`; coordinator `us/azure/anthropic/eccn-claude-opus-5`.
- Output usage: gpt 3956; claude 7703; grok 3815; coordinator 6481. All stopped normally. All four process audits report cleanup confirmed; no cleanup block/warning or served-identity note reported.
- Full supplied-spec coverage reported by all participants. No repository, referenced background documents, prior reports, implementation, gateway experiments or tests were supplied or inspected. The reviewers evaluated unproven requirements, not implementation evidence.
- Citation checks improved to 9 matched and 4 mismatched/quote-absent. All four mismatches are Grok line-range errors; the coordinator's summary calls them three but actually enumerates four. Preserve the audit count and original locations.
- Thirteen exact singleton groups; coordinator formed 11 claims by merging session-ID sources (group-3/group-7) and direction-domain sources (group-4/group-11). Session-ID merge crosses medium-defect versus question classifications and different locations; do not erase that disagreement. The direction merge retains a mismatched source despite another source's matched quote.
- Effective assessments: two substantiated claims, eight unresolved claims, one candidate suggestion. No claims rejected. Matched quotations locate evidence; they do not themselves establish the defect or resolve the intended scope. Pause remains mandatory under the reported gate even though fix sources are present.

### Round-4 original sources and coordinator disposition

All original lines refer to `spec` at the frozen round-4 hash. This table preserves original findings and assessments. Subsequent human clarifications are recorded below; they do not rewrite citations or constitute a clean gate override.

| Group / original source | Classification | Original lines / citation | Claim and uncertainty | Coordinator proposed → effective / rationale |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/medium | 192 / matched | No stable transport-independent identifier/correlation/lifetime/session-binding contract for canceling/querying non-mutation work (discovery, inspection, render, resync), while mutation IDs cover only edits/restores. | substantiated → substantiated. Control cancel/status is required; native MCP IDs cannot silently supply the missing public mapping. Protocol selection might eventually supply it, but its observable semantics are not stated. |
| group-2: gpt/finding-2 | question | 190 / matched | What if backend resources never become safely releasable: indefinite held capacity, degraded/faulted status, or recovery/escalation? | unresolved → unresolved. Unsafe teardown and extra unbounded pools are forbidden; busy-state observability exists, but permanent loss/recovery policy is not explicit. No release deadline can be inferred. |
| group-3: claude/finding-1 | defect/medium | 158–173 / matched | Public VSR-Session-ID header lacks format/length/encoding unlike the launch ID and credentials. Could be intended as a host-chosen opaque token. | substantiated → substantiated, merged with group-7. Coordinator sees wire asymmetry affecting interop but retains the medium-versus-question disagreement and possible implementation-choice interpretation. |
| group-4: claude/finding-2 | question | 241–265 / matched | Full-circle elevation [0,360] aliases directions and the +Y wording leaves intended wrap/canonicalization/version behavior unclear. | unresolved → unresolved, merged with group-11; effective reason citation-unverified because the merged Grok locator fails. Author intent remains open in this review, not a confirmed defect. |
| group-5: claude/finding-3 | question | 186–221 / matched | Is page size client-selectable within 64/default and 256/max, or wholly server-chosen? No request/negotiation rule is stated. | unresolved → unresolved. Could be an intended public selector or a deferred detail; no answer inferred. |
| group-6: claude/finding-4 | question | 174–185 / matched | Are concurrent local Restore/Disable actors supported, and how are they coordinated despite exemption from the external session slot? | unresolved → unresolved. Safe-point serialization mitigates partial-state risks; eligibility races/local-actor scope are not expressly discussed. |
| group-7: grok/sess-id-wire | question | 161–164 / matched | Public session ID has no length/alphabet/header-value contract. | substantiated → substantiated, merged with group-3. Preserve question classification rather than adopt Claude's medium severity for this source. |
| group-8: grok/error-surface | question | 176–177 / matched | Named outcomes lack stable HTTP/MCP encodings/payload shapes. Are bindings deferred to protocol selection? | unresolved → unresolved. Required behavior is named, but encoding deferral versus normative response normalization needs intent. |
| group-9: grok/viewer-disconnect | question | 313 / mismatched | Viewer disconnect is not explicitly equated with detach/whole-session revocation and terminal authority effects. | unresolved → unresolved (citation-unverified). Authority rules strongly suggest revocation; remaining issue may be terminology, not missing behavior. |
| group-10: grok/host-authority-bind | question | 169–170 / matched | Does the advertised address family have to match Host, or may the alternate loopback family be accepted? | unresolved → unresolved. Exact advertised-address/authority wording arguably forbids mismatches; coordinator requests explicit intent. |
| group-11: grok/direction-domain | question | 247–249 / mismatched | Closed elevation [0,360], aliases and wrap/gimbal behavior need intent confirmation. | unresolved → unresolved (citation-unverified), merged with group-4. The matched Claude quote does not repair this source's original locator. |
| group-12: grok/lease-config | question | 179–180 / mismatched | What happens when configured renewal is not earlier than expiry: reject, clamp, or unsupported? | unresolved → unresolved (citation-unverified). The invariant is present; the invalid-configuration enforcement surface is not stated. |
| group-13: grok/double-rounding-oracle | suggestion | 353 / mismatched | Name a concrete decimal for the known two-stage double-rounding test. | candidate → candidate (citation-unverified). Non-defect improvement suggestion only; no severity upgrade. |

### Round-4 parent locator verification — not a gate override

Using the unchanged hashed spec, a read-only exact-substring check found each mismatched quotation on a unique different line:

| Source | Reported lines | Actual quote line |
|---|---|---|
| grok/viewer-disconnect | 313 | 315 |
| grok/direction-domain | 247–249 | 251 |
| grok/lease-config | 179–180 | 178 |
| grok/double-rounding-oracle | 353 | 350 |

These observations preserve rather than rewrite the original report. They locate quoted text, not prove claims, dismissals, or merge validity. No new adjudication call was made.

### Round-4 human clarification round 1 — accepted

- Human response: `Looks good.` to the nine question-topic recommendations and the reuse of the already-verified numeric test example.
- Bound review: `spec-review:1:4`, report `658eb1c7-5440-470f-8e8f-3ea19c41bc9f`, scope SHA-256 `5f6f8af1bc1e47527735c4237a9754a3e1a9688c2963584f98f43773f4e63924`.
- These are human-selected clarifications, not a retroactive change to the original report, citation verdicts, classifications, or contested merge. They were recorded first; the linked operation-identity decision was subsequently approved below.

| Sources | Approved clarification | Rationale |
|---|---|---|
| gpt/finding-2 (question) | Resources and capacity may remain held indefinitely when safe release is impossible. Expose waiting-for-backend-cleanup state, elapsed waiting time and held capacity; report a fault when the backend actually reports one, not merely because it is slow. Control operations remain available. Operator-managed backend/application recovery may be needed; no forced destruction, automatic recovery, or cleanup deadline is promised. | Distinguish observable cleanup waiting from an unknowable prediction that a backend will never return, while preserving resource safety and the existing non-preemption boundary. |
| grok/sess-id-wire (question); claude/finding-1 (defect/medium) | Public session IDs are fresh server-generated 128-bit identifiers encoded as 32 lowercase hexadecimal characters. Clients echo them unchanged. They remain public correlation values, distinct from ownership secrets. | Close the wire contract consistently with the existing public launch-ID representation without making public identity an authorization credential. |
| claude/finding-3 (question) | Support optional page_size integer, defaulting to the configured default (initially 64), up to the configured maximum (initially 256). Reject invalid values rather than clamp. Byte/work caps can return fewer items. Page size is a budget, not query membership/order, and may change on continuation without alone invalidating the cursor. | Make the default/maximum meaningful to clients while keeping cursor progress independent of page-size choices. |
| claude/finding-4 (question) | Multiple in-process callers/windows may submit actions; local and external integration scene operations use the same host-serialized execution path, with permission/version/eligibility rechecks at execution. First successful restore consumes eligibility; a second cannot apply it again. Local authority bypasses only the external-session requirement, not safety checks; another process remains external. | Local controls must not create a concurrency bypass or an undocumented extra privileged network client. |
| grok/error-surface (question) | Stable machine-readable semantic outcome codes are required; clients must not parse English. Preserve occupied, busy, cursor_stale, superseded, outcome_unavailable, already_restored, item_too_large and committed-versus-uncommitted distinctions. The validated adapter may choose/document the HTTP/MCP carrier mapping. | Freeze observable meaning without prematurely inventing a second transport protocol; operation-correlation fields remain the next decision. |
| grok/viewer-disconnect (question) | Viewer Disconnect is local whole-session revocation: release ownership, invalidate secrets/cursors and cancel outstanding work without Pi cooperation. Already-committing edits finish safely; backend cleanup may continue. | State explicitly the already intended distinction between local disconnection and merely disabling agent edits. |
| grok/host-authority-bind (question) | Derive allowed Host authority from the descriptor's exact host and port. IPv4 and IPv6 loopback authorities are alternatives for different configured endpoints, not aliases for one another. | Clarify advertised-address binding rather than permit cross-family authority substitution. |
| grok/lease-config (question) | Reject integration activation before endpoint publication for non-positive/non-finite durations or renewal not earlier than expiry; do not clamp. Attachment communicates configured durations to trusted client code. The embedding host decides whether the application exits or continues with integration disabled after the error. | Enforce the configuration invariant and prevent a fixed 30-second client timer from ignoring a valid non-default server policy. |
| claude/finding-2; grok/direction-domain (questions) | Keep the deliberately chosen inclusive [0,360] range for both angles and the inspected VSR formula. Allow aliases but do not wrap or geometrically canonicalize before storage/version comparison; changed stored angles are parameter changes even when physical direction is equivalent. Clarify that increasing elevation from zero initially rotates toward +Y. | Preserve the existing VSR convention and approved numeric rules rather than silently replace the elevation domain. |
| grok/double-rounding-oracle (suggestion) | Reuse the previously verified irradiance input 1 + 2^-24 + 2^-80 (exact decimal already recorded in advisory 2); expected stored value under the selected two-stage rule is 1.0. | A checked mathematical fixture is available; using irradiance keeps the >1 input within its allowed domain. This is not a new numeric policy. |

- The separate linked medium finding `gpt/finding-1` was not inferred resolved by the nine approvals; it received the explicit follow-up approval below.
- Review count remains 4/5 and Last review remains the original pause; no panel call or implementation is authorized by these design approvals.

### Round-4 human clarification round 2 — unified operation identity accepted

- Human response: `Yes` to Q10's explicit extension of the existing request-ID scheme to all externally submitted scene work.
- Source: `gpt/finding-1` (defect/medium), review `spec-review:1:4`, report `658eb1c7-5440-470f-8e8f-3ea19c41bc9f`, original scope SHA-256 `5f6f8af1bc1e47527735c4237a9754a3e1a9688c2963584f98f43773f4e63924`.
- Decision: discovery, inspection, rendering, edits, restoration and resynchronization share the live external session's monotonically increasing client-selected request counter. The complete operation reference is the originating public session ID plus request ID, known before submission. Cancellation, status/results and activity use that qualified reference and current authority checks; transport reconnect preserves identity, and new attachment cannot reinterpret an old number under a new session.
- Duplicate protection extends to non-mutation work: retained retries return the original status/result; evicted old IDs never execute again. Repeating a completed render's ID does not render a fresh frame. Clients serialize admissions across all scene operations, not necessarily completion when an early admission acknowledgement exists.
- Renewal, cancel, detach and status queries remain control operations and do not consume this scene-request counter. Local actions retain separate host-local correlation, not an invented external session.
- Early-cancel boundary: cancellation of a reference not yet admitted cannot claim confirmed cancellation; the original request may still arrive. Whole-session revocation prevents that session's delayed requests from later admission. Unknown status never grants permission for blind renumbering/replay.
- Rationale: one qualified identity scheme supports reconnect-safe control and lost-admission-response lookup without adding a second server-handle scheme. This deliberately broadens the prior mutation-only numbering contract and its admission-order constraint.
- No retrospective severity/merge/citation changes or clean-gate override is implied. The approved documentation amendment is recorded below.

### Applied round-4 clarification revision

- User approvals: `Looks good.` for Q1–Q9, followed by `Yes` for Q10's deliberately broader all-scene-operation identity scheme. Together these cover all 13 original sources, preserving their original classifications and the fact that overlapping sources were not independent extra votes.
- Revised spec SHA-256: `ae8dddcecfdef43fe34c05dca09d9fc188b30c806a710bee83fea3ab90b7c0e0`; 60,673 bytes; 398 lines; 72 user stories.
- Updated: cleanup-wait visibility and indefinite resource holding; 128-bit/32-lowercase-hex public session IDs; optional page_size with default/max validation and cursor-budget distinction; serialized concurrent local actions; machine-readable semantic outcomes with validated carrier mapping; viewer Disconnect as local whole-session revocation; descriptor-family-exact Host validation; fail-closed invalid lease configuration and advertised actual durations; intentional non-canonicalized full-circle angles; concrete double-rounding fixture; and qualified operation references/duplicate protection across every external scene operation.
- Operation Reference wire value combines session_id and request_id. New submissions must match the authenticated current session; correlation is not authority. Control calls do not consume the scene counter, local actions retain local correlation, old references cannot be silently retargeted, cached read/render outcomes are not fresh work, and cancellation of not-yet-admitted work cannot falsely report success.
- Acceptance tests now cover all selected clarifications, including reference lookup/cancel over reconnect, non-mutation duplicate replay, exhaustion of the all-scene counter, page-size changes, concurrent local Restore, long-lived cleanup waiting, invalid configuration, address-family mismatch, exact session-ID form, outcome mapping, and the numeric fixture. Glossary defines Operation Reference.
- Verification: exact requested template, 72 sequential stories, local links, implementation-section source-path/snippet restrictions, all ten clarification anchors, absence of mutation-only numbering remnants, and the unchanged review-state fields. A read-only exact-rational check confirmed the pasted irradiance literal equals 1 + 2^-24 + 2^-80 and the selected two-stage binary32 result is 1.0.
- The original round-4 report remains bound to its unchanged original `5f6f8...` artifact. No new review, gateway/native-parser experiment, VSR build/test, ticket, addendum, or production implementation ran. The revised bytes require fresh panel review; the gate has not been overridden.
- One of the five allowed initial spec-review rounds remains. Full ticket-format setup and preservation of the pre-existing `.envrc` change remain independent prerequisites before ticket cutting/draining.

### Round-4 recovery requirements

- Gate pause won over fix sources at the original review outcome. Subsequent explicit human approvals authorized the documented spec/glossary/test-contract amendments above, not a retrospective clean verdict. No tickets, implementation, or additional review have run.
- Four of five initial spec-review rounds are consumed; one remains. Do not regenerate round 4 or reset the count. The next scope must be captured and its dispatch recorded before any authorized generation.
- Human decisions are bound above to `spec-review:1:4`, its scope hash and source IDs, including operation-correlation and session-ID wire decisions. Original classifications, disagreements, locator failures and gate are retained. These design approvals do not retroactively rewrite the report or weaken citation/merge checks for the next review.
- The other ten original items are questions and the final item a suggestion, not additional high/medium defects. Some ask to clarify already-selected behavior; do not reopen it or invent missing intent automatically.
- Full ticket-format configuration and preservation of the unrelated `.envrc` change remain prerequisites before cutting/draining tickets. Review completion is not implementation authorization.

### Round-4 frozen section line map

All ranges are 1-based and inclusive, computed from the unchanged attachment; boundaries include the final blank line before the next peer section where present.

| Lines | Section |
|---|---|
| 9–16 | Problem Statement |
| 17–26 | Solution |
| 27–121 | User Stories |
| 29–43 | Application integration and attachment |
| 44–58 | Authority and session lifetime |
| 59–70 | Inspection and observation |
| 71–84 | Editing, numerical values, and request identity |
| 85–95 | Restoration and synchronization |
| 96–110 | Rendering, cancellation, and feedback |
| 111–121 | Resource safety, deployment, and verification |
| 122–328 | Implementation Decisions |
| 124–133 | 1. Framework boundary and host integration |
| 134–157 | 2. Private launch descriptor and publication |
| 158–173 | 3. Authentication, ownership, and transport |
| 174–185 | 4. Capabilities and logical-session lifetime |
| 186–221 | 5. Execution, admission, and resource accounting |
| 222–230 | 6. Identity and revision semantics |
| 231–240 | 7. Bounded discovery and inspection |
| 241–265 | 8. Editable values and numeric conversion |
| 266–275 | 9. Mutation request identity and admission |
| 276–287 | 10. Agent Edit Batches and downstream synchronization |
| 288–299 | 11. One-way Batch Restoration |
| 300–308 | 12. Revision-pinned rendering |
| 309–316 | 13. Activity and viewer feedback |
| 317–328 | 14. Required technical validation |
| 329–368 | Testing Decisions |
| 331–336 | Confirmed primary seam |
| 337–343 | Existing infrastructure and prior art |
| 344–359 | Behavioral acceptance coverage |
| 360–368 | Thin adapter checks and end-to-end acceptance |
| 369–382 | Out of Scope |
| 383–390 | Further Notes |

## Consolidated specification — to-spec

- User requested `/to-spec` synthesis after approving both advisory-informed design packages. No further interview was conducted; the already-confirmed public headless integration API seam was reused.
- Artifact: `.scratch/pi-integration/spec.md`, local-markdown issue tracker, `ready-for-agent` status/label. No remote publication.
- Input spec SHA-256: `e36554855bd41595781a36860157d3fb274a35d2f9a6a4e75be8bbd09ecb0bce` (70,606 bytes).
- Consolidated spec SHA-256: `5f6f8af1bc1e47527735c4237a9754a3e1a9688c2963584f98f43773f4e63924` (53,545 bytes; 390 lines; 72 user stories).
- Consolidation reorganizes the approved contract, removes repeated review-history prose from the normative artifact, and adds user stories for already-approved ownership, pagination, numerical, recovery, admission, and publication behavior. No new product decisions or milestones were introduced.
- Fourteen implementation sections cover: framework/host boundary; descriptor publication/schema; authentication/transport; authority/session lifetime; scene/control work and limits; identity/revisions; bounded paging; edit surface/numerics; mutation admission/IDs; atomic edit/synchronization; one-way restoration; revision-pinned images; feedback; required technical validation.
- Preserved sensitive choices include 32 total scene/8 control requests, session proof distinct from launch authentication, unchanged numeric defaults and binary64-first validation, safe-integer IDs and no blind replay, conditional one-way restore, revision-sensitive paging, no arbitrary-C++ preemption, direct-only HTTP and descriptor safety, and private gateway bootstrap as unproven validation work.
- Testing remains one primary headless behavioral seam with thin MCP/Pi, rendering, viewer, and build-configuration checks. Existing Catch2/CTest, Scene/Object/Parameter, archive, RenderIndex/helide, and project-transaction testing patterns remain prior art, not proofs of the new guarantees.
- The existing workspace build/install instruction is explained in terms of installed-library search precedence; no CI job, provisioning implementation, or successful test run is invented. Required rendering acceptance must actually render with a usable device.
- Verification: exact requested top-level template, 72 sequential correctly formatted stories, local reference links, ready-for-agent metadata, implementation-section source-path/snippet restriction, approved contract/value marker inventory, and unchanged single-milestone shape. Reviewed old and consolidated contracts for coverage.
- This is documentation synthesis, not a review result. The old reports remain bound to their original hashes; no panel has reviewed these new bytes. Phase remains paused, 3/5 review rounds consumed, Last review unchanged, Frozen no, no tickets/addenda/implementation.

## Advisory consultation: round-3 decision support 2

- User request: `Can you get the panel to discuss those points?`
- Workflow: one `panel_consult` generation, Standard selection implicit. Advisory decision support, not spec-review round 4 or adjudication of citation/gate status.
- Status: advisory consultation returned degraded; useful GPT/Claude inputs and coordinator synthesis retained, Grok invalid-response. No automatic replacement generation.
- Scope: post-restore record lifecycle; JSON-to-float32 conversion; client mutation-ID numbering/overflow; minimal descriptor/authentication wire formats; operation-queue versus control-path accounting.
- Related original round-3 sources: `gpt/finding-4`; `grok/spec-float32-json`; `claude/finding-1` and `grok/spec-mutation-id-start-wrap`; `grok/spec-descriptor-auth-contract`; `grok/spec-queue-slot-scope`. Loopback-address forms may be addressed only as part of the requested wire contract, not a separate scope expansion.
- Original review: `spec-review:1:3`, report `e721516f-62d7-40ba-b535-ff946a2b7491`, scope SHA-256 `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`.
- Approved working-spec SHA-256 at consultation input: `a9099c06196bc885a73a7dac0f7d6d6364d0cae4122a3331d645288defaa1fff`; contained the separately approved session-secret, inspection, and descriptor-safety contracts. The later approved revision is recorded below.
- Inputs: explicit decision question with relevant approved constraints and unresolved choices; no attachments, repository exploration, credentials, ambient files, or parent instructions.
- Requested output: one coherent minimal-v1 recommendation per choice, strongest alternative/trade-off, observable error/state rules, and deterministic tests, preserving dissent and labeling any proposed change to an agreed constraint. No implementation or claim of empirically validated gateway behavior.
- Report ID: `af923f2a-ac5f-4c73-b397-34972a5958c1`
- Result: `degraded`. GPT and Claude succeeded; Grok failed response validation (`response.options: at least two alternatives required`). Coordinator synthesis is available. Do not describe this as three-family agreement or substitute another reviewer.
- Recommendation/coverage inspection: complete rendered pages at offsets 0, 24000, 48000, including both valid original perspectives, alternatives, uncertainty, tests, coordinator reasoning and disagreement. No cleanup warning was reported in rendered output; no independent process-cleanup audit is asserted. No raw export, replacement generation, or spec review occurred.

### Advisory 2 — agreement, dissent, and checked errors

- GPT/Claude favor one-way restoration, current-session client-generated monotonic IDs with server admission high-water accounting, a small private JSON descriptor, and separate bounded scene/control paths. Numeric domain-check ordering and ID wire encoding differ materially.
- Numeric disagreement: GPT recommends exact-decimal token range validation before direct correctly rounded binary32 conversion; Claude prefers parsed binary64 then binary32 rounding followed by domain checks, accepting values just outside the declared domain that round into it. Claude also describes strict binary64-domain-before-cast as an alternative. No unanimous numeric rule exists.
- ID disagreement: GPT chooses JSON safe-integer IDs through 2^53−1; Claude chooses canonical decimal strings for uint64. Both choose client generation, gaps, atomic server admission, no ID consumption on pre-admission failure, no re-execution, and no wrap. A lease can be renewed indefinitely; neither range choice should be justified by falsely treating the 120-second TTL as a maximum total session lifetime.
- Do not adopt Claude's launch-auth-on-attach-only suggestion: it contradicts the approved every-MCP-request launch-authentication requirement. Keep launch authentication alongside the session proof.
- Do not silently adopt GPT's no-eviction-of-eligible-restoration-records suggestion: the approved bounded-retention policy already allows visible oldest-eligible eviction. Preserve that policy and reserve necessary storage before commit.
- Both consultations count running/yielded work within the 32 scene slots, whereas the earlier text calls the limit pending-only. Treat that as a proposed tightening requiring human approval, not an unchanged restatement. GPT's separate retained-backend pool is an alternative, not a requirement already approved.
- The coordinator asserted decimal→binary64→binary32 double rounding is harmless under a precision inequality, explicitly requesting re-derivation. A local read-only exact-rational check disproved the blanket claim: x = 1 + 2^-24 + 2^-80 has exact decimal 1.00000005960464477539062582718061255302767487140869206996285356581211090087890625. Its nearest binary32 is 1.00000011920928955078125, but conversion through binary64 then binary32 produced 1. Do not rely on the coordinator's assertion.
- The same local check showed binary32(-1e-18) = -1.000000045813705e-18, not zero; Claude's example treating it as underflow-to-zero is incorrect. These were arithmetic checks only, not VSR implementation, parser conformance, or gateway experiments.
- Increasing mutation IDs in send order alone does not guarantee admission order across multiple connections. Recommend serializing admission acknowledgements (or completion if no early acknowledgement exists), not assuming transport send order suffices.

### Advisory 2 — human-approved parent synthesis

The user approved the presented consolidated package, including the parent's binary64-first alternative and explicit queue-limit change; this does not adopt every participant proposal or turn the degraded consultation into a clean review.

1. Restoration: one-way in v1. Preparation failure changes neither values/versions nor eligibility. At restore commit, consume original eligibility even if downstream synchronization fails; create no redo journal. Release the old restoration payload/slot when safe, retaining bounded history/result summaries. Same retained restore ID replays its outcome; a new ID against a known consumed record reports already restored. After historical metadata eviction report unavailable rather than assert it never existed. Older records cannot revive when a newer restore happens to reproduce their values. Local UI uses a host-local correlation ID and the same safety rules, not a second external session. Temporary preparation memory stays bounded; preserve the existing visible-eviction/reservation policy.
2. Numeric conversion: parent recommends the pragmatic strict binary64 alternative, not either participant's primary recommendation. Define correctly rounded finite binary64 parser output as authoritative; validate field domain and finite binary32 magnitude bounds BEFORE nearest-ties-to-even binary32 conversion. Accept ordinary decimal/integer-valued numeric inputs; reject booleans/strings/non-finite/out-of-domain/overflow. Thus 0.1 accepts, parsed 1.0000000000000002 rejects for opacity even though it would round to 1.0f, and a very long decimal already rounded by the binary64 parser is judged by that parsed value. Permit finite subnormals and positive underflow to zero, normalize -0 to +0. Explicitly do not promise equivalence to one-step exact-decimal→binary32 rounding or exact-lexeme domain checks. The human approved this choice; native parser/conversion conformance still needs validation.
3. IDs: recommend client-generated JSON safe integers 1..9007199254740991 shared by edits/restores within one session, server high-water initially zero, gaps allowed, client normally starts at 1. Admission atomically reserves scene/status capacity, records canonical payload identity, advances high-water and queues; pre-admission auth/schema/capacity errors consume neither ID nor slot. Scene-dependent checks occur later at the host safe point and failures then consume an admitted ID. Retained identical requests replay status; conflicting requests reject; unretained IDs at/below high-water remain unavailable and cannot execute. Unknown status for a higher ID is only a momentary observation, never authority to resend the intent under a new ID. Serialize admissions to avoid out-of-order stranding. No wrap; exhaustion requires explicit fresh-session attachment for further mutations, not silent revival. Preserve normal control/read access while the user arranges that transition.
4. Wire proposal: strict UTF-8 JSON, schema_version=1, launch_id as fresh 32-lowercase-hex public identity, scheme=http, host=127.0.0.1 or ::1, port integer 1..65535, path=/mcp, launch_credential as canonical unpadded base64url of 32 random bytes; max 4 KiB. Reject duplicate/unknown fields and unsupported versions/types. Fresh filename vsr-mcp-<launch_id>.json uses the approved secure no-replace publication. Exact Host is 127.0.0.1:<port> or [::1]:<port>; no DNS substitution/default-port elision. Every request carries Authorization: Bearer <launch credential>; session-scoped requests additionally carry VSR-Session-ID and VSR-Session-Proof: Bearer <session secret>. Those values are unrelated to native MCP session IDs. Attach/bootstrap is consumed only by trusted transport code; its private response may carry the secret without entering tool output, avoiding an assumption that an unverified SDK exposes response headers. The actual private bootstrap routing/SDK hook remains a validation deliverable, not a claim of current gateway compatibility. Explicit new proposal: VSR transport forbids redirects, proxy routing, and environment-proxy discovery. The human approved these names/encodings and direct-only routing restrictions; gateway/bootstrap feasibility is still a validation deliverable.
5. Queue proposal: tighten the earlier pending-only wording to 32 TOTAL admitted scene-work slots for discovery pages, inspection pages, renders, edit batches, restores, and resynchronization, including queued/running/yielded work. One request keeps one slot across yields. A logically canceled/timed-out backend request holds its slot until resource release is safe, avoiding an additional unbounded or unselected backend-in-flight pool; the client-visible outcome remains terminal. Applicable deadlines still start at admission. This is a deliberate scope/limit change explicitly approved by the human. A separate bounded control/metadata path permits at most 8 external requests, prioritizes/coalesces lease/cancel/detach work, and only reads integration metadata/immutable retained results, never live scene state. Local operator revocation sets bounded in-process state and cannot be blocked by external capacity; it is not an external-client privilege or an unbounded operator queue. Saturation returns busy before admission. Control-path 8 is a human-selected tunable default, not a measured result.

### Advisory 2 — approval and validation boundary

- Human response: `Approved` to the five-item package, including the binary64-before-conversion domain check, JSON safe-integer IDs, concrete descriptor/session-header contract, no redirects/proxies, and 32-total-scene/8-control accounting. This authorizes the documented design revision, not implementation, a review-grade override, or citation clearance.
- Highest-value future checks: parser/rounding boundary corpus with a reference; real gateway private bootstrap/header/reconnect/renewal/redaction experiment; restoration lifecycle and same-object version sequences; admission-order/status-race/eviction cases; scene/control saturation and non-interruptible backend cleanup. No such feature implementation or gateway spike has run.
- The advisory input spec was `a9099c06196bc885a73a7dac0f7d6d6364d0cae4122a3331d645288defaa1fff`; its approved replacement is recorded below. Review count remains 3/5 and Last review remains the original round-3 pause; neither advisory consultation consumed a review round.

### Human approval of advisory decision support 2

- Human response: `Approved`, following the parent recommendation derived from degraded advisory report `af923f2a-ac5f-4c73-b397-34972a5958c1`. Preserve the invalid Grok response, dissent, rejected assumptions and numeric counterexample in the record; no three-family agreement is claimed.
- Bound original review: `spec-review:1:3`, report `e721516f-62d7-40ba-b535-ff946a2b7491`, reviewed scope SHA-256 `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`.

| Original source(s) | Human decision and rationale | Applied specification / tests |
|---|---|---|
| gpt/finding-4 (question) | One-way Batch Restoration consumes original eligibility at commit, including committed synchronization failure. Preparation failure leaves values/versions/eligibility unchanged. Same retained ID replays; new ID against a known consumed record reports already restored, with unavailable only when the needed history is gone. No redo journal or older-eligibility revival; release payload/position when safe, retain bounded history, and use local correlation for local UI. | Decision 9; restoration and synchronization tests; glossary defines Batch Restoration. |
| grok/spec-float32-json (question) | Authoritative finite correctly rounded binary64 JSON-parser output; domain and finite binary32 magnitude checks BEFORE nearest-even binary32 conversion. Accept ordinary numbers, finite subnormals and positive underflow; normalize stored zero to +0. Reject coercions, non-finite/out-of-domain/overflow. No exact-lexeme or direct-decimal-rounding equivalence promise. This selects a practical deterministic input contract rather than accepting the coordinator's disproved blanket claim. | Decision 7 and numeric test corpus requirements, including 0.1, just-outside opacity, overflow, subnormals, -0, and double-rounding examples. |
| claude/finding-1 (defect/low); grok/spec-mutation-id-start-wrap (question) | Client-generated JSON safe integers 1..9007199254740991, server high-water initially zero, gaps allowed, atomic admission/capacity/status reservation, and no pre-admission ID consumption. Later scene validation failures consume admitted IDs. Canonical parsed-request matching, bounded unavailable-after-eviction behavior, serialized admissions, explicit status-race caution, and no wrap; fresh session needed for further mutations after exhaustion. | Decision 8; admission/out-of-order/duplicate/eviction/status-race/exhaustion tests. |
| grok/spec-descriptor-auth-contract (defect/medium); grok/spec-loopback-address-forms (suggestion) | Select exact v1 UTF-8 JSON fields and encodings, fresh 128-bit public launch ID/filename, http literal 127.0.0.1 or ::1 with explicit port and /mcp, canonical 256-bit base64url credentials, Authorization on every request, and distinct VSR session headers. Reject unknown/duplicate/bad fields, authority rewriting, redirects and proxies. Trusted non-model bootstrap remains a gate-1 integration deliverable rather than an assumed SDK feature. | Decisions 2–3; wire/publication/interoperability tests and direct-only transport scope. |
| grok/spec-queue-slot-scope (question) | Deliberately replace 32 pending-only with 32 TOTAL admitted scene requests covering discovery/inspection/render/edit/restore/resync, including running/yielded work and canceled backend work until safe release. Separate 8-request control/metadata path for attach/renew/detach/cancel/status/results; prioritize/coalesce lifecycle traffic; local revocation uses bounded in-process authority independent of external capacity. | Decision 12 and saturation/resource-lifetime tests. |

- Related editorial clarification: replace gateway/adapter shorthand with gateway or a minimal adapter if validation shows one is needed. This preserves the already-approved conditional adapter decision and does not adopt claude/finding-2 as an implementation defect; original coordinator rejection is retained.
- Revised spec SHA-256: `e36554855bd41595781a36860157d3fb274a35d2f9a6a4e75be8bbd09ecb0bce`; 70,606 bytes; 384 lines; 55 user stories.
- Glossary distinguishes one-way Batch Restoration from a new restorable Agent Edit Batch. Corresponding tests explicitly separate an edit's retained restoration eligibility after sync failure from a restore's consumed eligibility after sync failure.
- Verification: reviewed the revised contracts; mechanical checks cover template sections, 55 sequential user stories, local links, implementation-section path/snippet restrictions, approved contract/test anchors, and removal of old pending-only/ambiguous numeric wording. No production implementation, build, native parser/gateway experiment, or new panel generation ran.
- Review state remains paused, 3 of 5 rounds used, with original round-3 report/citation warnings preserved. New bytes have not been panel-reviewed. Remaining review handling includes citation verification and residual operational/previous-assessment dispositions (workspace build/install guidance, helide acceptance provisioning, and documenting already-approved choices where questioned); do not reopen these five selected contracts implicitly.

## Advisory consultation: round-3 decision support 1

- User request: `Let's get the meta-review panel help with those decisions.`
- Workflow: `panel_consult`, Standard selection implicit, one requested advisory generation; not spec-review round 4 and not an adjudication/override of the paused review.
- Status: advisory consultation complete; one generation, no review-grade outcome or gate override. Do not automatically regenerate on recovery.
- Decision scope: session-ownership proof, bounded inspection/discovery, and secure descriptor creation/publication.
- Related original sources: `gpt/finding-1`, `gpt/finding-2`, `gpt/finding-3` from `spec-review:1:3`, report `e721516f-62d7-40ba-b535-ff946a2b7491`, reviewed spec SHA-256 `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`.
- Inputs: one explicit decision question containing the agreed constraints relevant to those three choices; no attachments, repository exploration, ambient files, credentials, or parent instructions.
- Brief: optional embedded loopback MCP for independently started native/headless VSR applications; one logical external session; per-launch private credential; explicit renewable lease; capability ceiling; model-hidden secrets; application-owned safe scene access; finite queues/storage; opaque identities/revisions; existing descriptor privacy/lifetime rules. Ask for minimal v1 contracts, concrete inspection bounds/cursor semantics, portable file-safety guarantees with Linux examples, alternatives, limitations, and deterministic acceptance tests.
- Requested output: compact complete advisory recommendations and dissent, identifying any proposed change to an agreed constraint; not a review grade or assertion that the original findings have been resolved.
- Report ID: `d5f80eae-ade9-49ad-b612-5a8bc1133c2b`
- Result: complete. Standard gpt/claude/grok participants and coordinator all succeeded. No cleanup warning reported in rendered output; no independent process-cleanup audit is asserted here.
- Recommendation/coverage inspection: complete rendered pages at offsets 0, 24000, and 48000, including original perspectives, alternatives, assumptions, coordinator assessment, dissent, and proposed tests/experiments. No additional generation or raw-report export.

### Advisory recommendations — human-approved selection

The recommendations below record the advisory proposal. The subsequent approval record selects the recommended contracts and limits, not every alternative or participant assumption.

- Common direction across all participants: distinguish launch authentication from logical-session ownership with a hidden per-attach capability; use bounded incremental, revision-bound paging for discovery and inspection; publish descriptors privately and atomically in a validated per-user directory. Agreement is not empirical validation.
- A proposed contract: launch credential plus non-secret session ID plus independent random 256-bit per-attach capability. Require both authentication and ownership proof for session-scoped inspection, submission, renewal, cancellation, sensitive status/results and detach, independent of TCP connections. Deliver/store the capability through transport-private state, never model-visible tools/results/logs or the descriptor. Reject a second attach with coarse occupied status only, no incumbent session ID, handles, data, results, or observer access. Terminal sessions cannot revive; reattachment issues new capability and requires fresh inspection.
- A strongest alternative: a gateway-issued principal only if demonstrated unforgeable, model-private, reconnect-stable, and present on all relevant calls. A TCP connection alone cannot meet the agreed session-lifetime contract. A transport-private bearer cookie is not inherently TCP-bound, but must satisfy the same capability guarantees; no fallback to bare connection identity or model-visible secret is acceptable.
- B proposed contract: one paging model for registry/target/object/parameter enumeration; opaque authenticated cursors bound to session, query, target and the relevant registry/inspection revision. Returned-field, membership, order, registration or handle-mapping changes invalidate affected cursors; reject stale cursors and restart, never silently skip removed items or mix revisions. Inspection revision tracking must include returned metadata changes that do not change render revisions. No historical snapshots or full-scene result lists. Explicit completion/continuation and stop reasons; never truncate handles, types, revision/conflict tokens or other correctness-bearing values. Truncate optional display fields only with explicit markers and cheaply available original length; mandatory oversize items fail explicitly. Do not hash whole oversized fields merely to report truncation, which could defeat the work bound.
- B proposed numeric choices (parent recommendation, not panel unanimity): default 64 items/page, max 256; 1 MiB serialized response; 64 KiB/item; 16 KiB optional metadata/item; 4 KiB UTF-8/display string; 4096 candidate visits/page request; 8 MiB aggregate inspector scratch (separate from existing scene ownership and transport buffers); at most 256 candidate visits per safe-point slice with a cooperative 2 ms yield target; 5 s logical inspection deadline from admission. Normal page calls use the existing bounded scene-operation queue; renewal remains independent. Deadline ends the request as timeout, not false completion; item/byte/work exhaustion returns a continuation only for a valid revision and saved progress. The human subsequently approved these starting limits; performance and provider feasibility still require measurement, not assumptions.
- B assumptions: providers must support stable incremental enumeration and bounded individual steps. Arbitrary C++ callbacks cannot be forcibly preempted; an unsupported/non-cooperative provider must not claim this capability. Cursor validity/expiry remains bounded by its live session; no snapshot cache is needed for correctness. Retain the previously agreed finite positive launch configurability rather than silently impose the downward-only restriction suggested by some participants.
- C proposed contract: validated owner-controlled private local runtime directory, no-follow/exclusive creation of a fresh random temporary file at mode 0600 (POSIX example), complete bounded write, and atomic no-replace publication to a fresh high-entropy per-launch name only after listener/authentication readiness. Never overwrite, chmod, truncate, or follow an existing destination; use another fresh name or fail closed. Readers validate the opened regular file, owner, permissions, bounded size and strict schema, not a path checked before open. Record file identity for cleanup and leave any mismatched/uncertain entry untouched. Same-account malicious races are not excluded by a portable compare-and-unlink primitive; no such isolation is promised.
- C schema recommendation: at most 4 KiB, version, fresh launch identity, exact endpoint/authority, and launch credential; no session capability/state, scene data, or timestamp used as proof of validity. Credential must remain present as already agreed; one participant's omitted field is not a valid alternative to that constraint. Crash leftovers are inert only when the associated launch ended; a copied descriptor for a still-running launch may still authenticate. Platforms lacking validated equivalent safe-publication semantics report unsupported rather than weaken guarantees.

### Advisory dissent, limitations, and validation

- A: participants disagree on generic versus explicit occupied errors and whether to expose the public incumbent ID; parent recommends explicit coarse occupied without incumbent ID. They also differ on connection/cookie fallbacks; never weaken the transport-independent ownership requirement.
- B: GPT proposed 100/default, 256/max items, 1 MiB responses, 64 KiB/item, 5 s deadline, 2 ms slices; Grok proposed 64 items, 256 KiB responses, 8 MiB scratch, different timing/queue semantics and downward-only configuration; Claude declined numbers. The numeric set above is a proposed combination, not measured consensus. Reject silent missing-handle skipping or resume-after-mutation that would contradict explicit revision invalidation.
- C: GPT/Grok prefer strict fresh-name/no-replace publication; Claude entertained replacing stale files at a conventional location. Prefer no-replace. Combine file-identity checks with launch identity where useful; do not claim portable atomic compare-and-unlink against malicious same-account actors. Timestamp/credential omission in one minimal schema is not adopted.
- Claude's stated assumption of no cross-user attacker is incompatible with the supplied requirement to protect against other OS users/path hazards and is not adopted. Likewise any suggestion to put secrets into model-visible bodies or fall back to bare TCP ownership is not adopted.
- Recommended first experiment: real Pi/MCP path carrying launch auth and session capability privately across initialize, discovery/session-scoped operations, renewal and reconnect; verify second-client denial and inspect model-visible arguments/results/logs for leakage. Gateway incompatibility requires a minimal trusted adapter or a separately proven equivalent principal, not a weaker ownership contract. No experiment has run.
- Deterministic test priorities: owner proof required despite correct launch credential; reconnect and expired-token rejection; renewal under full queue; large-scene pagination within all caps and with explicit stale cursors/oversize outcomes; fair cooperative enumeration; wrong-owner/symlink/pre-existing descriptor rejection; no partial publication; listener ready before advertising; no cleanup of another file/instance.
- Human approval of the recommended A/B/C contracts and starting limits is recorded below. Other round-3 questions and the panel's citation-verification limitation are not resolved by this selection.
- Review state remains Phase paused, Review round 3, initial cap 5, and Last review bound to the original pause. Applying approved documentation changes does not authorize ticket cutting, implementation, or another review generation under the still-paused gate.

### Human approval of advisory decision support 1

- Human response: `Approved` to the presented three contracts and proposed limits, following consultation `d5f80eae-ade9-49ad-b612-5a8bc1133c2b`.
- Bound original review: `spec-review:1:3`, report `e721516f-62d7-40ba-b535-ff946a2b7491`, original scope SHA-256 `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`.
- This approves design clarifications and their inclusion in the unfrozen spec. It is not a severity adjudication, implementation proof, citation override, or conversion of the original pause into a clean gate.

| Original source | Approved contract and rationale | Applied specification / tests |
|---|---|---|
| gpt/finding-1 (defect/high; original effective unresolved retained) | Separate launch credential, public session ID, and independent private 256-bit per-attach session secret. Require current ownership proof for session-scoped calls, keep it hidden from model/descriptor/logs, preserve it across TCP reconnects, invalidate on session termination, and deny second clients with coarse occupied only. Launch authentication alone is not ownership; actual gateway handling must be validated or supplied by a trusted adapter. | Decisions 3–4; authentication/ownership acceptance cases; first Pi interoperability gate. |
| gpt/finding-2 (defect/high; original effective unresolved retained) | Bounded incremental revision-bound discovery/inspection, with explicit completion/continuation/staleness/oversize/timeout behavior and no full-scene snapshots. Approve the proposed finite starting limits: 64 default/256 max items, 1 MiB responses, 64 KiB/item, 16 KiB optional metadata/item, 4 KiB strings, 4096 visits, 8 MiB aggregate inspector scratch, 256 visits/cooperative 2 ms slice, 5 s admission deadline. Normal page calls use the bounded scene queue; renewal remains independent. Providers need bounded steps; no arbitrary-C++ preemption claim. | Decisions 6 and 12; large-scene discovery/inspection tests with metadata-sensitive revisions and independent resource/fairness checks. |
| gpt/finding-3 (risk/medium; original effective unresolved retained) | Private validated runtime directory, exclusive/no-follow restrictive fresh creation, atomic no-replace publication after readiness, strict opened-file checks, and identity-gated cleanup. A 4 KiB descriptor contains version, launch identity, endpoint/authority, and launch credential, not session state/secrets. Fail closed or report unsupported where equivalent guarantees cannot be validated. Prevent transient/cross-instance hazards without claiming same-account OS isolation. | Decisions 2 and 12; publication/reader/cleanup/concurrent-launch/unsafe-path tests and platform validation gate. |

- Related sources such as `grok/spec-descriptor-auth-contract` and `grok/spec-queue-slot-scope` may be partly clarified by these contracts, but no full acceptance/dismissal of those original claims is inferred. Other original questions and rejected/uncertain assessments remain separate.
- Revised spec SHA-256: `a9099c06196bc885a73a7dac0f7d6d6364d0cae4122a3331d645288defaa1fff`; 60,254 bytes; 355 lines; 55 user stories.
- Original round-3 report remains valid only for its original bytes. No review has examined this new revision. No raw panel report was exported, no technical experiment/build ran, and no code, ticket, or addendum was created.
- Documentation verification: template, story sequence, local links, implementation path/snippet restrictions, approved contract/default anchors, and the unchanged review counter/gate are checked before commit.

## Review spec-review:1:3

- Human authorization: `Caps increased as suggested. Let's resume.` following the proposed budget repair and fresh final review.
- Authorized one Standard generation as round 3 against the revised spec; at dispatch the cap was 3. Rounds 1 and 2 remain consumed. No gate override or profile/reviewer escalation.
- Status: complete Standard generation and synthesis; gate pause, not fix or clean. This attempt consumed round 3 and exhausted the then-configured cap. The later two-round extension below changes the allowance, not this outcome.
- Scope mode: spec; milestone 1 is the entire specification.
- Scope manifest: exactly `.scratch/pi-integration/spec.md`; no addenda, pipeline state, or ambient repository context attached.
- Attachment: ID `spec`, kind `text`, label `.scratch/pi-integration/spec.md`, full inline bytes.
- SHA-256: `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`
- Size: 50,560 bytes; 329 lines.
- Source change ID: `mmvnktnxyrmvmztloxrtstnvtutmtpyx`
- Source commit ID: `000e08975de863eec06bc35299f0f84674904c14`
- Base: none; Review base: none.
- Budget verification: deployed `panel.json` has reviewer caps 8000 and coordinator cap 16000; deployed validator permits those ceilings. Reviewer soft target remains 3000. Standard roster remains gpt/claude/grok. Agent made no configuration edits.
- Deployed panel configuration SHA-256: `9b11aff100d8ae31fb261483b3e4669eda3b9825f715a592207e2f5dee7d5f9c` (budget/source checks performed 2026-09-21 before dispatch; effective request budgets to be confirmed from report audit).
- Objective: completeness, ambiguity, contradictions, testability, and scope boundaries of the complete revised spec; technical validation gates are unproven requirements. Compact but complete findings/synthesis, preserving original sources, classifications/severities, uncertainties, dissent and coverage. Use exact short quotations and verifiable locations. Do not invent repository evidence or confuse intentionally unselected internal algorithms with missing observable requirements.
- Report ID: `e721516f-62d7-40ba-b535-ff946a2b7491`
- Report created: `2026-09-21T15:22:23.265Z`
- Result status: `complete`; no generation error.
- Pipeline gate: `pause`; reasons `citation-unverified`, `unresolved-significant`; notes none.
- Source classification: evaluated. Tool gate counts: fix sources 0; pause sources 15. These are gate accounting, not a count of confirmed significant defects.
- Original candidates: 15 total — 2 high defects, 2 medium defects, 1 medium risk, 1 low defect, 1 low risk, 7 questions, 1 suggestion. Preserve classifications; do not promote the coordinator's opinion that a question is defect-like into a new high/medium finding.
- Task packet hash: `7e274297d0a76cd72cdf331d451ef1377991c95734c0dee2eec5b225f281c0b9`
- Coordinator packet hash: `9bb99b3cd5e033e81ce7cee0bd079a88cd2393d2d41b7a6e6fe065a681094fc0`
- Audited artifact: inline, 50,560 bytes, 329 lines, SHA-256 exactly matches the captured manifest above.
- Effective budget confirmation: report preflight/actual audit used 8000 output tokens for each selected reviewer and 16000 for coordinator. Actual coordinator input estimate 75077 with headroom 900731; no scope truncation needed.
- Freshness: after review, both live spec and pinned source commit rehashed to `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`. No spec edits occurred in round 3.
- Inspection: complete rendered pages at offsets 0 and 23350; targeted raw pages at offsets 0, 100000, 123350, 146700. All original candidates, coverage, coordinator reasons, effective assessments, audit hashes/caps and cleanup inspected. Unread raw intervals duplicate artifact/participant text already read, not missing review scope. No raw-report export or replacement generation.

### Round-3 participants and coverage

- Standard roster gpt/claude/grok, one successful attempt each. Requested models: gpt `us/openai/openai/eccn-gpt-5.6-sol`, claude `us/azure/anthropic/eccn-claude-sonnet-5`, grok `xai/xai/eccn-grok-4.6`. Coordinator `us/azure/anthropic/eccn-claude-opus-5` also completed in one attempt; not another independent vote.
- Output usage: gpt 2799; claude 4789; grok 3765; coordinator 8511. All stopped normally. Increased caps solved this round's truncation problem; they did not establish a clean content gate.
- All four attempt processes report cleanup confirmed. No cleanup block/warning or served-identity note reported.
- Reviewers report full supplied-spec coverage, with no repository, tests, implementation, gateway experiments, addenda, prior pipeline state, or referenced background documents. Excluding those was the requested scope, not evidence that their contents are absent from the repository. No runtime/acceptance gates were executed.
- All 15 citation checks are `not-applicable: unverifiable-scope`, despite the coordinator's manual assertion that quoted text appears. Structured warnings array is empty, but the gate explicitly blocks on unverified citations. Neither a quote match nor a coordinator opinion establishes a finding or a dismissal.
- Fifteen exact singleton candidate groups; coordinator produced 14 claims, merging group-5 and group-15 on mutation-ID numbering. The merged sources retain different original classifications (low defect versus question); no parent reclassification or automatic progress.
- Effective assessments: 12 claims unresolved, 2 rejected. The rejected claims are claude/finding-2 and grok/spec-lease-extend-rule; their exact rationale is retained below. Proposed substantiated claims were generally made effectively unresolved because citations were unverified. Preserve that distinction.

### Round-3 original candidates and coordinator assessments

All locations below are original scopes in artifact `spec` at the captured round-3 hash. Every source has citation status `not-applicable: unverifiable-scope`. This table preserves the original outcome. Subsequent human-approved clarifications and their source mappings are recorded in advisory approval records 1 and 2 above; they do not rewrite the original assessments or clear citation verification.

| Exact group / source | Original classification | Original scope | Original claim / condition | Coordinator proposed → effective; rationale |
|---|---|---|---|---|
| group-1: gpt/finding-1 | defect/high | 4. Logical controller lifetime | Per-launch authentication does not specify proof binding requests to the owning session; another launch-credential holder could name/control the incumbent session despite rejected attachment. | unresolved → unresolved. Session identifier is implied and same-account OS isolation is excluded; coordinator considers high severity unestablished and cannot decide whether session proof is a deferred stack detail. Original high remains preserved. |
| group-2: gpt/finding-2 | defect/high | 6. Structured observations and safe identity | Inspection/discovery has no measurable result-size, count, traversal/time, or continuation bound, permitting large allocations or safe-point stalls on large registered scenes. | substantiated → unresolved (citation-unverified). Coordinator finds no inspection limit in decision 12, so bounded-resource acceptance lacks an inspection oracle; severity depends on scene size. |
| group-3: gpt/finding-3 | risk/medium | 2. Explicit registration and discovery | Owner-only final access does not define safe parent directory, exclusive/no-follow creation, restrictive mode at creation, atomic publication, or unsafe-existing-path behavior; transient disclosure/substitution can affect other OS users. | substantiated → unresolved (citation-unverified). Coordinator regards this as real hardening/testability gap not covered by excluding same-account OS isolation. |
| group-4: gpt/finding-4 | question | 9. Conditional restoration | After successful Restore, original eligibility/history, repeated restoration, restoration-of-restoration, and capacity accounting are unspecified. | substantiated → unresolved (citation-unverified). Same preparation/storage rules and no application-wide undo/redo leave post-restore behavior open; remains a question. |
| group-5: claude/finding-1 | defect/low | 8. Agent Edit Batch contract | Which party generates mutation IDs and how session monotonicity is enforced allegedly remains ambiguous, affecting duplicate/lost-response tests. | substantiated → unresolved (citation-unverified), merged with group-15. Coordinator acknowledges chosen-before-submission suggests client selection but says first/gap/wrap rules remain unspecified. Original low defect retained separately from grok's question. |
| group-6: claude/finding-2 | defect/medium | 4. Logical controller lifetime | Pi gateway/adapter cadence wording allegedly treats a contingent adapter as already settled, contradicting the adapter validation gate. | rejected → rejected, citation still unverified. Coordinator reads gateway/adapter disjunctively with the Solution and decision 13: whichever component proves necessary must maintain cadence, not a claim that an adapter already exists. At most an editorial clarification; no contradiction established. |
| group-7: claude/finding-3 | question | Thin adapter and acceptance checks | Workspace build-and-install-before-test convention is referenced but not reproduced in the self-contained spec. | substantiated → unresolved (citation-unverified). Coordinator sees a minor external process-reference gap; workspace guidance was not supplied. |
| group-8: claude/finding-4 | risk/low | Existing infrastructure and prior art | Required helide provisioning owner/CI tier is not assigned, potentially blurring allowed developer skips versus mandatory acceptance. | unresolved → unresolved. Spec already forbids skips as acceptance evidence; coordinator cannot decide if missing ownership is intentionally operational/out of scope without CI policy. |
| group-9: grok/spec-descriptor-auth-contract | defect/medium | 2. Explicit registration and discovery | No parseable descriptor schema/path, credential encoding/header name, or exact Host matching format is fixed for interoperability. | unresolved → unresolved. Coordinator recognizes explicit transport/version/public-name deferrals and attachment procedure as a validation deliverable; cannot decide whether a wire contract is required now. |
| group-10: grok/spec-lease-extend-rule | question | 4. Logical controller lifetime | Renewal arithmetic (reset full TTL from renewal instant versus extend remaining time) allegedly unspecified. | rejected → rejected, citation still unverified. Coordinator reads expires-after-time-without-renewal plus extends-from-monotonic-clock as a fresh TTL from the renewal instant; no genuine ambiguity demonstrated. |
| group-11: grok/spec-queue-slot-scope | question | 12. Bounded lifetime and resource use | Batch/renewal accounting is explicit, but which other operations (render, restore, resync, inspection, cancel/detach) consume pending slots is not. | substantiated → unresolved (citation-unverified). Coordinator finds queue-full acceptance needs scope beyond the two stated cases; remains a question. |
| group-12: grok/spec-light-direction-domain | question | 7. First-version edit scope | Elevation [0,360] is unusual/redundant relative to signed-elevation conventions; asks whether intentional. | substantiated → unresolved (citation-unverified). Coordinator notes the explicit formula makes every input unambiguous, so not a testability blocker; asks for intent confirmation rather than a defect. |
| group-13: grok/spec-float32-json | question | 7. First-version edit scope | Float32-representable without clamping does not state whether ordinary inexact JSON decimals round or reject. | substantiated → unresolved (citation-unverified). Coordinator considers a missing numeric conversion oracle, but original question classification is retained; do not promote its opinion into a new defect severity. |
| group-14: grok/spec-loopback-address-forms | suggestion | 3. Authentication and capability enforcement | Enumerate IPv4/IPv6 literal loopback and Host forms to reduce transport-test variance. | substantiated → unresolved (citation-unverified). Coordinator says existing endpoint/expected-authority rule may suffice; retains suggestion, not defect. |
| group-15: grok/spec-mutation-id-start-wrap | question | 8. Agent Edit Batch contract | First admitted ID, generating party, gap acceptance, overflow/wrap undefined. | substantiated → unresolved (citation-unverified), merged with group-5. Coordinator preserves extra first/gap/wrap issues and the differing original classification. |

### Human review-allowance extension

- Human instruction: `Let's go with 2 more rounds.`
- Initial spec-review allowance increases from 3 to 5 total rounds. Three attempts remain consumed; rounds 4 and 5 are available after resolving the current pause under the existing gate rules. This does not increase other review-point caps or implementer iterations.
- Bound to the existing round `spec-review:1:3`, report `e721516f-62d7-40ba-b535-ff946a2b7491`, and reviewed spec SHA-256 `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`.
- This is explicit allowance for two additional review rounds, not acceptance/dismissal of any particular original source, citation override, clean outcome, or permission to implement. Significant unresolved findings still require human decisions with source IDs and rationale.
- No round 4 dispatch has occurred. Keep Last review bound to round 3 until a new authorized, eligible scope is recorded as pending before generation.

### Round-3 recovery requirements

- The raised budgets were active and all outputs completed. This pause is for `citation-unverified` and `unresolved-significant`, not output truncation.
- Three rounds are consumed out of the human-extended initial spec-review cap of 5. Do not reset the count, reduce scope, substitute reviewers, or treat the absence of fix tickets as a clean result. Additional allowance does not clear the finding/citation pause.
- Only the human may resolve the pause. Decisions must name `spec-review:1:3`, its exact scope hash, affected original source IDs, and rationale, retaining original report/gate/assessments. Both approved advisory design packages are recorded above; remaining operational/previous-assessment handling and citation verification are not blanket-dismissed, and no clean gate override exists.
- Review citations/assessment limitations and significant unresolved claims require human handling, not another automatic adjudication call. The human has extended the allowance by two rounds; the remaining prerequisite is resolution of the existing pause, not another request for review budget.
- Automatic progress still requires a complete clean outcome. Only the explicitly human-approved spec clarifications above have been applied after this pause; no ticket cutting, addenda, implementation, new review generation, or panel configuration changes followed from that approval.
- Missing full repo ticket-format configuration and preserved unrelated `.envrc` remain independent prerequisites before ticket cutting/draining.

## Review spec-review:1:2

- Human authorization: `round 2 it is`, explicitly responding to the proposed new Standard review against the revised spec with compact synthesis.
- This authorizes one replacement generation as round 2, not reusing round 1, resetting the cap, or bypassing the gate. Round 1 remains consumed and preserved below.
- Status: generation completed with incomplete reviewer and coordinator output; gate pause, not fix or clean. This attempt consumes round 2. No automatic replacement generation.
- Scope mode: spec
- Scope manifest: exactly `.scratch/pi-integration/spec.md`; no addenda or ambient repository context.
- Attachment: ID `spec`, kind `text`, label `.scratch/pi-integration/spec.md`, full unchanged inline bytes.
- SHA-256: `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`
- Size: 41,990 bytes; 304 lines.
- Source change ID: `tkkzvmwukzosvozvrnyvxxrzvxxzllzo`
- Source commit ID: `f407e2e3f71b0a6e0708a29015f5b74f9d66f720`
- Base: none; Review base: none.
- Review objective: completeness, ambiguity, contradictions, testability, and scope boundaries across the full revised spec. Technical gates are unproven, not implemented capabilities. Compact but complete reviewer findings and coordinator synthesis; preserve every required field, source ID, severity, uncertainty, and dissent, avoiding repeated background prose. Cite exact existing section titles without Markdown heading prefixes with short verbatim evidence, or verified line ranges; do not guess line numbers. Standard selection implicit, no scope reduction or external reviewers.
- Report ID: `bc3720c5-dd69-4070-8819-d56fcdac8435`
- Report created: `2026-09-18T18:54:32.381Z`
- Report status: `synthesis-failed`
- Run error: `model output limit reached`
- Pipeline gate: `pause`; reason `invalid-or-incomplete-review`; notes none.
- Gate source classification / fix sources / pause sources: not evaluated, not zero.
- Retained original candidates: 12 from successful gpt/grok responses, unconfirmed and undeduplicated: 3 high defects, 6 medium defects, 1 low defect, 1 question, 1 suggestion. Claude has no complete validated response and is not represented as zero findings.
- Task packet hash: `83300736bf6c3bb9589a0ccadf565e151748d118d4c33fed0c3181ea1f5152f3`
- Coordinator packet hash: `ad5f22d983597c7ee4769492ccd191cf053e5655a19afd3a170103caab09336c`
- Audited inline artifact SHA-256/size/line count exactly match the captured manifest above.
- Freshness: live spec and pinned source commit rehashed after review, both matching `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`. No spec edits occurred during this round.
- Candidate/coverage inspection: complete rendered report and targeted read-only raw pages at offsets 0, 23338, 80000, 103338, 126676. The skipped raw interval contains duplicate artifact text, not uninspected retained candidates. No new generation or raw-report export.

### Round-2 participants and coverage

- Requested Standard participants: gpt, claude, grok, one attempt each. gpt (`us/openai/openai/eccn-gpt-5.6-sol`) and grok (`xai/xai/eccn-grok-4.6`) succeeded and each returned six candidates.
- Claude (`us/azure/anthropic/eccn-claude-sonnet-5`) incomplete: stop `length`, output usage 4000. Its truncated raw response is retained in the report, not parsed as a complete review. Visible fragments concern queue-entry accounting, deterministic timing, and validation-checklist coverage, followed by an unfinished fourth finding. Do not invent the missing answer or treat these fragments as gate-adjudicated candidates.
- Coordinator (`us/azure/anthropic/eccn-claude-opus-5`) incomplete: stop `length`, output usage 6000. No valid assessments/dismissal reasons exist; its partial prose/merges must not be substituted for completed synthesis.
- The objective requested compact but complete output without dropping candidates or required fields; those instructions did not prevent output-limit failures.
- gpt/grok report reviewing the entire supplied revised spec, no repository, external documents, tests, gateway behavior, or implementation. Technical gates remained unproven. Claude's partial coverage claim is not proof of a complete response.
- All 12 retained citation checks are `not-applicable: unverifiable-scope`; the warnings array is empty. Quote checks do not establish or refute a defect. Preserve these limitations despite the exact-heading citation request.
- All four attempt processes report cleanup confirmed; no cleanup warning/block or served-identity note was reported.
- All 12 exact candidate groups are singletons. In particular orientation has different original severities (gpt high, grok low); do not merge or average that disagreement. There is no valid coordinator adjudication of any overlap.

### Round-2 retained original candidates

All original locations below are scoped to artifact `spec` at SHA-256 `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`. All citation statuses are `not-applicable: unverifiable-scope`. These are unadjudicated review claims, not automatically actionable tickets.

| Exact group | Source ID | Original classification/severity | Original scope | Claim / mechanism and uncertainty retained |
|---|---|---|---|---|
| group-1 | gpt/finding-1 | defect/high | 10. Render-result contract | Correct rendered-image orientation is required without a normative origin, axis/row order, or normalization rule; the scoped Image Cache ADR is not an export convention. Future format selection might define it, but current self-contained acceptance allegedly lacks an oracle. |
| group-2 | gpt/finding-2 | defect/high | 8. Agent Edit Batch contract | Post-commit synchronization failure blocks valid rendering, but no public trigger, retry/host callback, state transition, or activity signal defines observable recovery. Internal retry algorithm may remain unspecified; missing observable recovery is the claim. |
| group-3 | gpt/finding-3 | defect/high | 10. Render-result contract | Render requests do not specify how requested scene/view state is selected or pinned (input tokens, admission, execution safe point, or operation ID), so valid versus superseded outcomes under concurrent changes are ambiguous. Opaque output tokens alone do not define render preconditions. |
| group-4 | gpt/finding-4 | defect/medium | 6. Structured observations and safe identity | Revision-token scope/invalidation and conflict granularity are unspecified, making unrelated-change acceptance vary by implementation. This is a claim about observable revision semantics, not the internal identity-mapping algorithm. |
| group-5 | gpt/finding-5 | defect/medium | 4. Logical controller lifetime | Disable agent edits is distinguished from disconnect, but revocation wording does not specify whether narrowing authority cancels only newly unauthorized mutations or also inspections/renders. |
| group-6 | gpt/finding-6 | defect/medium | 12. Bounded lifetime and resource use | The logical render deadline has a value but no clock origin (admission, queueing, safe-point execution, backend submission), making timeout results for identical traces ambiguous. |
| group-7 | grok/finding-1 | defect/medium | First-version edit scope | The finite supported edit surface and prepared acceptance scene are unnamed; required parameter types/units/ranges and first-workflow sufficiency allegedly cannot be judged from the contract alone. An unpublished scene might have been intended but was not supplied. |
| group-8 | grok/finding-2 | defect/medium | Conditional restoration | No predicate distinguishes conflicting versus unrelated changes at parameter/object/scene/token granularity for stale edits/restoration. Whole-target tokens might be intended but are not stated. |
| group-9 | grok/finding-3 | defect/low | Render-result contract | Image layout/orientation/encoding has no stated origin/channel/color-space rule, so agent/viewer consistency lacks an oracle. Original severity is low, unlike gpt/finding-1 high; preserve disagreement. |
| group-10 | grok/finding-4 | defect/medium | Agent Edit Batch contract | Lost-response handling remains an instruction to specify later; no client-visible operation/status/replay contract decides ambiguous outcomes and retry safety. Activity correlation may be intended but is not tied to retry behavior. |
| group-11 | grok/finding-5 | question | Authentication and capability enforcement | Host/origin validation and redaction rules are postponed to transport selection; unclear whether gate 13.4 intentionally permits that deferral. Not classified as a defect/risk. |
| group-12 | grok/finding-6 | suggestion | Logical controller lifetime | Consider explicit renewal operation/late-renew result rather than relying on unspecified MCP keepalive behavior. Existing general expired-session rejection may suffice; not classified as a defect/risk. |

### Round-2 human clarification round 1 — accepted

- Human response: `Agreed` to Q1–Q4 on conflict granularity, requested render state, synchronization recovery, and image format.
- Applies to `spec-review:1:2`, report `bc3720c5-dd69-4070-8819-d56fcdac8435`, original scope SHA-256 `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`.
- These are explicit human design decisions, not a completed synthesis, severity reclassification, clean gate, or replacement-generation authorization. Initially recorded without edits; subsequently applied in the revised artifact recorded below.

| Affected original sources | Human decision | Rationale |
|---|---|---|
| gpt/finding-4; grok/finding-2 | V1 edit/restoration conflicts are object-level: any parameter change on an affected material/light invalidates the relevant inspected/post-commit version, including another parameter on that object and change-back-to-old-value. Changes on other objects do not invalidate the batch. Restoration changes only the batch's parameters, never a whole-scene snapshot. | Conservative rejection avoids requiring per-parameter tracking initially. Existing object-level versions are building blocks, not proof of reliable tracking across the new transaction/failure paths. |
| gpt/finding-3 | Render requests explicitly carry desired scene and view revision tokens, returned by inspection; a completed edit returns its resulting scene token. Validate tokens at safe-point execution. A mismatch yields superseded, not rendering whichever state is current. Relevant changes during rendering also prevent success. Successful results identify matching tokens. No historical scene snapshots in v1; inspect again if desired state is gone. | Bind image provenance to explicit requested state. Scene revisions cover render-relevant changes more broadly than individual-object edit-conflict versions. |
| gpt/finding-2 | Expose synchronization states ready, synchronization required, synchronizing, failed; provide an explicit Resynchronize operation and viewer action. Application supplies recovery callback executed at a safe point. Successful recovery restores render eligibility; failed recovery stays visible and blocks affected rendering. Rebuild downstream state from the current scene without replaying the edit or changing its outcome. No automatic retry loops. | Provide a public trigger and observable recovery transition. Existing RenderIndex rebuild entry points do not establish safe failure recovery and require validation. |
| gpt/finding-1; grok/finding-3 | Normalize exported results to PNG, 8-bit RGBA, sRGB RGB channels, straight alpha, top-left origin with x increasing rightward and y downward. Normalize rendering buffers as necessary without changing scene textures or sampler coordinates. Verify with an asymmetric test image and backend format/alpha checks. | Define a deterministic export oracle. Preserve original high-versus-low severity disagreement; the human selects the contract rather than claiming the panel resolved it. |

### Round-2 human clarification round 2 — accepted

- Human response: `Agree` to Q5–Q8 on permission narrowing, deadline origin, initial edit surface, and lost-response handling.
- Applies to `spec-review:1:2`, report `bc3720c5-dd69-4070-8819-d56fcdac8435`, scope SHA-256 `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`.
- Explicit human decisions only; the original incomplete review and pause are unchanged. Initially recorded without edits; subsequently applied below. No new review authorization is implied.

| Affected original sources | Human decision | Rationale |
|---|---|---|
| gpt/finding-5 | Disable agent edits retains the session, rejects new remote edits/restorations, and cancels queued ones; an already-committing edit finishes safely. Inspections/renders continue under their remaining permissions. Other capability narrowing cancels only newly unauthorized work. Disconnect, whole-session revocation, and expiry release the session and cancel all its outstanding work under existing rules. | Distinguish capability narrowing from ending authority for the entire session; do not interrupt still-permitted observation. |
| gpt/finding-6 | The logical render deadline runs from successful admission using a monotonic clock, including queueing, waiting for safe execution, and rendering. An expired queued request never starts backend rendering. Busy rejection is not admission. Safe backend cleanup may outlast a logical timeout; tests control the clock. | Give clients an admission-to-result bound with deterministic scheduling tests rather than an unspecified GPU-only budget. |
| grok/finding-1 | Built-in v1 editing is restricted to matte color (three linear-RGB components each [0,1]), matte opacity ([0,1]), directional-light color (linear RGB each [0,1]), directional irradiance (nonnegative, W/m²), and directional direction (two angles, degrees, VSR azimuth/elevation convention, each [0,360]). Every component must be finite and float32-representable; invalid values reject, never clamp. Hosts may expose less; other built-in parameter edits are deferred. Prepared acceptance scene is a procedural sphere above a ground plane, matte materials, one directional light, fixed camera, helide. Host constructs it; agent only changes allowed values. | Close the observable edit surface and acceptance fixture without requiring runtime object creation by the agent. |
| grok/finding-4 | Edits/restorations carry session-scoped monotonically numbered IDs chosen before submission, with status query. Repeating an admitted ID never executes again: return retained status/result, reject conflicting retained payload, or report outcome unavailable if details expired while still refusing old-ID execution. After unavailable outcome/session loss, inspect and formulate a new request instead of blind replay. | Monotonic numbering supports bounded duplicate protection without retaining every historical result forever; lost responses do not imply rollback or authorize replay. |

### Round-2 human clarification round 3 — accepted

- Human response: `agree` to Q9–Q10 and the two minor bookkeeping clarifications accompanying them.
- Applies to `spec-review:1:2`, report `bc3720c5-dd69-4070-8819-d56fcdac8435`, original scope SHA-256 `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`.
- All 12 retained original candidates now map to human decisions across clarification rounds 1–3. Original severities, especially high-versus-low orientation disagreement, are preserved rather than adjudicated by an invented synthesis.

| Affected original sources | Human decision | Rationale |
|---|---|---|
| grok/finding-5 (question) | Listen only on literal loopback; descriptor identifies exact endpoint. Require a fresh cryptographically random 256-bit launch credential on MCP requests including initialization/discovery, in the authentication header rather than URLs/tool arguments/model-visible results. Accept expected Host authority only; do not trust forwarded-host claims. Reject requests carrying Origin in v1, with no authentication bypass when absent. Owner-only descriptors; no credential or whole-request-body echo in logs/errors; summaries/scene strings remain data. Same-account malicious-process isolation is not promised. | Establish an explicit minimum stack-validation/security contract while distinguishing authenticated local access from an OS sandbox. Browser-origin clients are outside v1. |
| grok/finding-6 (suggestion) | Provide explicit authenticated renewal naming current session. Successful renewal extends from server monotonic time; ordinary tool traffic does not implicitly renew. Reject late renewal as session expired and reject revoked sessions. Maintenance stays serviceable despite busy rendering/full scene queue, consumes none of the 32 scene-operation slots, and is maintained by gateway/adapter independently of model thinking at the agreed cadence. | Make lease maintenance observable and testable rather than assume MCP keepalive or model activity renews ownership. A gateway gap justifies a narrow adapter. |
| Claude truncated fragments, NOT validated retained findings | One batch counts as one queued request independently of its parameter count. Technical-validation gates do not replace acceptance testing of the whole spec. Controlled-clock testing was already approved in Q6. | Record the human's accompanying clarifications without pretending the incomplete Claude response was a complete review or inventing its unfinished fourth finding. |

### Applied revision after round-2 human clarifications

- Revised normative artifact: `.scratch/pi-integration/spec.md`.
- Revised SHA-256: `e6699db7ff30a320f50e4c69398946f8d9badec1c532b0f07e2ad678f85c804c`
- Size: 50,560 bytes; 329 lines; still 55 numbered user stories.
- Updated the corresponding observable behavioral tests, including same-object versus other-object conflict cases, change-back detection, explicit requested render tokens, resynchronization transitions, normalized PNG output, capability narrowing, admission-clock deadlines, exact parameter validation, duplicate IDs after eviction, Host/Origin/authentication rules, explicit renewal under load, and whole-spec acceptance coverage.
- Glossary now states object-level conditional restoration. Historical interview notes remain non-normative.
- Factual checks used source inspection only: existing Object parameter versions and delegate timing are not proof of integration-safe version tracking; existing SaveToFilePass flips output rows but does not establish every backend's alpha/color semantics; Light constructors and VSRMath azelToDir provide the stated initial parameters and direction convention. No production code was changed or tested.
- Verification: reviewed the updated contract/testing sections; mechanical checks passed for the exact template, 55 sequential stories, local references, implementation-section path/snippet restrictions, approved requirement anchors, and removal of superseded deferred-security/replay wording. Source formula was read to spell out the VSR direction convention rather than guessing.
- Freshness: the original round-2 report applies only to its original `ff94b5...` scope, NOT this revised artifact. The new spec has not received panel review. Last review remains the original pause, and two rounds remain consumed.
- No panel configuration/budget change, replacement generation, scope splitting, ticket cutting, addendum, or implementation was authorized or performed by these human clarification responses.

### Round-2 recovery requirements

- Human authorization `round 2 it is` has been consumed by exactly one generation. No authorization for round 3, alternate reviewers, panel configuration changes, or a gate override is implied.
- Preserve round 2, scope hashes, source IDs, original severities, and incomplete participant/synthesis status. Only the human may resolve this pause; do not repair/adjudicate spec findings automatically under a pause gate.
- Review cap is 3; two rounds are now consumed. One possible round remains only with explicit authorization following recovery. Do not reset the count or regenerate round 2.
- Recommendation for the operator: address the reviewer/coordinator output budgets before spending the remaining round unchanged; prompting compactness alone failed. No panel configuration was inspected or modified as part of this task.
- Missing repo ticket-format configuration and the preserved `.envrc` working-copy addition remain independent preconditions before ticket cutting/draining.
- No new spec edits, tickets, addenda, or implementation followed this outcome.

## Review spec-review:1:1

- Status: completed generation with invalid/incomplete synthesis; outcome recorded as pause, not fix or clean. This attempt consumes review round 1; no automatic replacement generation.
- Scope mode: spec
- Scope manifest: exactly `.scratch/pi-integration/spec.md`; no addenda or repository context supplied.
- Attachment ID: `spec`
- Attachment kind: `text`
- Attachment label: `.scratch/pi-integration/spec.md`
- SHA-256: `ba318472e87adaaa9bd8b28a99f2e3ed64b92fba34041a58ec5f77f556373b56`
- Source change ID: `wmxnzywxtzvvmvzpvqqrqqsnvotpzkkk`
- Source commit ID: `fb1e6150df4542404ae46f39df174fa24536f28e`
- Review objective: completeness, ambiguity, contradictions, testability, and scope boundaries of the supplied full specification; no repository exploration. Milestone 1 is the entire spec. Assess requirements and explicitly pending technical-validation gates without treating them as already proven capabilities.
- Report ID: `6c424e46-583c-466f-9d42-ed5f16e9cd7c`
- Report created: `2026-09-18T18:16:06.171Z`
- Report status: `synthesis-failed`
- Run error: `model output limit reached`
- Pipeline gate: `pause`
- Gate reasons: `invalid-or-incomplete-review`
- Gate notes: none
- Gate source classification / fix sources / pause sources: not evaluated; not zero.
- Retained candidates: 13, unconfirmed and undeduplicated. Original classifications: 2 high defects, 4 medium defects, 2 medium risks, 4 questions, 1 suggestion. No valid coordinator assessments or dismissal reasons exist; do not adopt partial coordinator output as an outcome.
- Task packet hash (all reviewer attempts): `f91f2796179e51c5d221a4c1cae1991581d15a9db3fd199c7749e9228e815477`
- Coordinator packet hash: `8090d50f1a9d802fb8156cf22febbd7c7bf948d88d1c0ddb4f12083dc2ebcb69`
- Audited artifact: 34,237 bytes, 279 lines, SHA-256 exactly matches the scope hash above; origin inline.
- Freshness at original outcome: live spec and pinned source commit both rehashed after review and matched the captured/audited hash. No spec edits occurred during review. Subsequent human-approved revisions are recorded below and intentionally have a different hash; this old report must not be reused as a review of the revised bytes.
- Candidate/coverage inspection: complete rendered report plus raw read-only pages at offsets 0, 23338, 46676, 70014, 93352, and 116690; no replacement generation and no raw-report export.

### Participants and coverage

- Standard requested participants: gpt, claude, grok. All succeeded in one attempt each. Requested models: gpt `us/openai/openai/eccn-gpt-5.6-sol`; claude `us/azure/anthropic/eccn-claude-sonnet-5`; grok `xai/xai/eccn-grok-4.6`.
- Coordinator `us/azure/anthropic/eccn-claude-opus-5`: one incomplete attempt, stop reason `length`, output usage 6000. Do not change reviewer selection or substitute another scaffold to recover automatically.
- All participants inspected only the complete inline spec. Design notes, glossary, repository, implementation, and validation experiments were intentionally not supplied. Their absence is disclosed coverage, not permission to widen a replacement round silently.
- All participants treated technical gates as unproven; no implementation/tests/experiments were performed by this review.
- All four attempt processes report cleanup confirmed. No cleanup warning/block or served-identity note was reported.
- Citation checks: 6 mismatched/quote-absent candidates; 7 not-applicable/unverifiable-scope candidates. No matched citations. These statuses are retained, not interpreted as proving or disproving a candidate. Structured review warnings array is empty; source checks still carry the limitations below.
- Exact candidate groups are 13 singletons, listed below. No valid synthesis/deduplication exists. Apparent overlaps remain separate original sources; silence from another reviewer is not rejection.

### Retained original candidates

Locations below preserve the reviewer's original scope or line claim; mismatched anchors are NOT silently corrected. All refer to artifact `spec` with the frozen scope hash above. These are review data, not confirmed defects or ready-to-implement tickets.

| Exact group | Source ID | Original classification/severity | Original location | Citation status | Original claim / mechanism retained for human decision |
|---|---|---|---|---|---|
| group-1 | gpt/finding-1 | defect/high | Implementation Decisions, `### 8. Agent Edit Batch contract` | not-applicable: unverifiable-scope | Atomicity does not specify whether observer notifications and delegate side effects are covered; restoring final values after a later failure may leave externally observable partial effects. Existing delegate buffering/reversal feasibility is unknown. |
| group-2 | gpt/finding-2 | defect/medium | Implementation Decisions, `### 4. Logical controller lifetime` | not-applicable: unverifiable-scope | In-flight render/non-atomic cancellation lacks selected stop/finish/status/result-retention/resource-release outcomes and bounds; mandated tests lack a deterministic oracle. |
| group-3 | gpt/finding-3 | defect/medium | Implementation Decisions, `### 9. Conditional restoration` | not-applicable: unverifiable-scope | Local viewer restoration authority after narrowing/disabling/revoking edit authority is undefined; implementations may block post-disconnect recovery or exceed launch authority. |
| group-4 | claude/finding-1 | risk/medium | lines 214–217 | mismatched: quote-absent | Deferred numeric bounds/accounting leave bounded-resource acceptance thresholds unspecified; uncertainty whether unsupplied notes select values. |
| group-5 | claude/finding-2 | risk/medium | lines 161–162 | mismatched: quote-absent | Descriptor cleanup trigger/lifetime after exit/crash/timeout is unspecified; reviewer alleges a stale per-launch credential could remain valid/readable. Crash-cleanup scope is uncertain. |
| group-6 | claude/finding-3 | question | User Stories: Access and ownership (stories 14, 43, 47) | not-applicable: unverifiable-scope | Does the local viewer require controller status for disable/disconnect/restore, or is it a separate local actor outside single-controller ownership? |
| group-7 | claude/finding-4 | question | Implementation Decisions: 13, gate 4 | not-applicable: unverifiable-scope | Does the dependency/transport validation gate refer to requirements in decisions 1–5 or to an unstated external checklist? |
| group-8 | grok/finding-1 | defect/high | lines 143–144 | mismatched: quote-absent | Mutation/notification mechanism remains investigation rather than a chosen commit/rollback model; reviewer alleges partial-apply behavior might claim compliance. Existing design-note decisions are unknown to reviewer. |
| group-9 | grok/finding-2 | defect/medium | line 118 | mismatched: quote-absent | Abort/finish/superseded/timeout/image-validity outcomes for in-flight cancellation are unspecified; reviewer alleges divergent hosts and buffer/result risks. |
| group-10 | grok/finding-3 | defect/medium | lines 128–129 | mismatched: quote-absent | No external identity/revision scheme is selected despite conflict/restoration/render dependencies; reviewer alleges ad-hoc IDs could cause wrong-object edits or false provenance. Existing reusable IDs are unknown. |
| group-11 | grok/finding-4 | question | `## Out of Scope` | not-applicable: unverifiable-scope | Are read-only non-controller attachments allowed, or are external inspection/activity/render APIs exclusive to the controller? Is headless host feedback being confused with a second session? |
| group-12 | grok/finding-5 | question | `## Further Notes` | not-applicable: unverifiable-scope | Referenced interview/glossary documents were not supplied; reviewer cannot check whether they contradict the consolidated spec. |
| group-13 | grok/finding-6 | suggestion | line 189 | mismatched: quote-absent | Document concrete bounds and renewal/expiry intervals before treating lifecycle/limit tests as specified; reviewer acknowledges values may intentionally be left to implementation. |

### Human clarification round 1 — accepted

- Human response: `Looks good` to the three recommendations on batch atomicity, in-flight cancellation, and local restoration authority.
- Applies to original review round `spec-review:1:1`, report `6c424e46-583c-466f-9d42-ed5f16e9cd7c`, scope SHA-256 `ba318472e87adaaa9bd8b28a99f2e3ed64b92fba34041a58ec5f77f556373b56`.
- This records human design decisions, not a successful panel synthesis, a clean gate, or authorization to rerun. The original pause/report and original candidate classifications remain unchanged. Decisions were initially recorded without edits and subsequently applied in the revision recorded below.

| Affected original sources | Human decision | Rationale |
|---|---|---|
| gpt/finding-1; grok/finding-1 | Prepare/validate the full batch and reserve restoration storage before mutation. Commit exposes complete new state or unchanged old state, not intermediate edits. Notify downstream consumers only after the complete commit. If downstream synchronization fails, report edits applied with synchronization failure, retain restoration eligibility, and prevent affected renders from claiming valid output until recovery. Do not promise rollback of arbitrary external callback effects. | Separate scene-state atomicity from downstream synchronization. An error after commit must not falsely imply rollback. This is a behavior to validate, not an assertion that existing update batching provides it. |
| gpt/finding-2; grok/finding-2 | When the application processes cancellation, cancel the outstanding logical request, attempt backend cancellation if supported, and discard any late image for that request without returning its status to success. Retain resources until safe release and reject new work as busy while canceled backend work still occupies the necessary view resources. A result completed before cancellation remains completed. | Provide prompt logical cancellation without promising immediate backend/GPU interruption or unsafe resource destruction; bound work instead of accumulating it. |
| gpt/finding-3; claude/finding-3 | The local viewer user is not another remote controller and needs no client lease. Label the control Disable agent edits; it disables remote edits and remote restoration. Local Restore remains available for eligible batches under the application's capability ceiling and conflict checks. Withdrawing application permission to mutate a target also disables local restoration for it. | Revoking an agent should not remove the human recovery path, while local recovery must not exceed application-owned authority. |

### Human clarification round 2 — accepted

- Human response: `Agreed` to recommendations Q4–Q7 and the accompanying recommendations for the last two review questions.
- Applies to original round `spec-review:1:1`, report `6c424e46-583c-466f-9d42-ed5f16e9cd7c`, scope SHA-256 `ba318472e87adaaa9bd8b28a99f2e3ed64b92fba34041a58ec5f77f556373b56`.
- No new panel generation, coordinator adjudication, severity reclassification, or gate override is implied. All 13 original sources now have an explicit human clarification/interpretation below or in round 1, not a declaration that the panel returned clean.

| Affected original sources | Human decision | Rationale |
|---|---|---|
| claude/finding-2 | Fresh credential and application identity every launch, never reload old credentials; owner-only descriptor access and orderly-shutdown removal. A crash may leave an inert descriptor, but it cannot authenticate to a later launch even if the port is reused. No background cleanup service required. | Distinguish a leftover file from a valid credential. A dead server does not keep accepting authentication merely because its descriptor survives. Specify launch isolation and test it. |
| claude/finding-1; grok/finding-6 | Finite positive launch-configurable defaults: lease 120 s / renewal 30 s; queue 32; batch 64 edits / request body 1 MiB; combined activity/restore storage 16 MiB with at most 1,024 activity entries / 32 restorable batches; requested image at most 1,024 by 1,024; retained images 32 MiB; logical render deadline 30 s. Reject excess pending work; evict oldest eligible retained records visibly; reject edits whose required restoration storage cannot fit. Test smaller configured boundaries plus defaults. | Configurable bounds are objectively testable; a single hardcoded universal limit is not necessary. Nevertheless selecting explicit defaults removes implementation ambiguity. A logical render deadline does not promise immediate backend termination. |
| grok/finding-3 | Opaque object handles and revision tokens, no raw-index identity; no handle reassignment to another object during a launch; removal/replacement invalidates old handles; defragmentation preserves correctly or explicitly invalidates; earlier-launch tokens cannot authorize later-launch edits. Internal mapping algorithm remains an implementation choice without persistent UUID/archive requirements. | The original negative identity invariant is testable without selecting a particular UUID implementation. Resolve the external contract rather than treat the unselected internal algorithm as proof of an untestable requirement. |
| grok/finding-4 | One external session slot in v1, including inspection-only attachment. Controller means slot ownership rather than unlimited edit permission. Local viewer/headless-host access is in-process and consumes no slot; additional external observers remain deferred. | Separate remote session admission from capability scope and local host feedback. |
| claude/finding-4 | Validation gate 4 explicitly references Implementation Decisions 1–5 and 12 and enumerates their optional-dependency, activation, descriptor/authentication, access, session, execution, and resource requirements. | Gate acceptance refers to the spec's own enumerable requirements, not an unstated external standard. |
| grok/finding-5 | Spec is the authoritative self-contained contract. Design notes are historical background and the glossary is vocabulary; neither adds or overrides acceptance requirements. | Missing background documents do not silently widen review scope or create hidden normative requirements. |

### Applied human-approved revision

- Revised files: `.scratch/pi-integration/spec.md` and `.scratch/pi-integration/CONTEXT.md` only; no production code or tickets.
- Revised spec SHA-256: `ff94b5a8986d3dc8f0480031998112137e70f8c8e69f231a87b0d43820e2c75c`
- Revised spec size: 41,990 bytes; 304 lines.
- Updated behavioral test requirements alongside contracts, including no intermediate mutation notifications, committed synchronization failure, cancellation/timeout late-result suppression, local recovery authority, per-launch credential invalidation, read-only slot ownership, opaque-handle safety, and configurable resource-boundary checks.
- Glossary clarifies that downstream synchronization failure does not undo a committed Agent Edit Batch.
- Verification: reviewed changed contract/testing sections; mechanical checks passed for requested template sections, 55 sequential user stories, local links, no implementation file paths/snippets, and approved clarification/default anchors. No runtime tests or interoperability experiments were performed.
- Review freshness at revision commit: the revised scope had not been reviewed. Round 1 remains consumed with its original pause. The subsequent explicit round-2 authorization and newly captured manifest/hash are recorded above; do not reset the count or reuse the old report.

### Recovery requirements

- Only the human may resolve this pause. Inspect the retained report and candidate/source IDs; do not invent a completed synthesis or treat an incomplete coordinator answer as a valid decision.
- Human clarification rounds 1 and 2 are recorded against the original round/hash/sources and applied to the unfrozen spec. The human subsequently authorized one new generation, `spec-review:1:2`, above. No gate override has been authorized.
- Round 1 has already been consumed. Any replacement generation needs explicit human authorization and must not reuse round 1 or silently reset the count; clearing `Phase: paused` alone is not retry permission.
- Automatic progress still requires a recorded complete clean outcome. No tickets were cut and `Frozen: no` remains truthful.
- Before ticket cutting, run `/setup-matt-pocock-skills` to establish the repo's full local ticket contract, then resolve handling of the preserved pre-existing `.envrc` change before work-tickets can classify working-copy changes. These are separate from the review failure.

## Preconditions and preserved work

- Existing working-copy addition `.envrc` predates this pipeline. It remains uncommitted and is excluded from all orchestrator commits and review material. It must not be treated as failed implementer WIP.
- `.scratch/` is globally ignored. Only this feature's explicitly named spec/design/glossary files and pipeline state are tracked for the requested local pipeline; ignore configuration is unchanged.
- `docs/agents/issue-tracker.md` and the repo-level skills setup configuration are absent. The required full ticket-file contract must be supplied/configured before cutting tickets; do not invent it from a partial template.
- No implementation has been dispatched. No tickets or addenda exist. No post-review has run.

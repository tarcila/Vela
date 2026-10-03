# 05: Obtain independent ownership and revision review

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Kind:** extends
**Files:** .scratch/scene-bounds-invalidation/issues/01-lifetime-safe-payload-invalidation.md, .scratch/scene-bounds-invalidation/01-verification.md

## Hole

The feature contract is settled, and ticket01's implementation and automated verification are preserved in change `xrwvlnwz`. The frozen spec and01 require independent correctness/Standards review. This dispatch forbids subdelegation and exposes only file/shell tools, not an independent review channel. The spec does not specify how to route that review in this restricted execution environment.

This is an **execution-route** extension, not a new feature decision, source split, budget objection, or proposed waiver. Human action is needed to obtain the required independent review. Static analyzer, Valgrind, public behavioral regressions and author inspection have run; none is dishonestly relabeled as the missing independent correctness/Standards reviewer.

## Current evidence

See `../01-verification.md` for exact commands, matched counts, memory-tool limitations, requirement mapping and red evidence. Final results:10 focused cases/1,201 assertions;291 integration cases/109,853 assertions;50 supporting cases/1,059 assertions;68/68 CTest; Valgrind exit0 with0 errors and0 definite leaks (reported CUDA runtime possible/reachable allocations disclosed); Clang analyzer4 translation units without diagnostics; formatting passes; installed/build device hashes match. Enabled existing build configuration is unchanged.

## Required action

Arrange an independent human or separately authorized reviewer of the code in `xrwvlnwz` and the frozen spec, concentrating on:

- Scene and Layer revocation order, actual owner destruction/reuse and object-resolution/use-count balance;
- attached assignment, detached construction/copy, source move-out versus storage relocation, root/gaps/failed/reused storage;
- coherent SceneRevision/InspectionRevision folding in either getter order, per-registration/per-Scene locality, bounded observations and checked exhaustion;
- preservation of existing precise histories, coarse callbacks, batching, generic Forest/Data Tree behavior, and coding standards.

Record actual reviewer findings/disposition, resolve any necessary01 corrections with fresh build/install/verification, and only then resolve05 and01. Do not dispatch an agent from this restricted iteration, silently waive review, modify executor/orchestrator configuration, or count the intermediate capability as complete bounds validity.

## Out of scope

Implementation of02/03; bounds calculation/publication, camera/capture/Device qualification, image-policy or encoder changes, historical ticket reconciliation, normal executor/configuration, other workspaces and user-private artifacts.

## Comments

### 2026-10-03 iteration — ready-for-human

Filed because the required independent review route is unavailable within this no-subdelegation dispatch.01 is blocked on05 and remains ready-for-human with verified source preserved. This is not a subdivision of the coupled payload ownership capability.

### 2026-10-03 orchestrator — independent review obtained at finish
The outer orchestrator supplied two fresh independent code-reviewer children for Standards and Spec/correctness. They deeply reviewed01's WIP and the latter reproduced10focused/1201assertions twice,291integration cases four times and68/68CTest. No blocking functional defect found in reviewed01 scope; Standards ownership-rule/documentation disposition remains in06, with friendship clarity07 and optional08–11 queuedneeds-triage. Full accumulated trunk review coverage is limited and explicitly disclosed in [finish-review.md](../finish-review.md).
The execution route is now demonstrated; no new feature decision or missing review credential remains. This record staysready-for-human as the review-disposition gate requested above until required findings are accepted/addressed and01 is properly closed. Do not interpret reviewer output as source-correction authority, waive standards/lifetime requirements or automatically resume02/03 after finish.

### Human acceptance — resolved
User explicitly approved01 and05 following the recorded independent reviews. The review route and this disposition gate are closed;01 is resolved against its existing tested implementation. Remaining04/06–11 needs-triage records are neither automatically accepted for implementation nor dismissed. No additional implementation dispatch, source correction, standards amendment or claim of exhaustive trunk approval follows this closure.

# 18: Triage repeated Dataset reacquisition in lifecycle test

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/tests/test_SciVisStudio.cpp

## Finding

Standards reviewer suggested a local helper for repeated findDataset-by-ID followed by REQUIRE in the strengthened lifecycle Scenario. Explicit reacquisition makes each invalidation boundary visible and follows an existing idiom elsewhere in this suite. Reviewer found no required change and warned that broader helper/test-infrastructure refactoring is outside the authorized narrow correction.

This is an optional Duplicated Code/readability judgement, not a lifetime defect. Removing a lookup or carrying its borrowed result across a save/inventory mutation would restore the use-after-free.

## Acceptance criteria

- [ ] Human dismisses as useful explicit boundary evidence, defers, or accepts a strictly local helper without broad test refactoring.
- [ ] Preserve per-boundary current-record lookup, stable Dataset ID and all root/transaction/asset assertions and Memcheck regression evidence.

## Comments

Optional Standards finish finding from15's two-source-file correction. Both independent reviewers approved that correction with no required changes. No automatic cleanup or new implementation loop.

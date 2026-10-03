# 10: Triage interpretation of variable assertion totals

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** .scratch/scene-bounds-invalidation/01-verification.md, src/tests/integration/

## Finding

Spec reviewer reproduced291 passing integration cases four times but observed109594–109825 assertions rather than the author's single-run109853. Focused payload cases reproduce exactly10/1201. No failing case or01 regression was demonstrated.

The existing asynchronous integration tests can count polling-dependent assertions differently between runs; historical verification already acknowledges variable totals. This finding concerns proof-report interpretation, not automatic evidence of flakiness. Stable matched case counts and successful assertions remain separate from requiring a deterministic total.

## Acceptance criteria

- [ ] Human dismisses as expected counted polling, or authorizes investigation only if there is evidence of varying test coverage/failures rather than incidental poll counts.
- [ ] Reports preserve actual per-run totals and matched case counts, never fabricate equality or change tests/tolerances solely to force totals to match.

## Comments

Optional Spec-axis finish finding; likely expected pre-existing variability, not a new defect. No automatic tests refactor or continuation.

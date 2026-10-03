# 14: Triage long-term counter-exhaustion host policy

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/core/Forest.hpp, src/vsr/scene/SceneDependencyState.hpp

## Finding

Spec reviewer mentioned that terminating on lifetime-counter wrap is abrupt for a host and might be considered long-term. Reviewer explicitly found this satisfies the frozen requirement to fail safely rather than reuse old validity; it also matches the already accepted clock pattern. No failing path or current defect was supplied.

This is a prospective design note, not a reason to weaken checked exhaustion or block02. Any alternate error propagation is a separate explicit compatibility decision; wrapping/reusing tokens is never acceptable. Existing11 concerns guard discipline, not this termination-policy question.

## Acceptance criteria

- [ ] Human dismisses as established policy, defers to future host error design, or explicitly commissions a separate compatible proposal.
- [ ] Preserve current fail-safe checks and token/lifetime non-reuse until a separately approved alternative exists.

## Comments

Optional Spec-axis second-run finish observation; no required source changes, new product hole or implementation loop.

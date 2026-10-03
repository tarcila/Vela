# 16: Triage deliberate cross-Forest copy clarity

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/core/Forest.hpp, src/tests/test_Forest.cpp

## Finding

Standards reviewer suggested explaining why copy_subtree checks the receiving newParent container but does not require the source container to equal the receiver. Generic Forest copying can intentionally accept a foreign source; scene-specific ownership is guarded by the optional CopyValidation preflight. This differs from reparent, which does not transfer storage across Forests.

Optional clarity judgement, not a required change or established defect. Do not forbid supported generic cross-Forest copies merely to make the guards symmetrical, or weaken Scene preflight to match generic values.

## Acceptance criteria

- [ ] Human accepts a concise why-comment, defers or dismisses the concern.
- [ ] Any change preserves generic default-neutral copying, supported same-Scene cross-Layer cloning, foreign Scene rejection before mutation and reparent restrictions.

## Comments

New optional Standards finish finding on03 vxxspsoo/08f72000. The underlying cross-Forest behavior is existing public compatibility, not a new Scene transfer promise. No automatic source change or post-finish loop.

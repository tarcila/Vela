# 11: Triage dependency-state guard discipline

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/scene/SceneDependencyState.hpp, src/vsr/scene/Scene.cpp, src/vsr/scene/LayerNodeData.cpp

## Finding

Spec reviewer listed an optional concern that SceneDependencyState::noteChange termination/revocation guarding relies on caller discipline, explicitly not currently exploitable. No failing path or concrete missing-overflow check was supplied.

Source inspection shows noteChange itself checks the live Scene pointer and terminates on revision wrap; Scene revocation order and owning-thread use are part of the existing contract. Triage must identify a concrete additional invariant before changing this implementation or labeling it a correctness bug. Raw-pointer prohibition/ownership rationale is separately captured in06.

## Acceptance criteria

- [ ] Human requests a specific invariant audit, defers, or dismisses the underspecified optional concern.
- [ ] Any accepted change preserves checked exhaustion, no owner resurrection, O(1) scene-local validity and callback/history compatibility, with public tests.

## Comments

Optional Spec-axis finish finding retained without inventing missing evidence. No source change or automatic follow-up run.

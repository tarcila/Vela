# 04: Triage the missing pointer Layer-activation overload

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Type:** adjacent bug, not a specification hole
**Files:** src/vsr/scene/Scene.hpp, src/vsr/scene/Scene.cpp, src/tests/test_Scene.cpp

## Current behavior

`Scene.hpp` declares `Scene::setLayerActive(const Layer *, bool)`, but `Scene.cpp` defines only the Token overload. A public regression using the pointer overload compiled and then failed to link with an undefined reference. This is present in the dispatch parent `uwnoqyvl`, not introduced by ticket01.

Evidence: `../logs/01-red-foreign-build.txt` contains the linker failure; `jj file show -r uwnoqyvl src/vsr/scene/Scene.cpp` and the corresponding header confirm the pre-existing mismatch. Ticket01's compatibility control uses the implemented Token overload instead, without changing the activation API.

## Desired behavior

Triage whether to implement or remove the unsupported declaration. If implemented, qualify the observable active-Layer state, existing notifications and scene revisions, including unavailable/foreign Layer arguments, through public Scene tests.

This finding does not block ticket01's payload capability. It is not authorization to change the frozen spec, activation semantics, or broader Scene APIs during ticket01.

## Out of scope

Payload ownership/attachment, membership/transfer invalidation, capture, image policies, user configuration and historical camera/capture tickets.

## Comments

### 2026-10-03 iteration — needs-triage

Recorded the adjacent pre-existing linker defect; no source repair attempted. The implemented Token overload remains covered by the passing public compatibility matrix.

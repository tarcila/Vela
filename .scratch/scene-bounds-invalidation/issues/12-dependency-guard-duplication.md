# 12: Triage dependency-guard duplication

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/SceneDependencyState.hpp

## Finding

Standards reviewer suggested sharing small guard logic in Layer::noteMembershipChange and LayerNodeData::noteDependencyChange. This is an optional Duplicated Code/readability judgement, not a documented-standard violation or runtime failure.

The methods are similar but not identical: payload writes additionally require attached value and a live Layer owner; Layer already owns its state. A common helper must not erase those distinctions or re-enable dependency tracking on a removed Layer. Four-line similarity alone does not establish a worthwhile abstraction.

## Acceptance criteria

- [ ] Human accepts targeted consolidation, defers to a future touch, or dismisses the concern.
- [ ] Any accepted change preserves attachment/owner expiry/removal guards, no extra callbacks and focused payload/membership tests.

## Comments

Optional Standards finding from independent second-run finish review of vuxprvxk/3b1010d3. Both axes approved the in-scope02 source with no required changes. No automatic cleanup or implementation loop.

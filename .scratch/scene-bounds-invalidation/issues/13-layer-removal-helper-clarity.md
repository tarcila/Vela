# 13: Triage repeated Layer-removal sequence

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/scene/Scene.cpp, src/vsr/scene/Layer.cpp, src/tests/integration/test_IntegrationMembership.cpp

## Finding

Standards reviewer suggested a private detach helper for the repeated signalLayerRemoved/revokeMembership sequence in removeLayer overloads and removeAllLayers. The repeated explicit signal pattern predates02; this is an optional future-maintenance judgement, not a new correctness defect or requirement to refactor now.

Preserve notification timing relative to the Scene Layer map, membership revocation and actual erasure. Retained Layers must keep safe object resolution/use counts without becoming registered or advancing Scene dependencies again.

## Acceptance criteria

- [ ] Human accepts a later helper extraction, defers or dismisses the concern.
- [ ] Any accepted change keeps exact explicit callback/batch behavior, identity retirement and retained-owner tests passing.

## Comments

Optional Standards finding from independent second-run finish review of vuxprvxk/3b1010d3. No required source changes or automatic next run authorized.

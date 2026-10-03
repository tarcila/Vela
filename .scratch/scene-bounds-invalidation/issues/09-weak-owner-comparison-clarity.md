# 09: Triage weak-owner comparison clarity

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/scene/LayerNodeData.cpp, src/tests/integration/test_IntegrationIdentity.cpp

## Finding

Standards reviewer flagged the double owner_before idiom in LayerNodeData::bind and checkAssignmentOwner as correct but subtle. One distinguishes unset from expired ownership; the other compares ownership identity. A helper/comment might improve readability, but conflating the two meanings into one generic helper could make it worse.

This is a judgement-call clarity concern, not a weak-pointer correctness defect. Existing comments already explain parts of the reason; no extraction is required merely because the syntax repeats.

## Acceptance criteria

- [ ] Human accepts targeted clarification, defers or dismisses the concern.
- [ ] If changed, expired/unset/foreign/same ownership remain distinct and public expiry/assignment/reuse regressions pass without resurrecting expired indices.

## Comments

Optional Standards-axis finish finding on WIP xrwvlnwz. No source fix or automatic continuation.

### Third-run review — same concern, not a duplicate ticket
Standards reviewer noted another use of the ownership-identity idiom in Layer::validateCopy and suggested a sameOwner-style helper. Existing unset-versus-expired and foreign-versus-same-owner meanings must remain distinct. Retained here as the same optional clarity concern, not a required source change or new transfer defect.

# 07: Triage lifetime-state friendship rationale

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/scene/Scene.hpp, src/vsr/scene/Layer.hpp, src/vsr/scene/LayerNodeData.hpp

## Finding

Standards reviewer requested a rationale for new Scene friendship granted to Layer and LayerNodeData, or a narrower internal accessor. The friendships permit lifetime-state wiring but currently lack an adjacent why-comment.

Reviewer cited CODING_STANDARDS section13's commented ImagePipeline friendship example. That section does **not** explicitly mandate comments on every friend declaration; classify this as a readability/encapsulation judgement rather than an established hard violation. Section12's explain-why guidance is relevant. A mechanical accessor refactor is not automatically better than narrowly justified friendship.

## Acceptance criteria

- [ ] Human decides whether to add a concise why-comment, change the private seam, or dismiss the concern.
- [ ] Preserve lifetime revocation, root initialization and state privacy; any source adjustment passes formatting and appropriate regressions.

## Comments

Filed from independent Standards finish review of WIP xrwvlnwz in the pinned trunk range. No actual ownership failure alleged, source changed or automatic follow-up authorized.

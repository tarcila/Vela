# 06: Document revocable lifetime ownership

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** CODING_STANDARDS.md, docs/adr/, src/vsr/scene/SceneDependencyState.hpp, src/vsr/scene/Scene.hpp, src/vsr/scene/Layer.hpp, src/vsr/scene/LayerNodeData.hpp

## Finding

Independent Standards finish review of01 flagged the shared SceneDependencyState/LayerOwnerState control blocks and weak payload references against CODING_STANDARDS sections6/16. Section16 says stored non-owning members are always raw pointers; weak lifetime-qualified state needs an explicit documented distinction/exception. Shared pointers actually owning their state are not themselves non-owning-pointer violations. The approved spec requires safe revocation, so replacing weak guards with unqualified raw owner pointers is not a valid fix.

The reviewer recommends an ADR and/or a narrow standards clarification for revocable lifetime observation, explaining why owning the control block does not keep Scene/Layer alive and how expiry/object-count semantics work. This is the substantive Standards disposition still needed for01, not a demonstrated runtime defect. Architecture guidance says expensive ownership decisions should be recorded.

## Acceptance criteria

- [ ] Human accepts a narrow documented lifetime-control exception or requests another compliant lifetime-safe design; no weakening of the frozen spec.
- [ ] If documentation is accepted, distinguish owned control blocks, weak lifetime observation and ordinary stored raw owner pointers, record rationale/teardown guarantees, and follow domain/ADR conventions.
- [ ] Any source change is independently reviewed and reverified; documentation alone must not claim new runtime behavior.

## Comments

Filed from the Standards-axis finish review, pinned trunk e04c2853d04d3076b4f0870efc49f787bd870010 to02ce3e4c811875eb3b579039a7d18c5629db887f, with deep review of WIP xrwvlnwz. No automatic fix or next implementation loop.

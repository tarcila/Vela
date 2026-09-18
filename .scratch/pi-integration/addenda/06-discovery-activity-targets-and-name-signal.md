# Addendum 06 — Discovery covered targets in the activity log; name edits signal metadata change

**Extends:** addendum 04 items 1 and 5, spec §6 and §9. **Source:** holes 152 and 153, raised by ticket 140. **Decided by:** orchestrator.

## Decisions
1. **Discovery covered targets (152):** the activity log's discovery entries record exactly the covered targets the operation engine reports, which are the targets listed on the page. A cross-session lookup through the activity log checks read on each of them.
2. **Name edits (153):** every name-mutating path fires the object metadata-change signal, including `Object::editableName()` as used by the ImGui inspector. Implement this either by firing the signal when the edited name is committed, or by replacing the mutable reference with a setter-based API at call sites. A name change made through any public path advances the inspection revision.

## Why
Addendum 04 requires the inspection revision to track name and metadata changes incrementally, and requires a discovery record's covered targets to be its listed targets. These two gaps would make either guarantee hold only on some code paths.

# 115 — Discovery is admitted against scene 0, not the targets it lists

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/operations/Operations.hpp
**Kind:** extends

Ticket 103 found this gap while mapping discovery onto MCP. The adapter still maps the current behavior faithfully:

- `Inspector::makeDiscovery` submits with `Target::scene(0)` and `Capability::READ`. So a session that cannot read the first registered scene gets `forbidden` from `vsr_discover`, even when other targets are readable. When no scene is registered, it gets `invalid_request`.
- `OperationEngine::status` therefore treats scene 0 as the only covered target of a discovery record. Spec §9 says a discovery page covers every target listed in it.

The spec does not say which target should admit discovery, which is session-wide. The cheap assumption is to keep the current behavior for now. To revisit, admit discovery against a session-wide target, or pick any readable scene. Then record the listed targets on the record so that foreign lookups check read on each of them. Changing this has no MCP wire impact beyond making `forbidden` rarer.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 04 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 140.

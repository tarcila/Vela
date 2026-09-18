# 118 — No wire source for scene/view revision tokens

**Status:** resolved
**Milestone:** 1
**Kind:** extends
**Blocked by:** none
**Files:** src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/mcp/README.md

Spec §12 says a render names "scene/view revision tokens from inspection or completed edits", and spec §7 says "Inspection returns scene/view tokens; a committed edit returns its resulting scene token". The headless modules do not put them on the wire yet:

- `vsr_inspect` returns only the inspection-cursor `revision` (hex), not `SceneIdentity::sceneRevision()`. Nothing returns a view's `ViewRevisionTracker::revision()`.
- `vsr_edit` results carry only `batch_ref` (hole 116). `EditResult::sceneRevision` exists but is not serialized.

Ticket 105 assumed a token wire format: the string `"<activation>.<nonce>.<domain>.<value>"`, the same format that `render::detail::revisionText` already emits in a successful render result. `vsr_render` parses it. The only way to obtain tokens today is a previous successful render, or the host. Tests take them from the host side.

Revisit: add `scene_revision` to inspection page results and edit/restore results, and `view_revision` to discovery view items (or a view inspection), using the same string format. No MCP adapter change is needed beyond README rows. Pi end-to-end (ticket 18) cannot perform a first render until this is done.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 04 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 141.

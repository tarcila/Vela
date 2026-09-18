# 153 — Object::editableName() bypasses the metadata change signal

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/scene/Object.hpp, src/vsr/scene/Object.cpp, src/vsr/ui/imgui/vsr_ui_imgui.cpp

**Kind:** extends

Ticket 140 added `BaseUpdateDelegate::signalObjectMetadataChanged`. `setName` and the `set/removeMetadata*` functions emit it, which bumps the integration's inspection revision incrementally. `Object::editableName()` returns a mutable reference that the ImGui inspector writes through (`vsr_ui_imgui.cpp:369`). No signal fires on those edits, so cursors do not go stale when a name is renamed in the UI. `SceneIdentity::refreshMetadata/refreshAllMetadata` remain as explicit fallbacks.

Assumption: acceptable for v1, because nothing in the §14 workflow renames through the UI. Revisit by having the UI call `setName()` after an edit, or by replacing `editableName()`.

### 2026-09-28 orchestrator
Accepted as extends via addendum 06; now ready-for-agent. Contract: spec + addenda 04 and 06.

## Comments

### 2026-09-29 iteration — resolved
Removed `Object::editableName()` (Object.hpp/.cpp). The ImGui inspector (vsr_ui_imgui.cpp) now edits a copy and commits with `setName()`, which fires `signalObjectMetadataChanged`, so every public Object rename path advances the inspection revision (addendum 06 item 2). Across src/, the only Object `editableName()` call site was vsr_ui_imgui.cpp. `Animation::editableName`, `Layer::editableName` and the serialization_datatree.cpp use are on non-Object types and were left unchanged.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrTests vsrIntegrationTests vsr_ui_imgui vsrViewer` → OK; `vsrTests "[Object],[Scene]"` → 114 assertions/7 cases pass; `vsrIntegrationTests "[integration]"` → 5027 assertions/129 cases pass
Holes filed: 176-identity-comment-cites-removed-editablename

# 01 — Inspect actual registered-view configuration

**What to build:** Obtain the actual camera, device, renderer/pipeline provenance and capture policy of an explicitly registered view through the public headless and MCP interfaces, without creating render resources or guessing from an unrelated Scene camera.

**Blocked by:** none
**Status:** wontfix
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1–2; Testing Decisions 1 and 10.

## Current behavior

Discovery lists view identity, permissions and revision; the independent agent view uses its own camera/device. No complete registered-view configuration observation is exposed.

## Acceptance criteria

- [ ] Admitted vsr_view_info returns matched scene/view/policy revisions, effective world-space camera/projection, Device Identifier, renderer subtype/settings and Image Pipeline provenance, supported quality/limits/exposure and explicit unavailable/unknown reasons.
- [ ] Observe the actual host binding at its safe point. Selected Scene camera references are reported only when actually bound; no raw handles, process information or credentials leak.
- [ ] Register/read the selected viewer viewport and explicitly distinguish the legacy agent binding; capture remains unexposed until qualification, not enabled by metadata alone.
- [ ] Camera units/aspect are explicit; read-only unsupported projections and missing correctness fields are truthful. No GPU work or renderer allocation is triggered by inspection.
- [ ] Authority, admission/replay, revision/policy invalidation and legacy discovery/render contracts remain correct.
- [ ] Public headless and thin MCP tests prove real binding versus unrelated camera, changed configuration, current read denial and bounded representations; full existing regressions pass.

## Out of scope

Capture execution, bounds, camera writes, device fallback, server file access and architectural cleanup.

## Comments

The user approved the twelve-ticket graph and confirmed this spec directory for a serial work-tickets run with ten dispatches.

### 2026-10-03 iteration — wontfix
split into 13, 14. No implementation started: the complete admitted headless/MCP observation contract and the real viewer's owning-thread effective-binding adapter cannot both be implemented and verified within this sitting's 20-minute limit.
13 delivers public host-binding inspection end to end; 14 registers/observes the actual viewport and distinct legacy agent without enabling capture. Both preserve Milestone 1 and all original acceptance obligations. The original omitted Files; the parts name the inspected in-scope file subsets. Existing 04's dependency on 01 should be interpreted as completion of both replacement parts; no sibling was edited.
Verified: `jj diff --name-only` → exactly 01, 13 and 14 ticket paths; spec and siblings unchanged. A Python assertion check of paths/statuses/milestones/edges/acceptance lists passed after explicitly tracking both ignored new records with `jj file track --include-ignored .scratch/camera-capture/issues/13-admitted-view-info.md .scratch/camera-capture/issues/14-viewer-binding-observation.md` (the first scope check detected the records were not tracked without that flag). Documentation-only split; no build/runtime claim.
Holes filed: none
